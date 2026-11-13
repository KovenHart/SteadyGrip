// SteadyGrip - couche OpenXR implicite : stabilise la pose des manettes (micro-tremblements dus aux
// recalages optiques, téléportations au retour dans le champ des caméras).
//
// Les poses des manettes sont modifiées dans xrLocateSpace et xrLocateSpaces. La pose du casque
// (espaces de référence) n'est jamais modifiée. Les états des boutons grip sont seulement lus (au passage,
// quand le jeu les demande), jamais modifiés.

#include "../common/Shared.h"
#include "../common/Steam.h"

#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <cstdarg>
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace sg;

#define LAYER_NAME "XR_APILAYER_NOVENDOR_steadygrip"

namespace {

// --- Journal -------------------------------------------------------------------------------------
std::mutex g_logMutex;
void Log(const char* fmt, ...) {
	std::lock_guard<std::mutex> lock(g_logMutex);
	static FILE* f = [] {
		FILE* file = nullptr;
		const std::wstring path = DataDir() + L"\\SteadyGrip.log";
		_wfopen_s(&file, path.c_str(), L"w");
		return file;
	}();
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	OutputDebugStringA(buf);
	if (f) {
		fputs(buf, f);
		fflush(f);
	}
}

// --- État de la couche ---------------------------------------------------------------------------
XrInstance g_instance = XR_NULL_HANDLE;
PFN_xrGetInstanceProcAddr g_next = nullptr;
PFN_xrDestroyInstance g_xrDestroyInstance = nullptr;
PFN_xrDestroySession g_xrDestroySession = nullptr;
PFN_xrCreateActionSpace g_xrCreateActionSpace = nullptr;
PFN_xrDestroySpace g_xrDestroySpace = nullptr;
PFN_xrLocateSpace g_xrLocateSpace = nullptr;
PFN_xrLocateSpaces g_xrLocateSpaces = nullptr;
PFN_xrLocateSpacesKHR g_xrLocateSpacesKHR = nullptr;
PFN_xrPathToString g_xrPathToString = nullptr;
PFN_xrCreateAction g_xrCreateAction = nullptr;
PFN_xrSuggestInteractionProfileBindings g_xrSuggestInteractionProfileBindings = nullptr;
PFN_xrCreateReferenceSpace g_xrCreateReferenceSpace = nullptr;
PFN_xrGetActionStateFloat g_xrGetActionStateFloat = nullptr;
PFN_xrGetActionStateBoolean g_xrGetActionStateBoolean = nullptr;
PFN_xrSyncActions g_xrSyncActions = nullptr;
std::string g_appName = "?";

// Actions booléennes ou analogiques du jeu : on repère celles liées au bouton grip (« squeeze »).
struct InputAction {
	std::string name;
	int gripHands = 0;   // bit 0 : liée au grip gauche, bit 1 : au grip droit
};
std::map<XrAction, InputAction> g_inputActions;
struct GripState {
	double value = 0, t = 0;
	bool seen = false;
};
GripState g_grip[2];
std::map<XrPath, int> g_pathHand;   // cache sous-chemin -> main

// Correction du grip qui se relâche par instants : un filtre par action et par sous-chemin demandé.
// L'état corrigé est calculé une fois par xrSyncActions, pour que « changé depuis la dernière
// synchronisation » reste cohérent même si le jeu lit l'action plusieurs fois.
uint64_t g_syncCount = 0;
struct GripFix {
	GripFilter filter;
	uint64_t evalSync = UINT64_MAX;
	double out = 0;
	bool changed = false;
	XrTime lastChange = 0;
};
std::map<std::pair<XrAction, XrPath>, GripFix> g_gripFix;
GripFilter::Stats g_gripStats[3];   // gauche, droite, main inconnue (statistiques de la période)
bool g_gripFixLogged = false;

struct ActionInfo {
	std::string name;
	std::vector<std::string> subPaths;
	int boundHands = 0; // bit 0 : liée à /user/hand/left, bit 1 : à /user/hand/right
};

struct ActionSpaceInfo {
	XrSession session;
	XrAction action;
	std::string subPath;   // sous-chemin donné à la création de l'espace (souvent vide avec Unreal)
	int hand;              // 0 gauche, 1 droite, -1 inconnue
	std::string path;      // libellé affiché
	bool logged = false;
};

struct StreamState {
	PoseFilter filter;
	int hand = -1;
	std::string path;
	double statT0 = 0;
	uint64_t statCorr0 = 0;
	double corrPerSec = 0;
	// Bilan périodique pour le journal
	double logT0 = 0;
	uint64_t frames = 0, poseOk = 0, velOk = 0;
	uint64_t logCorr0 = 0, logTp0 = 0;
	int lastVelState = -1;
	int lastTrackedState = -1;   // bit XR_SPACE_LOCATION_POSITION_TRACKED_BIT
	double untrackedSince = 0;
	uint64_t posTracked = 0;
	// Pertes signalées par le service Pimax
	LossGuard guard;
	OrientSmoother orient;           // stabilisation de l'orientation (visée)
	int lastGuardPhase = 0;
	int trackState = 0;
	double guardPhaseSince = 0;
	bool lastCoupled = false;
	XrSpace partner = XR_NULL_HANDLE;   // espace de l'autre main, pour lier les deux mains
	bool partnerResolved = false;
	// Perte en cours : où était la main (vue du casque) au début de la perte
	double lossAz = 0, lossEl = 0, lossDist = 0, lossWall = 0;
	bool lossReinit = false, lossCoupled = false, lossGrips = false;
	// Carte des pertes : temps passé (s) et pertes par zone autour du casque
	struct Zones {
		double expEl[LossZones::kEl] = {}, expAz[LossZones::kAz] = {}, expD[LossZones::kDist] = {};
		uint64_t nEl[LossZones::kEl] = {}, nAz[LossZones::kAz] = {}, nD[LossZones::kDist] = {};
		void exposure(double az, double el, double d, double dt) {
			expEl[LossZones::El(el)] += dt;
			expAz[LossZones::Az(az)] += dt;
			expD[LossZones::Dist(d)] += dt;
		}
		void loss(double az, double el, double d) {
			nEl[LossZones::El(el)]++;
			nAz[LossZones::Az(az)]++;
			nD[LossZones::Dist(d)]++;
		}
	} sessionZones;
	// Bilan par période de réglages (remis à zéro à chaque changement de réglages)
	struct Period {
		uint64_t frames = 0;
		uint64_t movN = 0, stillN = 0, losses = 0, holds = 0, gapN = 0;
		double movRaw = 0, movOut = 0, stillRaw = 0, stillOut = 0, gapSum = 0;
		Zones zones;
	} period;
	int hist = 0;                       // nombre d'images valides consécutives mémorisées (0 à 2)
	int lastPeriodPhase = 0;
	double lastStatT = 0;               // instant de la dernière image comptée (le jeu demande souvent 2 fois le même instant)
	V3 rawP1, rawP2, outP1, outP2;      // positions des deux images précédentes
	Q rawQ1, rawQ2, outQ1, outQ2;
	// Cinq dernières images (pour les à-coups, mesurés sur l'image du milieu)
	V3 winRaw[5], winOut[5];
	double winSpeed[5] = {};
	bool winOk[5] = {};
	int winN = 0;
};

std::mutex g_mutex;
std::map<XrSpace, ActionSpaceInfo> g_actionSpaces;
std::map<XrAction, ActionInfo> g_actions;
std::map<std::pair<XrSpace, XrSpace>, StreamState> g_streams; // (espace manette, espace de base)
std::map<XrSession, XrSpace> g_viewSpaces;                    // espace VIEW (casque) créé par la couche, par session

SharedMemory g_shared;
bool g_sharedOk = false;
FilterSettings g_settings;
int32_t g_preset = PresetMedium;
double g_lastSettingsRead = 0;

std::string PathToString(XrPath path) {
	if (path == XR_NULL_PATH || !g_xrPathToString) return "";
	char buf[XR_MAX_PATH_LENGTH] = {};
	uint32_t n = 0;
	if (XR_FAILED(g_xrPathToString(g_instance, path, sizeof(buf), &n, buf))) return "";
	return buf;
}

int HandFromPath(const std::string& p) {
	if (p.rfind("/user/hand/left", 0) == 0) return 0;
	if (p.rfind("/user/hand/right", 0) == 0) return 1;
	return -1;
}

bool ContainsNoCase(std::string s, const char* what) {
	for (auto& c : s) c = (char)tolower((unsigned char)c);
	return s.find(what) != std::string::npos;
}

// Détermine la main d'un espace de pose, dans l'ordre : sous-chemin de l'espace, sous-chemin unique de
// l'action, liaisons suggérées (une seule main), nom de l'action. Appelé sous g_mutex.
void ResolveHand(ActionSpaceInfo& info) {
	if (info.hand >= 0) return;
	int hand = HandFromPath(info.subPath);
	std::string how = "sous-chemin de l'espace";
	auto it = g_actions.find(info.action);
	if (hand < 0 && it != g_actions.end()) {
		const ActionInfo& a = it->second;
		if (a.subPaths.size() == 1) { hand = HandFromPath(a.subPaths[0]); how = "sous-chemin de l'action"; }
		if (hand < 0 && (a.boundHands == 1 || a.boundHands == 2)) { hand = a.boundHands == 1 ? 0 : 1; how = "liaisons"; }
		if (hand < 0) {
			const bool l = ContainsNoCase(a.name, "left"), r = ContainsNoCase(a.name, "right");
			if (l != r) { hand = l ? 0 : 1; how = "nom de l'action"; }
		}
	}
	if (hand >= 0) {
		info.hand = hand;
		info.path = hand == 0 ? "/user/hand/left" : "/user/hand/right";
		Log("  -> main %s (via %s)\n", hand == 0 ? "gauche" : "droite", how.c_str());
	}
}

// Pertes par minute passée dans chaque zone (zones fréquentées au moins 5 s).
std::string ZonesText(const StreamState::Zones& z) {
	std::string out;
	auto part = [&](const char* title, const double* exp, const uint64_t* n, int count, const char* (*name)(int)) {
		std::string p;
		for (int i = 0; i < count; i++) {
			if (exp[i] < 5.0) continue;
			char b[160];
			snprintf(b, sizeof(b), "%s%s %.1f/min (%llu en %.0f s)", p.empty() ? "" : ", ", name(i), n[i] / exp[i] * 60.0, (unsigned long long)n[i], exp[i]);
			p += b;
		}
		if (!p.empty()) out += std::string("\n        ") + title + " : " + p;
	};
	part("hauteur", z.expEl, z.nEl, LossZones::kEl, LossZones::ElName);
	part("côté", z.expAz, z.nAz, LossZones::kAz, LossZones::AzName);
	part("distance", z.expD, z.nD, LossZones::kDist, LossZones::DistName);
	return out;
}

// Bouton grip serré (lu au passage quand le jeu le demande ; au plus 0,5 s d'âge). Appelé sous g_mutex.
bool GripHeld(int hand) {
	const GripState& g = g_grip[hand];
	return g.seen && NowSeconds() - g.t < 0.5 && g.value > 0.5;
}

// Journal des pertes, une ligne par perte (pour tracer la carte des pertes) :
// %LOCALAPPDATA%\SteadyGrip\SteadyGrip_pertes.csv. Appelé sous g_mutex.
void WriteLossCsv(const StreamState& st, double durMs, double gapCm) {
	static double lastWall[2] = { 0, 0 };
	if (st.hand < 0 || st.hand > 1) return;
	// Le jeu localise souvent deux espaces par main (grip et visée) : une seule ligne par perte.
	if (std::fabs(st.lossWall - lastWall[st.hand]) < 0.05) return;
	lastWall[st.hand] = st.lossWall;
	static FILE* f = [] {
		FILE* file = nullptr;
		const std::wstring path = DataDir() + L"\\SteadyGrip_pertes.csv";
		_wfopen_s(&file, path.c_str(), L"ab");
		if (file) {
			fseek(file, 0, SEEK_END);
			if (ftell(file) == 0)
				fputs("date;jeu;main;azimut_deg;elevation_deg;distance_cm;duree_ms;reinitialisation;ecart_reprise_cm;deux_grips;liee_autre_main\r\n", file);
		}
		return file;
	}();
	if (!f) return;
	SYSTEMTIME t;
	GetLocalTime(&t);
	fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u;%s;%s;%.0f;%.0f;%.0f;%.0f;%d;%.0f;%d;%d\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
		g_appName.c_str(), st.hand == 0 ? "gauche" : "droite", st.lossAz, st.lossEl, st.lossDist * 100.0, durMs, st.lossReinit ? 1 : 0, gapCm,
		st.lossGrips ? 1 : 0, st.lossCoupled ? 1 : 0);
	fflush(f);
}

