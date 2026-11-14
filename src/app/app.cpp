// SteadyGrip - application compagnon.
//
// L'interface est une page web (dossier « ui » à côté de l'application) affichée par WebView2, le moteur
// web de Windows. Cette partie C++ fait le reste : installation de la couche OpenXR, réglages (settings.ini),
// bibliothèque des jeux et jaquettes, lecture de l'état de suivi Pimax (droits administrateur, lecture
// seule), et dialogue avec la page par messages JSON.

#include "../common/Shared.h"
#include "../common/Steam.h"
#include "Json.h"
#include "Library.h"
#include "PimaxReader.h"
#include "TrackBeeper.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <wrl.h>
#include <WebView2.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

using namespace sg;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kImplicitKey[] = L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
constexpr wchar_t kManifestName[] = L"XR_APILAYER_NOVENDOR_steadygrip.json";
constexpr wchar_t kUiHost[] = L"app.steadygrip";
constexpr wchar_t kCoversHost[] = L"jaquettes.steadygrip";
constexpr UINT WM_APP_REFRESH = WM_APP + 1;   // jaquette téléchargée : rafraîchir l'interface
constexpr UINT_PTR kTimer = 1;

HWND g_wnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_web;
SharedMemory g_shared;
PimaxReader g_reader;
TrackBeeper g_beeper;
std::string g_lastState;   // dernier état envoyé à la page (on n'envoie que les changements)
std::string g_runningId;   // jeu en cours déjà traité (ajout automatique à la bibliothèque)

// --- Utilitaires ---------------------------------------------------------------------------------
std::wstring ExeDir() {
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring s = path;
	return s.substr(0, s.find_last_of(L"\\/"));
}

std::wstring ManifestPath() { return ExeDir() + L"\\" + kManifestName; }