std::string SettingsDesc(const FilterSettings& s, int32_t preset) {
	static const char* presets[] = { "Désactivé", "Léger", "Moyen", "Fort", "Personnalisé" };
	static const char* couple[] = { "automatique", "toujours", "jamais", "deux grips serrés" };
	char grip[32] = "non";
	if (s.gripFix) snprintf(grip, sizeof(grip), "%.0f ms", s.gripHoldMs);
	char b[480];
	snprintf(b, sizeof(b),
		"préréglage %s, actif %d, lissage à l'arrêt %.0f ms, en mouvement %.0f ms, téléportations %.0f ms, orientation %.0f, "
		"contenir les pertes %d, modèle de bras %d, dérive max %.0f cm, portée %.0f cm, liaison des mains %s, correction du grip %s",
		preset >= 0 && preset <= 4 ? presets[preset] : "?", s.enabled ? 1 : 0, s.stillTauMs, s.movingTauMs, s.teleportBlendMs, s.orientSmoothing,
		s.lossGuard ? 1 : 0, s.armModel ? 1 : 0, s.maxLostDriftM * 100, s.maxReachM * 100,
		s.coupleMode >= 0 && s.coupleMode <= 3 ? couple[s.coupleMode] : "?", grip);
	return b;
}

double g_periodStart = 0;
std::string g_periodDesc;
bool g_changePending = false;
double g_lastChange = 0;

// Bilan de la période qui se termine, pour la manette la plus utilisée de chaque main. Appelé sous g_mutex.
void EndPeriod(const char* why) {
	const double now = NowSeconds();
	const double dur = g_periodStart ? now - g_periodStart : 0;
	if (dur >= 5.0) {
		Log("=== Bilan sur %.0f s (%s) - réglages : %s\n", dur, why, g_periodDesc.c_str());
		for (int hand = 0; hand < 2; hand++) {
			const StreamState* best = nullptr;
			for (const auto& [key, st] : g_streams)
				if (st.hand == hand && (!best || st.period.frames > best->period.frames)) best = &st;
			if (!best || best->period.frames == 0) continue;
			const auto& p = best->period;
			Log("    [%s] à-coups en mouvement : %.2f mm/image (Pimax seul %.2f) ; tremblement de visée : %.3f°/image (Pimax seul %.3f) ; "
				"%llu pertes, %llu réinitialisations, écart moyen à la reprise %.0f cm\n",
				hand == 0 ? "gauche" : "droite", p.movN ? p.movOut / p.movN : 0.0, p.movN ? p.movRaw / p.movN : 0.0,
				p.stillN ? p.stillOut / p.stillN : 0.0, p.stillN ? p.stillRaw / p.stillN : 0.0, (unsigned long long)p.losses,
				(unsigned long long)p.holds, p.gapN ? p.gapSum / p.gapN * 100.0 : 0.0);
			const std::string zones = ZonesText(p.zones);
			if (!zones.empty()) Log("    [%s] carte des pertes (pertes par minute passée dans la zone) :%s\n", hand == 0 ? "gauche" : "droite", zones.c_str());
		}
		if (g_settings.gripFix) {
			static const char* names[] = { "gauche", "droite", "main non précisée" };
			for (int i = 0; i < 3; i++) {
				const auto& gs = g_gripStats[i];
				if (!gs.masked && !gs.shortReleases) continue;
				Log("    [%s] grip : %llu relâchements parasites masqués (%.1f par minute, le plus long %.0f ms) ; "
					"%llu relâchements suivis d'un nouvel appui en moins de 0,4 s\n", names[i], (unsigned long long)gs.masked,
					gs.masked * 60.0 / dur, gs.maskedMaxMs, (unsigned long long)gs.shortReleases);
			}
		}
	}
	// Fin de session ou de partie : carte des pertes de toute la session.
	if (std::strcmp(why, "changement de réglages") != 0) {
		for (int hand = 0; hand < 2; hand++) {
			const StreamState* best = nullptr;
			double bestT = 0;
			for (const auto& [key, st] : g_streams) {
				double t = 0;
				for (double e : st.sessionZones.expEl) t += e;
				if (st.hand == hand && t > bestT) { best = &st; bestT = t; }
			}
			if (!best || bestT < 30) continue;
			Log("=== Carte des pertes de la session, main %s (%.0f s suivies, pertes par minute passée dans la zone) :%s\n",
				hand == 0 ? "gauche" : "droite", bestT, ZonesText(best->sessionZones).c_str());
		}
		for (auto& [key, st] : g_streams) st.sessionZones = StreamState::Zones{}; // notée une seule fois
	}
	for (auto& [key, st] : g_streams) st.period = StreamState::Period{};
	for (auto& gs : g_gripStats) gs = GripFilter::Stats{};
	g_periodStart = now;
	g_periodDesc = SettingsDesc(g_settings, g_preset);
}

bool SameSettings(const FilterSettings& a, const FilterSettings& b) { return std::memcmp(&a, &b, sizeof(FilterSettings)) == 0; }

void RefreshSettings() {
	const double now = NowSeconds();
	if (now - g_lastSettingsRead < 0.1) return;
	g_lastSettingsRead = now;
	if (g_sharedOk) {
		FilterSettings s;
		int32_t p;
		if (g_shared.readSettings(s, p)) {
			if (!SameSettings(s, g_settings) || p != g_preset) {
				if (!g_changePending) EndPeriod("changement de réglages"); // bilan des réglages précédents
				g_changePending = true;
				g_lastChange = now;
			}
			g_settings = s;
			g_preset = p;
		}
		// Réglages stables depuis 1 s (fin d'un déplacement de curseur) : on note les nouveaux réglages.
		if (g_changePending && now - g_lastChange > 1.0) {
			g_changePending = false;
			for (auto& [key, st] : g_streams) st.period = StreamState::Period{};
			g_periodStart = now;
			g_periodDesc = SettingsDesc(g_settings, g_preset);
			Log("Réglages modifiés : %s\n", g_periodDesc.c_str());
		}
		g_shared.block()->heartbeat = now;
		g_shared.block()->pid = GetCurrentProcessId();
	}
}

void PublishStats(StreamState& st) {
	const double now = NowSeconds();
	if (st.statT0 == 0) {
		st.statT0 = now;
		st.statCorr0 = st.filter.stats().corrections;
	} else if (now - st.statT0 >= 1.0) {
		st.corrPerSec = (st.filter.stats().corrections - st.statCorr0) / (now - st.statT0);
		st.statT0 = now;
		st.statCorr0 = st.filter.stats().corrections;
	}
	if (!g_sharedOk || st.hand < 0) return;
	SharedHand& h = g_shared.block()->hands[st.hand];
	const FilterStats& fs = st.filter.stats();
	strncpy_s(h.path, st.path.c_str(), _TRUNCATE);
	h.corrPerSec = st.corrPerSec;
	h.lastCorrMm = fs.lastCorrMm;
	h.offsetMm = fs.offsetMm;
	h.offsetDeg = fs.offsetDeg;
	h.teleports = fs.teleports;
	h.outOfView = fs.outOfView ? 1 : 0;
	h.velocityMissing = fs.velocityMissing ? 1 : 0;
	h.trackState = st.trackState;
	h.guardPhase = (int32_t)st.guard.phase();
	h.coupled = st.guard.coupled() ? 1 : 0;
	h.holds = st.guard.holds();
	h.gapAvgCm = st.guard.gapCount() ? st.guard.gapSumM() / st.guard.gapCount() * 100.0 : 0.0;
	h.lastGapCm = st.guard.lastReturnGapM() * 100.0;
	h.gripHeld = g_grip[st.hand].seen ? (GripHeld(st.hand) ? 1 : 0) : -1;
	h.tremorRaw = st.period.stillN ? st.period.stillRaw / st.period.stillN : 0.0;
	h.tremorOut = st.period.stillN ? st.period.stillOut / st.period.stillN : 0.0;
	h.caught = st.guard.gapCount();
	h.lastUpdate = now;
}

// État de suivi Pimax de la main, lu dans la mémoire partagée (application compagnon).
int PimaxState(int hand) {
	if (!g_sharedOk || hand < 0 || hand > 1) return TrackUnknown;
	const SharedBlock* b = g_shared.block();
	if (b->trackSource != SourceActive || NowSeconds() - b->trackHeartbeat > 0.5) return TrackUnknown;
	return (int)b->track[hand].state;
}

// Pose du casque dans l'espace de base, au même instant. Appelé sous g_mutex.
bool HeadPose(XrSession session, XrSpace baseSpace, XrTime time, V3& hp, Q& hq) {
	if (!g_xrCreateReferenceSpace) return false;
	// Plusieurs manettes sont localisées au même instant : une seule localisation du casque suffit.
	static XrSession cS = XR_NULL_HANDLE;
	static XrSpace cB = XR_NULL_HANDLE;
	static XrTime cT = 0;
	static bool cOk = false;
	static V3 cP;
	static Q cQ;
	if (session == cS && baseSpace == cB && time == cT) {
		hp = cP;
		hq = cQ;
		return cOk;
	}
	cS = session;
	cB = baseSpace;
	cT = time;
	cOk = false;
	auto it = g_viewSpaces.find(session);
	if (it == g_viewSpaces.end()) {
		XrReferenceSpaceCreateInfo ci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
		ci.poseInReferenceSpace.orientation.w = 1;
		XrSpace view = XR_NULL_HANDLE;
		if (XR_FAILED(g_xrCreateReferenceSpace(session, &ci, &view))) view = XR_NULL_HANDLE;
		it = g_viewSpaces.emplace(session, view).first;
		Log("Espace du casque pour la session %p : %s\n", (void*)session, view ? "créé" : "ÉCHEC");
	}
	if (!it->second) return false;
	XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
	if (XR_FAILED(g_xrLocateSpace(it->second, baseSpace, time, &loc))) return false;
	if (!(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) || !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) return false;
	hp = cP = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
	hq = cQ = { loc.pose.orientation.w, loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z };
	cOk = true;
	return true;
}

std::string Lower(std::string s) {
	for (auto& c : s) c = (char)tolower((unsigned char)c);
	return s;
}