// --- Installation de la couche (registre HKLM ; l'application tourne en administrateur) -----------
std::vector<std::wstring> RegisteredManifests() {
	std::vector<std::wstring> out;
	HKEY key;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kImplicitKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return out;
	for (DWORD i = 0;; i++) {
		wchar_t name[1024];
		DWORD len = 1024;
		if (RegEnumValueW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
		std::wstring n = name;
		if (n.size() >= wcslen(kManifestName) && _wcsicmp(n.c_str() + n.size() - wcslen(kManifestName), kManifestName) == 0) out.push_back(n);
	}
	RegCloseKey(key);
	return out;
}

// --- Raccourcis (bureau et menu Démarrer, pour tous les utilisateurs : l'application tourne en administrateur)
enum Where { OnDesktop = 0, InStartMenu = 1 };

std::wstring ShortcutPath(Where where) {
	PWSTR dir = nullptr;
	std::wstring out;
	if (SUCCEEDED(SHGetKnownFolderPath(where == OnDesktop ? FOLDERID_PublicDesktop : FOLDERID_CommonPrograms, 0, nullptr, &dir))) {
		out = std::wstring(dir) + L"\\SteadyGrip.lnk";
		CoTaskMemFree(dir);
	}
	return out;
}

bool HasShortcut(Where where) {
	const std::wstring p = ShortcutPath(where);
	return !p.empty() && FileExists(p);
}

// Crée (ou remplace, si l'application a été déplacée) le raccourci, ou le supprime.
void SetShortcut(Where where, bool on) {
	const std::wstring path = ShortcutPath(where);
	if (path.empty()) return;
	if (!on) {
		DeleteFileW(path.c_str());
		return;
	}
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	ComPtr<IShellLinkW> link;
	if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return;
	link->SetPath(exe);
	link->SetWorkingDirectory(ExeDir().c_str());
	link->SetIconLocation(exe, 0);
	link->SetDescription(L"SteadyGrip : stabilisation des manettes");
	ComPtr<IPersistFile> file;
	if (SUCCEEDED(link.As(&file))) file->Save(path.c_str(), TRUE);
}

int DoInstall(bool install) {
	HKEY key;
	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kImplicitKey, 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS)
		return 1;
	for (const auto& old : RegisteredManifests()) RegDeleteValueW(key, old.c_str()); // anciennes copies déplacées
	LONG r = ERROR_SUCCESS;
	if (install) {
		const DWORD zero = 0; // 0 = activée
		const std::wstring m = ManifestPath();
		r = RegSetValueExW(key, m.c_str(), 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
	}
	RegCloseKey(key);
	return r == ERROR_SUCCESS ? 0 : 1;
}

// 0 : non installée ; 1 : installée depuis ce dossier ; 2 : installée depuis un autre dossier.
int LayerStatus() {
	const auto regs = RegisteredManifests();
	if (regs.empty()) return 0;
	return _wcsicmp(regs[0].c_str(), ManifestPath().c_str()) == 0 ? 1 : 2;
}

// --- Réglages : un jeu (« steam:… », « exe:… ») ou « default » (réglages par défaut) ---------------
std::wstring SettingsSection(const std::string& id) { return id == "default" ? std::wstring(kGeneralSection) : GameSection(id.c_str()); }

bool HasOwnSettings(const std::string& id) { return id != "default" && HasProfile(SettingsSection(id)); }

// Réglages en vigueur pour un jeu : les siens, sinon les réglages par défaut.
void LoadFor(const std::string& id, int32_t& preset, FilterSettings& s) {
	LoadSettings(preset, s, HasOwnSettings(id) ? SettingsSection(id).c_str() : kGeneralSection);
}

// Jeu en cours et son identifiant ; faux si aucun jeu ne tourne avec la couche.
bool RunningGame(std::string& id, std::string& appName) {
	const SharedBlock* b = g_shared.block();
	if (!b || b->heartbeat == 0 || NowSeconds() - b->heartbeat > 3.0) return false;
	appName.assign(b->appName, strnlen(b->appName, sizeof(b->appName)));
	id = GameId(b->steamAppId, std::string(b->exeName, strnlen(b->exeName, sizeof(b->exeName))));
	if (id.empty()) id = "exe:" + appName;
	return true;
}

// Réglages modifiés : si ce sont ceux qu'utilise le jeu en cours, la couche les applique tout de suite.
void PushIfRunning(const std::string& id) {
	std::string run, app;
	if (!RunningGame(run, app)) return;
	if (id == run || (id == "default" && !HasOwnSettings(run))) {
		int32_t p;
		FilterSettings s;
		LoadFor(run, p, s);
		g_shared.writeSettings(s, p);
	}
}

int LevelOf(int32_t preset) { return preset == PresetCustom ? -1 : preset; }

const char* LevelName(int level) {
	static const char* n[] = { "Désactivée", "Réactive", "Équilibrée", "Très stable" };
	return level >= 0 && level <= 3 ? n[level] : "Personnalisée";
}

std::string SettingsJson(int32_t preset, const FilterSettings& s) {
	return "{\"level\":" + std::to_string(LevelOf(preset)) + ",\"lossGuard\":" + JsonBool(s.lossGuard) +
		",\"twoHands\":" + JsonBool(s.coupleMode == CoupleGrips) + ",\"armModel\":" + JsonBool(s.armModel) + ",\"learnedArm\":" + JsonBool(s.learnedArm) +
		",\"stillTauMs\":" + JsonNum(s.stillTauMs) + ",\"movingTauMs\":" + JsonNum(s.movingTauMs) +
		",\"teleportBlendMs\":" + JsonNum(s.teleportBlendMs) + ",\"orientSmoothing\":" + JsonNum(s.orientSmoothing) +
		",\"maxLostDriftCm\":" + JsonNum(std::round(s.maxLostDriftM * 100)) + ",\"maxReachCm\":" + JsonNum(std::round(s.maxReachM * 100)) +
		",\"gripFix\":" + JsonBool(s.gripFix) + ",\"gripHoldMs\":" + JsonNum(std::round(s.gripHoldMs)) + "}";
}

std::string Summary(const std::string& id) {
	if (id != "default" && !HasOwnSettings(id)) return "Réglages par défaut";
	int32_t p;
	FilterSettings s;
	LoadFor(id, p, s);
	if (!s.enabled) return s.gripFix ? "Désactivée · grip corrigé" : "Désactivée";
	std::string t = LevelName(LevelOf(p));
	if (s.coupleMode == CoupleGrips) t += " · armes à deux mains";
	if (s.gripFix) t += " · grip corrigé";
	return t;
}

std::string CoverUrl(const std::string& file) { return file.empty() ? std::string() : "https://" + Narrow(kCoversHost) + "/" + file; }

// --- Jaquettes (récupérées hors du fil de l'interface) ---------------------------------------------
void FetchCoverAsync(const std::string& id, uint32_t appId) {
	if (!appId) return;
	std::thread([id, appId] {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const std::string file = FetchSteamCover(appId);
		if (!file.empty()) IniSetStr(BiblioSection(id).c_str(), L"Image", Widen(file));
		CoUninitialize();
		PostMessageW(g_wnd, WM_APP_REFRESH, 0, 0);
	}).detach();
}

// Jeu lancé qui n'est pas encore dans la bibliothèque : on l'ajoute tout seul (avec sa jaquette Steam), et
// on reprend les réglages créés avant la 0.6 sous son nom OpenXR.
void AutoAddRunning() {
	std::string id, app;
	if (!RunningGame(id, app) || id == g_runningId) return;
	g_runningId = id;
	const SharedBlock* b = g_shared.block();
	GameEntry g;
	g.id = id;
	g.steamAppId = b->steamAppId;
	g.exe.assign(b->exePath, strnlen(b->exePath, sizeof(b->exePath)));
	g.name = app;
	if (g.steamAppId)
		for (const auto& sgame : InstalledSteamGames())
			if (sgame.appId == g.steamAppId) g.name = sgame.name;
	if (AddToLibrary(g)) FetchCoverAsync(g.id, g.steamAppId);
	const std::wstring legacy = GameSection(app.c_str());
	if (!HasOwnSettings(id) && HasProfile(legacy)) {
		int32_t p;
		FilterSettings s;
		LoadSettings(p, s, legacy.c_str());
		SaveSettings(p, s, SettingsSection(id).c_str());
	}
}

// --- État envoyé à la page -------------------------------------------------------------------------
std::string HandJson(const SharedHand& h, bool fresh) {
	std::string state = "none", text = "Pas de données";
	if (fresh) {
		switch (h.guardPhase) {
		case LossGuard::Contained: state = "lost"; text = "Hors champ : arme retenue"; break;
		case LossGuard::Holding: state = "lost"; text = h.coupled ? "Hors champ : suit l'autre main" : "Hors champ : arme gardée en main"; break;
		case LossGuard::Releasing: state = "return"; text = "Retour en douceur"; break;
		default: state = "ok"; text = "Vue par les caméras"; break;
		}
	}
	return "{\"state\":" + JsonStr(state) + ",\"text\":" + JsonStr(text) + "}";
}

bool BeepsWanted() { return IniGet(L"Sons", L"BipsSuivi", 0) != 0; }

bool RecordingWanted() { return IniGet(L"Diagnostic", L"EnregistrerPoses", 0) != 0; }

std::string BuildState() {
	SharedBlock* b = g_shared.block();
	const bool recording = RecordingWanted();
	const bool beeps = BeepsWanted();
	g_beeper.setEnabled(beeps);
	InterlockedExchange(&b->recordPoses, recording ? 1 : 0);
	const int layer = LayerStatus();
	const int source = b->trackSource;
	const bool pimaxOk = source == SourceActive || source == SourceSearching || source == SourceIncompatible || source == SourceNoModule;
	const bool signalOk = source == SourceActive;
	std::string readyText;
	if (layer == 0) readyText = "La stabilisation n'est pas installée";
	else if (layer == 2) readyText = "La stabilisation est installée depuis un autre dossier";
	else if (source == SourceNoPimax || source == SourceOff) readyText = "Lance Pimax Play ou Pimax EVO";
	else if (source == SourceDenied) readyText = "Relance SteadyGrip en administrateur";
	else if (source == SourceSearching) readyText = "Recherche du signal Pimax : fais sortir une manette du champ quelques secondes";
	else if (source == SourceIncompatible || source == SourceNoModule) readyText = "Version de Pimax non reconnue : lissage actif, sans la détection des pertes";
	else readyText = "Tout est prêt : lance ton jeu";
	const std::string pimaxText = pimaxOk ? "Casque Pimax détecté" : "Pimax non détecté";
	std::string signalText = signalOk ? "Détection des pertes de suivi active" : "Détection des pertes de suivi inactive";
	if (signalOk && b->pimaxCheck == CheckVerifying) signalText = "Nouvelle version de Pimax : vérification en cours";

	std::string runId, runApp;
	const bool running = RunningGame(runId, runApp);
	const double now = NowSeconds();
	std::string hands = "[";
	double rawSum = 0, outSum = 0;
	uint64_t caught = 0;
	for (int i = 0; i < 2; i++) {
		const SharedHand& h = b->hands[i];
		const bool fresh = running && h.lastUpdate != 0 && now - h.lastUpdate < 2.0;
		hands += (i ? "," : "") + HandJson(h, fresh);
		if (fresh && h.tremorRaw > 0) { rawSum += h.tremorRaw; outSum += h.tremorOut; }
		if (fresh) caught += h.caught;
	}
	hands += "]";
	const int tremorPct = rawSum > 0 ? (int)std::lround(100.0 * (1.0 - outSum / rawSum)) : 0;

	std::string games = "[";
	bool first = true;
	for (const auto& g : LoadGameLibrary()) {
		games += (first ? "" : ",") + std::string("{\"id\":") + JsonStr(g.id) + ",\"name\":" + JsonStr(g.name) + ",\"image\":" + JsonStr(CoverUrl(g.image)) +
			",\"own\":" + JsonBool(HasOwnSettings(g.id)) + ",\"summary\":" + JsonStr(Summary(g.id)) + ",\"running\":" + JsonBool(running && g.id == runId) + "}";
		first = false;
	}
	games += "]";

	std::string out = "{\"type\":\"state\",\"version\":" + JsonStr(kVersion);
	out += ",\"theme\":" + JsonStr(Narrow(IniGetStr(L"Interface", L"Theme")));
	out += ",\"desktop\":" + JsonBool(HasShortcut(OnDesktop)) + ",\"startMenu\":" + JsonBool(HasShortcut(InStartMenu));
	out += ",\"layer\":" + std::to_string(layer) + ",\"ready\":" + JsonBool(layer == 1 && signalOk) + ",\"readyText\":" + JsonStr(readyText);
	out += ",\"checks\":[{\"ok\":" + JsonBool(layer == 1) + ",\"text\":" + JsonStr(layer == 1 ? "Stabilisation installée" : "Stabilisation non installée") + "}";
	out += ",{\"ok\":" + JsonBool(pimaxOk) + ",\"text\":" + JsonStr(pimaxText) + "}";
	out += ",{\"ok\":" + JsonBool(signalOk) + ",\"text\":" + JsonStr(signalText) + "}]";
	out += ",\"pimaxVersion\":" + JsonStr(std::string(b->pimaxVersion, strnlen(b->pimaxVersion, sizeof(b->pimaxVersion))));
	out += ",\"running\":" + (running ? "{\"id\":" + JsonStr(runId) + ",\"appName\":" + JsonStr(runApp) + "}" : std::string("null"));
	out += ",\"hands\":" + hands + ",\"tremorPct\":" + std::to_string(tremorPct) + ",\"caught\":" + std::to_string(caught);
	out += ",\"beeps\":" + JsonBool(beeps);
	out += ",\"recording\":" + JsonBool(recording) + ",\"recordedFrames\":" + std::to_string(b->recordedFrames);
	out += ",\"recordFile\":" + JsonStr(std::string(b->recordFile, strnlen(b->recordFile, sizeof(b->recordFile))));
	out += ",\"defaultSummary\":" + JsonStr(Summary("default")) + ",\"games\":" + games + "}";
	return out;
}

void Post(const std::string& json) {
	if (g_web) g_web->PostWebMessageAsJson(Widen(json).c_str());
}

void SendState(bool force) {
	AutoAddRunning();
	const std::string s = BuildState();
	if (!force && s == g_lastState) return;
	g_lastState = s;
	Post(s);
}

void SendGame(const std::string& id) {
	int32_t p;
	FilterSettings s;
	LoadFor(id, p, s);
	std::string name = "Réglages par défaut", image;
	for (const auto& g : LoadGameLibrary())
		if (g.id == id) { name = g.name; image = CoverUrl(g.image); }
	Post("{\"type\":\"game\",\"id\":" + JsonStr(id) + ",\"name\":" + JsonStr(name) + ",\"image\":" + JsonStr(image) +
		",\"own\":" + JsonBool(id == "default" || HasOwnSettings(id)) + ",\"settings\":" + SettingsJson(s.enabled ? p : PresetOff, s) + "}");
}

// --- Boîtes de dialogue de fichiers -----------------------------------------------------------------
std::wstring PickFile(const wchar_t* title, const COMDLG_FILTERSPEC* filters, UINT count) {
	std::wstring result;
	ComPtr<IFileOpenDialog> dlg;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
	dlg->SetTitle(title);
	dlg->SetFileTypes(count, filters);
	if (SUCCEEDED(dlg->Show(g_wnd))) {
		ComPtr<IShellItem> item;
		PWSTR path = nullptr;
		if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
			result = path;
			CoTaskMemFree(path);
		}
	}
	return result;
}