// Espace de l'autre main correspondant à celui-ci (même type de pose si possible : « left-...-grip »
// <-> « right-...-grip »), dans la même session. Appelé sous g_mutex.
XrSpace FindPartner(XrSpace space, const ActionSpaceInfo& asi) {
	std::string want;
	auto a = g_actions.find(asi.action);
	if (a != g_actions.end()) {
		want = Lower(a->second.name);
		const size_t l = want.find("left"), r = want.find("right");
		if (l != std::string::npos) want.replace(l, 4, "right");
		else if (r != std::string::npos) want.replace(r, 5, "left");
		else want.clear();
	}
	XrSpace any = XR_NULL_HANDLE;
	for (const auto& [sp, info] : g_actionSpaces) {
		if (sp == space || info.session != asi.session || info.hand != 1 - asi.hand) continue;
		auto b = g_actions.find(info.action);
		if (!want.empty() && b != g_actions.end() && Lower(b->second.name) == want) return sp;
		if (!any && info.action == asi.action) any = sp;   // même action, autre sous-chemin
		if (!any) any = sp;
	}
	return any;
}

// Pose brute de l'autre main, si Pimax la suit. Appelé sous g_mutex.
bool PartnerPose(StreamState& st, XrSpace space, const ActionSpaceInfo& asi, XrSpace baseSpace, XrTime time, V3& pp, Q& pq) {
	if (PimaxState(1 - st.hand) != TrackTracking) return false;
	if (!st.partnerResolved || !g_actionSpaces.count(st.partner)) {
		st.partner = FindPartner(space, asi);
		st.partnerResolved = true;
	}
	if (!st.partner) return false;
	// Cache : une même pose n'est demandée qu'une fois par instant.
	struct Cached { bool ok; V3 p; Q q; };
	static XrTime cacheTime = 0;
	static std::map<std::pair<XrSpace, XrSpace>, Cached> cache;
	if (time != cacheTime) {
		cache.clear();
		cacheTime = time;
	}
	const auto key = std::make_pair(st.partner, baseSpace);
	auto c = cache.find(key);
	if (c == cache.end()) {
		Cached v{ false, {}, {} };
		XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
		if (XR_SUCCEEDED(g_xrLocateSpace(st.partner, baseSpace, time, &loc)) && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
			(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
			v.ok = true;
			v.p = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
			v.q = { loc.pose.orientation.w, loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z };
		}
		c = cache.emplace(key, v).first;
	}
	pp = c->second.p;
	pq = c->second.q;
	return c->second.ok;
}

// Filtre une pose localisée. Appelé sous g_mutex.
void FilterPose(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocationFlags flags, XrPosef& pose,
	const XrSpaceVelocityFlags* velFlags, const XrVector3f* linVel, const XrVector3f* angVel) {
	auto it = g_actionSpaces.find(space);
	if (it == g_actionSpaces.end()) return;                 // pas une manette
	if (g_actionSpaces.count(baseSpace)) return;            // pose relative à une autre manette : on ne touche pas

	ActionSpaceInfo& asi = it->second;
	ResolveHand(asi);
	// Seules les poses de manettes (main gauche ou droite identifiée) sont filtrées : jamais le regard
	// (eye tracking) ni les autres poses que le jeu peut déclarer.
	if (asi.hand < 0) return;
	StreamState& st = g_streams[{ space, baseSpace }];
	st.hand = asi.hand;
	st.path = asi.path;
	if (!asi.logged) {
		asi.logged = true;
		Log("Première localisation de l'espace %p (main %d) : pose %s, vitesses %s\n", (void*)space, asi.hand,
			(flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ? "valide" : "invalide",
			(velFlags && (*velFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)) ? "fournies" : "ABSENTES");
	}

	PoseIn in;
	in.t = (double)time * 1e-9;
	in.poseValid = (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
	in.p = { pose.position.x, pose.position.y, pose.position.z };
	in.q = { pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z };
	in.velValid = velFlags && (*velFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) && (*velFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT);
	if (in.velValid) {
		in.v = { linVel->x, linVel->y, linVel->z };
		in.w = { angVel->x, angVel->y, angVel->z };
	}
	// Journal : changements de disponibilité des vitesses, et bilan toutes les 10 s.
	st.frames++;
	if (in.poseValid) st.poseOk++;
	if (in.poseValid && in.velValid) st.velOk++;
	// Le runtime signale-t-il qu'il ne suit plus la position (extrapolation IMU seule) ?
	// Si oui, ce bit permettra de détecter les pertes de suivi optique de façon fiable.
	const int tracked = (flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) ? 1 : 0;
	if (tracked) st.posTracked++;
	if (in.poseValid && tracked != st.lastTrackedState) {
		const double nowT = NowSeconds();
		if (st.lastTrackedState == 0 && tracked == 1)
			Log("[%s] espace %p : position de nouveau SUIVIE après %.0f ms\n", st.hand == 0 ? "gauche" : st.hand == 1 ? "droite" : "?",
				(void*)space, (nowT - st.untrackedSince) * 1000.0);
		else if (tracked == 0)
			Log("[%s] espace %p : position NON SUIVIE (bit TRACKED absent)\n", st.hand == 0 ? "gauche" : st.hand == 1 ? "droite" : "?", (void*)space);
		if (tracked == 0) st.untrackedSince = nowT;
		st.lastTrackedState = tracked;
	}
	if (in.poseValid && (int)in.velValid != st.lastVelState) {
		Log("Espace %p (main %d) : vitesses %s\n", (void*)space, st.hand, in.velValid ? "fournies par le runtime" : "ABSENTES -> estimation par la couche");
		st.lastVelState = in.velValid;
	}
	const double now = NowSeconds();
	if (st.logT0 == 0) st.logT0 = now;
	if (now - st.logT0 >= 10.0) {
		const FilterStats& fs = st.filter.stats();
		Log("[%s] espace %p : %llu images, pose valide %.0f %%, position suivie %.0f %%, vitesses %.0f %%, %.1f recalages/s, %llu téléportations, écart %.2f mm\n",
			st.hand == 0 ? "gauche" : st.hand == 1 ? "droite" : "?", (void*)space, (unsigned long long)st.frames,
			st.frames ? 100.0 * st.poseOk / st.frames : 0.0, st.frames ? 100.0 * st.posTracked / st.frames : 0.0,
			st.poseOk ? 100.0 * st.velOk / st.poseOk : 0.0,
			(fs.corrections - st.logCorr0) / (now - st.logT0), (unsigned long long)(fs.teleports - st.logTp0), fs.offsetMm);
		st.logT0 = now;
		st.logCorr0 = fs.corrections;
		st.logTp0 = fs.teleports;
		st.frames = st.poseOk = st.velOk = st.posTracked = 0;
	}

	V3 p;
	Q q;
	st.filter.process(in, g_settings, p, q);
	if (in.poseValid) {
		// Stabilisation de l'orientation : très lissée quand l'arme est presque immobile, pas en mouvement rapide.
		q = st.orient.apply(in.t, q, g_settings.enabled ? g_settings.orientSmoothing : 0.0);
		// Perte de suivi signalée par Pimax : on contient la position (voir LossGuard).
		st.trackState = PimaxState(st.hand);
		V3 hp;
		Q hq;
		const bool headOk = st.trackState != TrackUnknown && g_settings.lossGuard && HeadPose(asi.session, baseSpace, time, hp, hq);
		// Pose de l'autre main : pendant une perte (pour lier les mains), et en mode automatique aussi le reste
		// du temps (pour savoir si les deux mains se déplacent ensemble).
		V3 pp;
		Q pq;
		const bool lossNow = headOk && (st.trackState == TrackLost || st.trackState == TrackIniting);
		const bool wantPartner = headOk && g_settings.coupleMode != CoupleNever && (lossNow || g_settings.coupleMode == CoupleAuto);
		const bool partnerOk = wantPartner && PartnerPose(st, space, asi, baseSpace, time, pp, pq);
		const bool gripsHeld = GripHeld(0) && GripHeld(1);
		p = st.guard.apply(in.t, st.trackState, p, q, in.p, headOk, hp, hq, partnerOk, pp, pq, g_settings, gripsHeld);
		const int phase = (int)st.guard.phase();
		const bool coupled = st.guard.coupled();
		// Carte des pertes : position de la main vue du casque au début de chaque perte, et temps passé par zone.
		const bool lossStart = (phase == 1 || phase == 2) && (st.lastGuardPhase == 0 || st.lastGuardPhase == 3);
		if (lossStart) {
			HeadAngles(hp, hq, in.p, st.lossAz, st.lossEl, st.lossDist);
			st.lossWall = NowSeconds();
			st.lossReinit = false;
			st.lossCoupled = false;
			st.lossGrips = gripsHeld;
			st.period.zones.loss(st.lossAz, st.lossEl, st.lossDist);
			st.sessionZones.loss(st.lossAz, st.lossEl, st.lossDist);
		}
		if (phase == 2) st.lossReinit = true;
		if (coupled) st.lossCoupled = true;
		if (headOk && phase == 0 && st.trackState == TrackTracking && in.t > st.lastStatT && st.lastStatT > 0) {
			double az, el, d;
			HeadAngles(hp, hq, in.p, az, el, d);
			const double dt = std::min(0.1, in.t - st.lastStatT);
			st.period.zones.exposure(az, el, d, dt);
			st.sessionZones.exposure(az, el, d, dt);
		}
		if (phase == 3 && (st.lastGuardPhase == 1 || st.lastGuardPhase == 2))
			WriteLossCsv(st, (NowSeconds() - st.lossWall) * 1000.0, st.guard.lastReturnGapM() * 100.0);
		if (phase != st.lastGuardPhase || coupled != st.lastCoupled) {
			static const char* names[] = { "normal", "perte : dérive contenue", "réinitialisation Pimax : modèle de bras", "reprise : retour progressif" };
			const double nowT = NowSeconds();
			char extra[224] = "";
			if (coupled) snprintf(extra, sizeof(extra), ", liée à l'autre main");
			else if (phase == 2 && !g_settings.armModel) snprintf(extra, sizeof(extra), " (désactivé : arme figée)");
			else if (phase == 3) snprintf(extra, sizeof(extra), ", écart à la reprise %.0f cm, retour en %.0f ms", st.guard.lastReturnGapM() * 100.0, st.guard.lastReturnMs());
			if (lossStart)
				snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " ; main à %.0f° %s, %.0f° %s du regard, %.0f cm", std::fabs(st.lossAz),
					st.lossAz >= 0 ? "à droite" : "à gauche", std::fabs(st.lossEl), st.lossEl >= 0 ? "au-dessus" : "sous", st.lossDist * 100.0);
			Log("[%s] espace %p : %s%s (phase précédente %.0f ms)\n", st.hand == 0 ? "gauche" : "droite", (void*)space, names[phase], extra,
				st.guardPhaseSince ? (nowT - st.guardPhaseSince) * 1000.0 : 0.0);
			if (lossStart && g_settings.coupleMode == CoupleGrips)
				Log("[%s] espace %p :   décision : %s\n", st.hand == 0 ? "gauche" : "droite", (void*)space,
					!g_grip[0].seen || !g_grip[1].seen ? "boutons grip non lus dans ce jeu, main indépendante"
					: coupled ? "deux grips serrés, main liée à l'autre main"
					: gripsHeld ? "deux grips serrés mais mains trop proches ou trop éloignées (ou autre main perdue), main indépendante"
					: "un grip relâché, main indépendante");
			if (lossStart && g_settings.coupleMode == CoupleAuto)
				Log("[%s] espace %p :   décision : mains %s (%d mesures%s, variation max %.1f cm, écart moyen %.0f cm)\n",
					st.hand == 0 ? "gauche" : "droite", (void*)space, st.guard.lastRigid() ? "SOLIDAIRES" : "indépendantes",
					st.guard.diagSamples(), st.guard.diagReused() ? ", décision précédente reprise" : "",
					st.guard.diagDeviation() * 100.0, st.guard.diagDistance() * 100.0);
			st.lastGuardPhase = phase;
			st.lastCoupled = coupled;
			st.guardPhaseSince = nowT;
		}
		pose.position = { (float)p.x, (float)p.y, (float)p.z };
		pose.orientation = { (float)q.x, (float)q.y, (float)q.z, (float)q.w };
		// Bilan de période : à-coups (variation du pas d'une image à l'autre) quand la manette bouge,
		// tremblement d'orientation quand elle est presque immobile ; brut (Pimax seul) et après SteadyGrip.
		auto& per = st.period;
		if (in.t > st.lastStatT) per.frames++;
		if (phase == 1 && (st.lastPeriodPhase == 0 || st.lastPeriodPhase == 3)) per.losses++;
		if (phase == 2 && st.lastPeriodPhase != 2) per.holds++;
		if (phase == 3 && st.lastPeriodPhase != 3) { per.gapSum += st.guard.lastReturnGapM(); per.gapN++; }
		st.lastPeriodPhase = phase;
		const bool newFrame = in.t > st.lastStatT;
		if (newFrame) st.lastStatT = in.t;
		if (newFrame && st.hist >= 2 && st.trackState != TrackLost && st.trackState != TrackIniting && phase == 0) {
			const double speed = in.velValid ? in.v.norm() : (in.p - st.rawP1).norm() * 72.0;
			auto stepRot = [](const Q& a, const Q& b) { Q d = a * b.conj(); if (d.w < 0) d = Q{ -d.w, -d.x, -d.y, -d.z }; return qlog(d); };
			const V3 r1 = stepRot(in.q, st.rawQ1), r0 = stepRot(st.rawQ1, st.rawQ2);
			const double angSpeed = in.velValid ? in.w.norm() : r1.norm() * 72.0;
			if (angSpeed < 0.15 && speed < 0.15) {
				per.stillRaw += (r1 - r0).norm() * 57.2958;
				per.stillOut += (stepRot(q, st.outQ1) - stepRot(st.outQ1, st.outQ2)).norm() * 57.2958;
				per.stillN++;
			}
		}
		if (newFrame) {
			// À-coups : écart entre la position d'une image et un lissage polynomial de ses voisines
			// (Savitzky-Golay, 5 images, exact pour un mouvement à accélération constante). Un mouvement régulier,
			// même rapide ou qui accélère, donne zéro ; seuls les sauts d'une image à l'autre comptent.
			for (int k = 0; k < 4; k++) {
				st.winRaw[k] = st.winRaw[k + 1]; st.winOut[k] = st.winOut[k + 1];
				st.winSpeed[k] = st.winSpeed[k + 1]; st.winOk[k] = st.winOk[k + 1];
			}
			st.winRaw[4] = in.p;
			st.winOut[4] = p;
			st.winSpeed[4] = in.velValid ? in.v.norm() : (st.hist >= 1 ? (in.p - st.rawP1).norm() * 72.0 : 0.0);
			st.winOk[4] = st.trackState != TrackLost && st.trackState != TrackIniting && phase == 0;
			st.winN = std::min(st.winN + 1, 5);
			if (st.winN == 5 && st.winOk[0] && st.winOk[1] && st.winOk[2] && st.winOk[3] && st.winOk[4] && st.winSpeed[2] > 0.15 && st.winSpeed[2] < 1.5) {
				auto residual = [](const V3* w) {
					const V3 fit = (w[0] * -3.0 + w[1] * 12.0 + w[2] * 17.0 + w[3] * 12.0 + w[4] * -3.0) * (1.0 / 35.0);
					return (w[2] - fit).norm() * 1000.0;
				};
				per.movRaw += residual(st.winRaw);
				per.movOut += residual(st.winOut);
				per.movN++;
			}
			st.rawP2 = st.rawP1; st.rawP1 = in.p; st.outP2 = st.outP1; st.outP1 = p;
			st.rawQ2 = st.rawQ1; st.rawQ1 = in.q; st.outQ2 = st.outQ1; st.outQ1 = q;
			st.hist = std::min(st.hist + 1, 2);
		}
	} else {
		st.hist = 0;
		st.winN = 0;
	}
	PublishStats(st);
}

// --- Fonctions interceptées ----------------------------------------------------------------------
XrResult XRAPI_CALL SG_xrCreateAction(XrActionSet actionSet, const XrActionCreateInfo* createInfo, XrAction* action) {
	const XrResult r = g_xrCreateAction(actionSet, createInfo, action);
	if (XR_SUCCEEDED(r) && createInfo && action &&
		(createInfo->actionType == XR_ACTION_TYPE_FLOAT_INPUT || createInfo->actionType == XR_ACTION_TYPE_BOOLEAN_INPUT)) {
		std::lock_guard<std::mutex> lock(g_mutex);
		g_inputActions[*action] = InputAction{ createInfo->actionName, 0 };
	}
	if (XR_SUCCEEDED(r) && createInfo && action && createInfo->actionType == XR_ACTION_TYPE_POSE_INPUT) {
		ActionInfo a;
		a.name = createInfo->actionName;
		for (uint32_t i = 0; i < createInfo->countSubactionPaths; i++) a.subPaths.push_back(PathToString(createInfo->subactionPaths[i]));
		std::string subs;
		for (const auto& p : a.subPaths) subs += " " + p;
		Log("Action de pose créée : %s, sous-chemins :%s\n", a.name.c_str(), subs.empty() ? " (aucun)" : subs.c_str());
		std::lock_guard<std::mutex> lock(g_mutex);
		g_actions[*action] = a;
	}
	return r;
}

XrResult XRAPI_CALL SG_xrSuggestInteractionProfileBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding* sb) {
	if (sb && sb->suggestedBindings) {
		std::lock_guard<std::mutex> lock(g_mutex);
		for (uint32_t i = 0; i < sb->countSuggestedBindings; i++) {
			auto in = g_inputActions.find(sb->suggestedBindings[i].action);
			if (in != g_inputActions.end()) {
				const std::string path = PathToString(sb->suggestedBindings[i].binding);
				const int hand = HandFromPath(path);
				if (hand >= 0 && path.find("/input/squeeze") != std::string::npos && !(in->second.gripHands & (1 << hand))) {
					in->second.gripHands |= 1 << hand;
					Log("Bouton grip : action %s liée à %s\n", in->second.name.c_str(), path.c_str());
				}
			}
			auto it = g_actions.find(sb->suggestedBindings[i].action);
			if (it == g_actions.end()) continue;
			const int hand = HandFromPath(PathToString(sb->suggestedBindings[i].binding));
			if (hand >= 0) it->second.boundHands |= 1 << hand;
		}
	}
	return g_xrSuggestInteractionProfileBindings(instance, sb);
}

XrResult XRAPI_CALL SG_xrCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo* createInfo, XrSpace* space) {
	const XrResult r = g_xrCreateActionSpace(session, createInfo, space);
	if (XR_SUCCEEDED(r) && createInfo && space) {
		ActionSpaceInfo info{ session, createInfo->action, PathToString(createInfo->subactionPath), -1, "" };
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_actions.find(createInfo->action);
		Log("Espace d'action créé : %p, action %s, sous-chemin %s\n", (void*)*space,
			it != g_actions.end() ? it->second.name.c_str() : "?", info.subPath.empty() ? "(aucun)" : info.subPath.c_str());
		ResolveHand(info);
		g_actionSpaces[*space] = info;
	}
	return r;
}

// État d'un bouton grip demandé par le jeu : on le note au passage (lecture seule). Appelé sous g_mutex.
// Main d'une lecture de grip : par le sous-chemin demandé, sinon par la seule main liée à l'action ; -1 si inconnue.
int GripHand(const InputAction& a, XrPath subPath) {
	int hand = -1;
	if (subPath != XR_NULL_PATH) {
		auto c = g_pathHand.find(subPath);
		if (c == g_pathHand.end()) c = g_pathHand.emplace(subPath, HandFromPath(PathToString(subPath))).first;
		hand = c->second;
	} else if (a.gripHands == 1 || a.gripHands == 2) {
		hand = a.gripHands == 1 ? 0 : 1;
	}
	return hand >= 0 && (a.gripHands & (1 << hand)) ? hand : -1;
}

void RecordGrip(XrAction action, XrPath subPath, bool active, double value) {
	auto it = g_inputActions.find(action);
	if (it == g_inputActions.end() || !it->second.gripHands || !active) return;
	const int hand = GripHand(it->second, subPath);
	if (hand < 0) return; // action commune aux deux mains, demandée sans préciser la main : impossible de savoir laquelle
	GripState& g = g_grip[hand];
	if (!g.seen) Log("Bouton grip lu pendant le jeu : main %s (action %s)\n", hand == 0 ? "gauche" : "droite", it->second.name.c_str());
	// Plusieurs actions peuvent être liées au même grip : on garde la plus récente.
	g.value = value;
	g.t = NowSeconds();
	g.seen = true;
}