std::wstring PickExe() {
	static const COMDLG_FILTERSPEC f[] = { { L"Programmes", L"*.exe" } };
	return PickFile(L"Programme du jeu", f, 1);
}

std::wstring PickImage() {
	static const COMDLG_FILTERSPEC f[] = { { L"Images", L"*.jpg;*.jpeg;*.png;*.webp" } };
	return PickFile(L"Image du jeu", f, 1);
}

// --- Messages de la page ---------------------------------------------------------------------------
void ApplySetting(const std::string& id, const std::string& key, const JsonValue& v) {
	int32_t p;
	FilterSettings s;
	LoadFor(id, p, s);
	if (key == "level") {
		const int level = std::clamp((int)v.num(), 0, 3);
		const FilterSettings keep = s; // les réglages des pertes ne dépendent pas du niveau
		s = Preset(level);
		s.lossGuard = keep.lossGuard;
		s.coupleMode = keep.coupleMode;
		s.armModel = keep.armModel;
		s.learnedArm = keep.learnedArm;
		s.maxLostDriftM = keep.maxLostDriftM;
		s.maxReachM = keep.maxReachM;
		s.gripFix = keep.gripFix;
		s.gripHoldMs = keep.gripHoldMs;
		s.enabled = level != PresetOff;
		p = level;
	} else if (key == "lossGuard") s.lossGuard = v.truthy();
	else if (key == "twoHands") s.coupleMode = v.truthy() ? CoupleGrips : CoupleNever;
	else if (key == "armModel") s.armModel = v.truthy();
	else if (key == "learnedArm") s.learnedArm = v.truthy();
	else if (key == "gripFix") s.gripFix = v.truthy();
	else if (key == "gripHoldMs") s.gripHoldMs = std::clamp(v.num(), 30.0, 400.0);
	else if (key == "maxLostDriftCm") s.maxLostDriftM = std::clamp(v.num(), 5.0, 60.0) / 100.0;
	else if (key == "maxReachCm") s.maxReachM = std::clamp(v.num(), 50.0, 120.0) / 100.0;
	else {
		// Réglages fins du lissage : on passe en préréglage personnalisé.
		if (key == "stillTauMs") s.stillTauMs = std::clamp(v.num(), 10.0, 200.0);
		else if (key == "movingTauMs") s.movingTauMs = std::clamp(v.num(), 0.0, 30.0);
		else if (key == "teleportBlendMs") s.teleportBlendMs = std::clamp(v.num(), 0.0, 300.0);
		else if (key == "orientSmoothing") s.orientSmoothing = std::clamp(v.num(), 0.0, 100.0);
		else return;
		p = PresetCustom;
		s.enabled = true;
	}
	SaveSettings(p, s, SettingsSection(id).c_str());
	PushIfRunning(id);
}