XrResult XRAPI_CALL SG_xrSyncActions(XrSession session, const XrActionsSyncInfo* syncInfo) {
	const XrResult r = g_xrSyncActions(session, syncInfo);
	if (XR_SUCCEEDED(r)) {
		std::lock_guard<std::mutex> lock(g_mutex);
		g_syncCount++;
	}
	return r;
}

// Correction du grip (si activée et si l'action est liée à un grip). Appelé sous g_mutex.
// Renvoie vrai si l'état rendu au jeu doit être remplacé par value / changed / lastChange.
bool FixGrip(XrAction action, XrPath subPath, bool active, double& value, XrBool32& changed, XrTime& lastChange) {
	if (!g_settings.gripFix) {
		if (!g_gripFix.empty()) g_gripFix.clear(); // désactivée en cours de partie : on repart de zéro
		return false;
	}
	auto it = g_inputActions.find(action);
	if (it == g_inputActions.end() || !it->second.gripHands) return false;
	GripFix& g = g_gripFix[{ action, subPath }];
	if (!active) {
		g.filter.reset();
		g.out = 0;
		return false;
	}
	if (!g_gripFixLogged) {
		g_gripFixLogged = true;
		Log("Correction du grip active (relâchements de moins de %.0f ms ignorés), action %s\n", g_settings.gripHoldMs, it->second.name.c_str());
	}
	if (g.evalSync != g_syncCount) {
		const double prev = g.out;
		const int hand = GripHand(it->second, subPath);
		g.out = g.filter.apply(NowSeconds(), value, g_settings.gripHoldMs, g_gripStats[hand < 0 ? 2 : hand]);
		g.changed = g.out != prev;
		if (lastChange > g.lastChange) g.lastChange = lastChange;
		g.evalSync = g_syncCount;
	}
	value = g.out;
	changed = g.changed ? XR_TRUE : XR_FALSE;
	lastChange = g.lastChange;
	return true;
}

XrResult XRAPI_CALL SG_xrGetActionStateFloat(XrSession session, const XrActionStateGetInfo* getInfo, XrActionStateFloat* state) {
	const XrResult r = g_xrGetActionStateFloat(session, getInfo, state);
	if (XR_SUCCEEDED(r) && getInfo && state) {
		std::lock_guard<std::mutex> lock(g_mutex);
		double v = state->currentState;
		XrBool32 changed = state->changedSinceLastSync;
		XrTime last = state->lastChangeTime;
		if (FixGrip(getInfo->action, getInfo->subactionPath, state->isActive != XR_FALSE, v, changed, last)) {
			state->currentState = (float)v;
			state->changedSinceLastSync = changed;
			state->lastChangeTime = last;
		}
		RecordGrip(getInfo->action, getInfo->subactionPath, state->isActive != XR_FALSE, state->currentState);
	}
	return r;
}

XrResult XRAPI_CALL SG_xrGetActionStateBoolean(XrSession session, const XrActionStateGetInfo* getInfo, XrActionStateBoolean* state) {
	const XrResult r = g_xrGetActionStateBoolean(session, getInfo, state);
	if (XR_SUCCEEDED(r) && getInfo && state) {
		std::lock_guard<std::mutex> lock(g_mutex);
		double v = state->currentState ? 1.0 : 0.0;
		XrBool32 changed = state->changedSinceLastSync;
		XrTime last = state->lastChangeTime;
		if (FixGrip(getInfo->action, getInfo->subactionPath, state->isActive != XR_FALSE, v, changed, last)) {
			state->currentState = v >= GripFilter::kPress ? XR_TRUE : XR_FALSE;
			state->changedSinceLastSync = changed;
			state->lastChangeTime = last;
		}
		RecordGrip(getInfo->action, getInfo->subactionPath, state->isActive != XR_FALSE, state->currentState ? 1.0 : 0.0);
	}
	return r;
}

void ForgetSpace(XrSpace space) {
	g_actionSpaces.erase(space);
	for (auto it = g_streams.begin(); it != g_streams.end();) {
		if (it->first.first == space || it->first.second == space) it = g_streams.erase(it);
		else ++it;
	}
}

XrResult XRAPI_CALL SG_xrDestroySpace(XrSpace space) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		// Unreal détruit les espaces (manettes et espace de base) avant la session : le bilan final doit être
		// fait avant qu'ils disparaissent (sinon il est vide).
		bool active = false;
		for (const auto& [key, st] : g_streams)
			if ((key.first == space || key.second == space) && st.period.frames > 0) active = true; // manette ou espace de base
		if (active) EndPeriod("fin de partie");
		ForgetSpace(space);
	}
	return g_xrDestroySpace(space);
}

XrResult XRAPI_CALL SG_xrDestroySession(XrSession session) {
	{
		// Les espaces d'une session sont détruits avec elle.
		std::lock_guard<std::mutex> lock(g_mutex);
		std::vector<XrSpace> toForget;
		for (const auto& [sp, info] : g_actionSpaces)
			if (info.session == session) toForget.push_back(sp);
		EndPeriod("fin de session"); // avant d'oublier les espaces : le bilan en a besoin
		for (XrSpace sp : toForget) ForgetSpace(sp);
		g_streams.clear();
		auto v = g_viewSpaces.find(session);
		if (v != g_viewSpaces.end()) {
			if (v->second) g_xrDestroySpace(v->second);
			g_viewSpaces.erase(v);
		}
	}
	return g_xrDestroySession(session);
}

XrResult XRAPI_CALL SG_xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) {
	bool target;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		target = g_actionSpaces.count(space) != 0 && g_actionSpaces.count(baseSpace) == 0;
	}
	if (!target || !location) return g_xrLocateSpace(space, baseSpace, time, location);

	// On a besoin des vitesses : si l'application ne les demande pas, on insère notre propre structure.
	XrSpaceVelocity* appVel = nullptr;
	for (auto* p = (XrBaseOutStructure*)location->next; p; p = p->next)
		if (p->type == XR_TYPE_SPACE_VELOCITY) appVel = (XrSpaceVelocity*)p;
	XrSpaceVelocity ownVel{ XR_TYPE_SPACE_VELOCITY };
	void* savedNext = location->next;
	if (!appVel) {
		ownVel.next = location->next;
		location->next = &ownVel;
	}
	const XrResult r = g_xrLocateSpace(space, baseSpace, time, location);
	if (!appVel) location->next = savedNext;
	if (XR_FAILED(r)) return r;

	const XrSpaceVelocity& v = appVel ? *appVel : ownVel;
	std::lock_guard<std::mutex> lock(g_mutex);
	RefreshSettings();
	FilterPose(space, baseSpace, time, location->locationFlags, location->pose, &v.velocityFlags, &v.linearVelocity, &v.angularVelocity);
	return r;
}

XrResult LocateSpacesCommon(PFN_xrLocateSpaces next, XrSession session, const XrSpacesLocateInfo* info, XrSpaceLocations* locs) {
	if (!info || !locs || !locs->locations) return next(session, info, locs);
	bool any = false;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_actionSpaces.count(info->baseSpace))
			for (uint32_t i = 0; i < info->spaceCount && !any; i++) any = g_actionSpaces.count(info->spaces[i]) != 0;
	}
	if (!any) return next(session, info, locs);

	XrSpaceVelocities* appVel = nullptr;
	for (auto* p = (XrBaseOutStructure*)locs->next; p; p = p->next)
		if (p->type == XR_TYPE_SPACE_VELOCITIES) appVel = (XrSpaceVelocities*)p;
	std::vector<XrSpaceVelocityData> ownData;
	XrSpaceVelocities ownVel{ XR_TYPE_SPACE_VELOCITIES };
	void* savedNext = locs->next;
	if (!appVel) {
		ownData.resize(locs->locationCount);
		ownVel.velocityCount = locs->locationCount;
		ownVel.velocities = ownData.data();
		ownVel.next = locs->next;
		locs->next = &ownVel;
	}
	const XrResult r = next(session, info, locs);
	if (!appVel) locs->next = savedNext;
	if (XR_FAILED(r)) return r;

	const XrSpaceVelocities& vel = appVel ? *appVel : ownVel;
	std::lock_guard<std::mutex> lock(g_mutex);
	RefreshSettings();
	const uint32_t n = std::min(info->spaceCount, locs->locationCount);
	for (uint32_t i = 0; i < n; i++) {
		const XrSpaceVelocityData* vd = (vel.velocities && i < vel.velocityCount) ? &vel.velocities[i] : nullptr;
		FilterPose(info->spaces[i], info->baseSpace, info->time, locs->locations[i].locationFlags, locs->locations[i].pose,
			vd ? &vd->velocityFlags : nullptr, vd ? &vd->linearVelocity : nullptr, vd ? &vd->angularVelocity : nullptr);
	}
	return r;
}

XrResult XRAPI_CALL SG_xrLocateSpaces(XrSession session, const XrSpacesLocateInfo* info, XrSpaceLocations* locs) {
	return LocateSpacesCommon(g_xrLocateSpaces, session, info, locs);
}

XrResult XRAPI_CALL SG_xrLocateSpacesKHR(XrSession session, const XrSpacesLocateInfo* info, XrSpaceLocations* locs) {
	return LocateSpacesCommon(g_xrLocateSpacesKHR, session, info, locs);
}

XrResult XRAPI_CALL SG_xrDestroyInstance(XrInstance instance) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		EndPeriod("fin de partie");
		g_actionSpaces.clear();
		g_actions.clear();
		g_inputActions.clear();
		g_pathHand.clear();
		for (auto& g : g_grip) g = GripState{};
		g_gripFix.clear();
		g_gripFixLogged = false;
		g_streams.clear();
		g_viewSpaces.clear();
	}
	Log("Instance détruite\n");
	const XrResult r = g_xrDestroyInstance(instance);
	g_instance = XR_NULL_HANDLE;
	return r;
}