void ResetSettings(const std::string& id) {
	if (id == "default") SaveSettings(PresetMedium, Preset(PresetMedium), kGeneralSection);
	else DeleteProfile(SettingsSection(id)); // le jeu reprend les réglages par défaut
	PushIfRunning(id);
	PushIfRunning("default");
}

void SendSteamList() {
	const auto lib = LoadGameLibrary();
	std::string out = "{\"type\":\"steam\",\"games\":[";
	bool first = true;
	for (const auto& g : InstalledSteamGames()) {
		// Outils de Steam (redistribuables, SteamVR...) : inutiles ici.
		const std::string low = LowerAscii(g.name);
		if (low.find("redistributable") != std::string::npos || low.find("steamvr") != std::string::npos || low.find("steamworks") != std::string::npos) continue;
		bool added = false;
		for (const auto& e : lib) added |= e.steamAppId == g.appId;
		out += (first ? "" : ",") + std::string("{\"appId\":") + std::to_string(g.appId) + ",\"name\":" + JsonStr(g.name) + ",\"added\":" + JsonBool(added) + "}";
		first = false;
	}
	Post(out + "]}");
}

void OnMessage(const std::string& text) {
	std::map<std::string, JsonValue> m;
	if (!JsonParseFlat(text, m)) return;
	const std::string cmd = m["cmd"].text;
	const std::string id = m["id"].text;
	if (cmd == "ready") { SendState(true); return; }
	if (cmd == "theme") IniSetStr(L"Interface", L"Theme", Widen(m["value"].text));
	else if (cmd == "install") {
		DoInstall(true);
		SetShortcut(OnDesktop, true); // raccourcis créés à l'installation (et remis à jour si le dossier a changé)
		SetShortcut(InStartMenu, true);
	} else if (cmd == "shortcut") SetShortcut(m["where"].text == "desktop" ? OnDesktop : InStartMenu, m["value"].truthy());
	else if (cmd == "uninstall") DoInstall(false);
	else if (cmd == "beeps") { IniSet(L"Sons", L"BipsSuivi", m["value"].truthy() ? 1 : 0); g_beeper.setEnabled(m["value"].truthy()); }
	else if (cmd == "beepTest") g_beeper.test();
	else if (cmd == "record") IniSet(L"Diagnostic", L"EnregistrerPoses", m["value"].truthy() ? 1 : 0);
	else if (cmd == "getGame") { SendGame(id); return; }
	else if (cmd == "set") { ApplySetting(id, m["key"].text, m["value"]); SendGame(id); }
	else if (cmd == "reset") { ResetSettings(id); SendGame(id); }
	else if (cmd == "listSteam") { SendSteamList(); return; }
	else if (cmd == "addSteam") {
		GameEntry g;
		g.steamAppId = (uint32_t)m["appId"].num();
		g.id = GameId(g.steamAppId, "");
		g.name = m["name"].text;
		if (g.steamAppId && AddToLibrary(g)) FetchCoverAsync(g.id, g.steamAppId);
	} else if (cmd == "pickExe") {
		const std::wstring path = PickExe();
		if (!path.empty()) {
			const std::wstring file = path.substr(path.find_last_of(L"\\/") + 1);
			Post("{\"type\":\"exePicked\",\"path\":" + JsonStr(Narrow(path)) + ",\"name\":" + JsonStr(Narrow(file.substr(0, file.find_last_of(L'.')))) + "}");
		}
		return;
	} else if (cmd == "pickImage") {
		const std::wstring path = PickImage();
		if (!path.empty()) Post("{\"type\":\"imagePicked\",\"path\":" + JsonStr(Narrow(path)) + "}");
		return;
	} else if (cmd == "addExe") {
		const std::string path = m["path"].text;
		if (path.empty()) return;
		GameEntry g;
		g.steamAppId = SteamAppIdForExe(path); // un .exe de jeu Steam choisi à la main reste reconnu comme jeu Steam
		g.id = GameId(g.steamAppId, LowerAscii(path.substr(path.find_last_of("\\/") + 1)));
		g.exe = path;
		g.name = m["name"].text.empty() ? g.id : m["name"].text;
		if (!m["image"].text.empty()) g.image = ImportImage(g.id, Widen(m["image"].text));
		if (AddToLibrary(g) && g.image.empty()) FetchCoverAsync(g.id, g.steamAppId);
	} else if (cmd == "setImage") {
		const std::wstring path = PickImage();
		if (path.empty()) return;
		const std::string file = ImportImage(id, path);
		if (!file.empty()) IniSetStr(BiblioSection(id).c_str(), L"Image", Widen(file));
		SendGame(id);
	} else if (cmd == "rename") {
		if (!m["name"].text.empty()) IniSetStr(BiblioSection(id).c_str(), L"Nom", Widen(m["name"].text));
		SendGame(id);
	} else if (cmd == "remove") {
		RemoveFromLibrary(id);
		if (id == g_runningId) g_runningId.clear();
	}
	SendState(true);
}

// --- WebView2 ---------------------------------------------------------------------------------------
void ResizeWebView() {
	if (!g_controller) return;
	RECT r;
	GetClientRect(g_wnd, &r);
	g_controller->put_Bounds(r);
}

void ShowFatal(const wchar_t* text) {
	MessageBoxW(g_wnd, text, L"SteadyGrip", MB_ICONERROR);
	PostQuitMessage(1);
}

constexpr wchar_t kNoWebView[] = L"WebView2, le moteur web de Windows, est introuvable.\n\n"
	L"Installe « Microsoft Edge WebView2 Runtime » depuis le site de Microsoft, puis relance SteadyGrip.";

HRESULT OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller) {
	if (FAILED(result) || !controller) {
		ShowFatal(L"Impossible d'afficher l'interface (WebView2).");
		return S_OK;
	}
	g_controller = controller;
	g_controller->get_CoreWebView2(&g_web);
	// Fond sombre pendant le chargement (pas d'éclair blanc).
	ComPtr<ICoreWebView2Controller2> c2;
	if (SUCCEEDED(g_controller.As(&c2))) c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{ 255, 17, 17, 17 });
	ComPtr<ICoreWebView2Settings> settings;
	if (SUCCEEDED(g_web->get_Settings(&settings))) {
		settings->put_AreDefaultContextMenusEnabled(FALSE);
		settings->put_IsStatusBarEnabled(FALSE);
		settings->put_IsZoomControlEnabled(FALSE);
#ifdef NDEBUG
		settings->put_AreDevToolsEnabled(FALSE);
#endif
	}
	// La page vient du dossier « ui » à côté de l'application, les jaquettes du dossier des données.
	ComPtr<ICoreWebView2_3> w3;
	if (FAILED(g_web.As(&w3))) {
		ShowFatal(L"WebView2 est trop ancien. Mets à jour « Microsoft Edge WebView2 Runtime », puis relance SteadyGrip.");
		return S_OK;
	}
	w3->SetVirtualHostNameToFolderMapping(kUiHost, (ExeDir() + L"\\ui").c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
	w3->SetVirtualHostNameToFolderMapping(kCoversHost, CoversDir().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
	EventRegistrationToken token;
	g_web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
		[](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
			LPWSTR msg = nullptr;
			if (SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg) {
				const std::string text = Narrow(msg);
				CoTaskMemFree(msg);
				OnMessage(text);
			}
			return S_OK;
		}).Get(), &token);
	ResizeWebView();
	g_web->Navigate((std::wstring(L"https://") + kUiHost + L"/index.html").c_str());
	SetTimer(g_wnd, kTimer, 250, nullptr);
	return S_OK;
}