XrResult XRAPI_CALL SG_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
	const XrResult r = g_next(instance, name, function);
	if (XR_FAILED(r) || !name || !function || !*function) return r;
	const std::string n = name;
	if (n == "xrGetInstanceProcAddr") *function = (PFN_xrVoidFunction)SG_xrGetInstanceProcAddr;
	else if (n == "xrDestroyInstance") *function = (PFN_xrVoidFunction)SG_xrDestroyInstance;
	else if (n == "xrDestroySession") *function = (PFN_xrVoidFunction)SG_xrDestroySession;
	else if (n == "xrCreateActionSpace") *function = (PFN_xrVoidFunction)SG_xrCreateActionSpace;
	else if (n == "xrCreateAction") *function = (PFN_xrVoidFunction)SG_xrCreateAction;
	else if (n == "xrSuggestInteractionProfileBindings") *function = (PFN_xrVoidFunction)SG_xrSuggestInteractionProfileBindings;
	else if (n == "xrDestroySpace") *function = (PFN_xrVoidFunction)SG_xrDestroySpace;
	else if (n == "xrLocateSpace") *function = (PFN_xrVoidFunction)SG_xrLocateSpace;
	else if (n == "xrGetActionStateFloat" && g_xrGetActionStateFloat) *function = (PFN_xrVoidFunction)SG_xrGetActionStateFloat;
	else if (n == "xrGetActionStateBoolean" && g_xrGetActionStateBoolean) *function = (PFN_xrVoidFunction)SG_xrGetActionStateBoolean;
	else if (n == "xrSyncActions" && g_xrSyncActions) *function = (PFN_xrVoidFunction)SG_xrSyncActions;
	else if (n == "xrLocateSpaces" && g_xrLocateSpaces) *function = (PFN_xrVoidFunction)SG_xrLocateSpaces;
	else if (n == "xrLocateSpacesKHR" && g_xrLocateSpacesKHR) *function = (PFN_xrVoidFunction)SG_xrLocateSpacesKHR;
	return r;
}

template <typename T>
void Resolve(XrInstance inst, const char* name, T& out) {
	PFN_xrVoidFunction f = nullptr;
	out = XR_SUCCEEDED(g_next(inst, name, &f)) ? (T)f : nullptr;
}

XrResult XRAPI_CALL SG_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layerInfo, XrInstance* instance) {
	if (!layerInfo || !layerInfo->nextInfo || std::strcmp(layerInfo->nextInfo->layerName, LAYER_NAME) != 0)
		return XR_ERROR_INITIALIZATION_FAILED;

	XrApiLayerCreateInfo chained = *layerInfo;
	chained.nextInfo = layerInfo->nextInfo->next;
	g_next = layerInfo->nextInfo->nextGetInstanceProcAddr;
	const XrResult r = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &chained, instance);
	if (XR_FAILED(r)) return r;
	g_instance = *instance;

	Resolve(g_instance, "xrDestroyInstance", g_xrDestroyInstance);
	Resolve(g_instance, "xrDestroySession", g_xrDestroySession);
	Resolve(g_instance, "xrCreateActionSpace", g_xrCreateActionSpace);
	Resolve(g_instance, "xrDestroySpace", g_xrDestroySpace);
	Resolve(g_instance, "xrLocateSpace", g_xrLocateSpace);
	Resolve(g_instance, "xrPathToString", g_xrPathToString);
	Resolve(g_instance, "xrCreateAction", g_xrCreateAction);
	Resolve(g_instance, "xrSuggestInteractionProfileBindings", g_xrSuggestInteractionProfileBindings);
	Resolve(g_instance, "xrLocateSpaces", g_xrLocateSpaces);
	Resolve(g_instance, "xrLocateSpacesKHR", g_xrLocateSpacesKHR);
	Resolve(g_instance, "xrCreateReferenceSpace", g_xrCreateReferenceSpace);
	Resolve(g_instance, "xrGetActionStateFloat", g_xrGetActionStateFloat);
	Resolve(g_instance, "xrGetActionStateBoolean", g_xrGetActionStateBoolean);
	Resolve(g_instance, "xrSyncActions", g_xrSyncActions);

	g_sharedOk = g_shared.open();
	const char* app = info && info->applicationInfo.applicationName[0] ? info->applicationInfo.applicationName : "?";
	g_appName = app;
	// Identité du jeu : son programme, et son numéro Steam s'il est installé par Steam.
	wchar_t exeW[MAX_PATH] = L"";
	GetModuleFileNameW(nullptr, exeW, MAX_PATH);
	const std::string exePath = Narrow(exeW);
	std::string exeName = exePath.substr(exePath.find_last_of("\\/") + 1);
	exeName = LowerAscii(exeName);
	const uint32_t steamId = SteamAppIdForExe(exePath);
	// Réglages de ce jeu (son profil s'il en a un, cherché par numéro Steam, programme, puis nom OpenXR).
	const std::wstring section = FindGameSection(steamId, exeName, app);
	int32_t p;
	LoadSettings(p, g_settings, section.empty() ? kGeneralSection : section.c_str());
	g_preset = p;
	if (g_sharedOk) {
		// Transmis à l'application compagnon, qui affiche le même jeu et les mêmes réglages.
		SharedBlock* b = g_shared.block();
		g_shared.writeSettings(g_settings, g_preset);
		strncpy_s(b->appName, app, _TRUNCATE);
		strncpy_s(b->exeName, exeName.c_str(), _TRUNCATE);
		strncpy_s(b->exePath, exePath.c_str(), _TRUNCATE);
		b->steamAppId = steamId;
		strncpy_s(b->profileSection, Narrow(section).c_str(), _TRUNCATE);
		b->pid = GetCurrentProcessId();
		ZeroMemory(b->hands, sizeof(b->hands));
	}
	Log("Jeu : %s, programme %s, numéro Steam %u\n", app, exeName.c_str(), steamId);
	Log("Profil : %s ; stabilisation de l'orientation %.0f, modèle de bras %d, dérive max %.0f cm, portée %.0f cm, liaison des mains %d\n",
		section.empty() ? "réglages généraux" : ("propre à ce jeu (" + Narrow(section) + ")").c_str(), g_settings.orientSmoothing, g_settings.armModel ? 1 : 0,
		g_settings.maxLostDriftM * 100, g_settings.maxReachM * 100, g_settings.coupleMode);
	g_periodStart = NowSeconds();
	g_periodDesc = SettingsDesc(g_settings, g_preset);
	if (g_sharedOk && g_shared.block()->pimaxVersion[0])
		Log("Pimax : %s%s\n", g_shared.block()->pimaxVersion,
			g_shared.block()->pimaxCheck == CheckVerifying ? " (nouvelle version : lecture en cours de vérification)" : "");
	Log("SteadyGrip %s chargé dans « %s » (moteur %s). Préréglage %d, actif=%d, mémoire partagée=%d, xrLocateSpaces=%d, contenir les pertes=%d, signal Pimax=%s\n",
		kVersion, info ? info->applicationInfo.applicationName : "?", info ? info->applicationInfo.engineName : "?",
		g_preset, g_settings.enabled ? 1 : 0, g_sharedOk ? 1 : 0, (g_xrLocateSpaces || g_xrLocateSpacesKHR) ? 1 : 0, g_settings.lossGuard ? 1 : 0,
		g_sharedOk && g_shared.block()->trackSource == SourceActive ? "actif" : "absent (application compagnon non lancée en administrateur ?)");
	return r;
}

} // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
	const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* request) {
	if (!loaderInfo || !request || (layerName && std::strcmp(layerName, LAYER_NAME) != 0)) return XR_ERROR_INITIALIZATION_FAILED;
	if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
		loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION || loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo))
		return XR_ERROR_INITIALIZATION_FAILED;
	if (request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
		request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION || request->structSize != sizeof(XrNegotiateApiLayerRequest))
		return XR_ERROR_INITIALIZATION_FAILED;
	if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
		loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
		return XR_ERROR_INITIALIZATION_FAILED;

	request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
	request->layerApiVersion = std::min<XrVersion>(XR_CURRENT_API_VERSION, loaderInfo->maxApiVersion);
	request->getInstanceProcAddr = SG_xrGetInstanceProcAddr;
	request->createApiLayerInstance = SG_xrCreateApiLayerInstance;
	return XR_SUCCESS;
}