void InitWebView() {
	const std::wstring dataDir = DataDir() + L"\\WebView2";
	const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
			if (FAILED(result) || !env) {
				ShowFatal(kNoWebView);
				return S_OK;
			}
			env->CreateCoreWebView2Controller(g_wnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
				[](HRESULT r, ICoreWebView2Controller* c) -> HRESULT { return OnControllerCreated(r, c); }).Get());
			return S_OK;
		}).Get());
	if (FAILED(hr)) ShowFatal(kNoWebView);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_SIZE: ResizeWebView(); return 0;
	case WM_GETMINMAXINFO: {
		auto* mm = (MINMAXINFO*)lp;
		const UINT dpi = GetDpiForWindow(hwnd);
		mm->ptMinTrackSize = { MulDiv(980, dpi, 96), MulDiv(660, dpi, 96) };
		return 0;
	}
	case WM_TIMER: SendState(false); return 0;
	case WM_APP_REFRESH: SendState(true); return 0;
	case WM_DESTROY:
		KillTimer(hwnd, kTimer);
		if (g_controller) g_controller->Close();
		g_web.Reset();
		g_controller.Reset();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
	// Mode ligne de commande : SteadyGripApp.exe --install | --uninstall
	if (cmd && wcsstr(cmd, L"--install")) {
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const int r = DoInstall(true);
		SetShortcut(OnDesktop, true);
		SetShortcut(InStartMenu, true);
		return r;
	}
	if (cmd && wcsstr(cmd, L"--uninstall")) {
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const int r = DoInstall(false);
		SetShortcut(OnDesktop, false);
		SetShortcut(InStartMenu, false);
		return r;
	}

	// Une seule instance.
	HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SteadyGripApp");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		if (HWND w = FindWindowW(L"SteadyGripApp", nullptr)) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
		return 0;
	}

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	if (!g_shared.open()) {
		MessageBoxW(nullptr, L"Impossible d'ouvrir la mémoire partagée.", L"SteadyGrip", MB_ICONERROR);
		return 1;
	}
	g_reader.start(g_shared.block()); // lecture de l'état de suivi Pimax (lecture seule)
	g_beeper.setEnabled(BeepsWanted());
	g_beeper.start(g_shared.block()); // bips de suivi (option)

	WNDCLASSEXW wc{ sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(17, 17, 17));
	wc.lpszClassName = L"SteadyGripApp";
	wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
	wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	RegisterClassExW(&wc);

	const UINT dpi = GetDpiForSystem();
	RECT r{ 0, 0, MulDiv(1240, dpi, 96), MulDiv(800, dpi, 96) };
	AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
	g_wnd = CreateWindowExW(0, wc.lpszClassName, L"SteadyGrip", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
	ShowWindow(g_wnd, show);
	InitWebView();

	MSG m;
	while (GetMessageW(&m, nullptr, 0, 0)) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	g_beeper.stop();
	g_reader.stop();
	CoUninitialize();
	if (mutex) CloseHandle(mutex);
	return 0;
}
