// SteadyGrip - mémoire partagée entre la couche OpenXR (dans le jeu) et l'application compagnon,
// et lecture/écriture des réglages sur disque.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "PoseFilter.h"

namespace sg {

constexpr uint32_t kSharedMagic = 0x53475250; // 'SGRP'
constexpr uint32_t kSharedVersion = 10;
constexpr wchar_t kSharedName[] = L"Local\\SteadyGripShared_v11";
constexpr char kVersion[] = "0.6.5";

enum Preset : int32_t { PresetOff = 0, PresetLight = 1, PresetMedium = 2, PresetStrong = 3, PresetCustom = 4 };

struct SharedHand {
	char path[64];            // ex. /user/hand/left
	double lastUpdate;        // horodatage (secondes, QueryPerformanceCounter) de la dernière mise à jour
	double corrPerSec;
	double lastCorrMm;
	double offsetMm;
	double offsetDeg;
	uint64_t teleports;
	int32_t outOfView;
	int32_t velocityMissing;
	int32_t trackState;       // état Pimax appliqué (TrackState)
	int32_t guardPhase;       // LossGuard::Phase
	int32_t coupled;          // main liée à l'autre main pendant la perte
	int32_t pad3;
	uint64_t holds;           // nombre de maintiens pendant une réinitialisation
	double gapAvgCm;          // écart moyen évité à la reprise (cm)
	double lastGapCm;         // dernier écart évité
	int32_t gripHeld;         // bouton grip serré (-1 : grip non lu dans ce jeu)
	int32_t pad5;
	double tremorRaw;         // tremblement de visée de la période en cours (°/image) : Pimax seul...
	double tremorOut;         // ... et après SteadyGrip
	uint64_t caught;          // pertes contenues depuis le début de la partie
};

// État de suivi d'une manette, lu par l'application compagnon dans le journal du service Pimax.
struct SharedTrack {
	volatile LONG state;      // TrackState
	int32_t pad;
	double changed;           // NowSeconds() du dernier changement
	uint64_t transitions;
};

// Où en est la lecture du service Pimax (application compagnon).
enum TrackSource : int32_t {
	SourceOff = 0,            // application compagnon absente
	SourceNoPimax = 1,        // service Pimax introuvable
	SourceDenied = 2,         // accès refusé (pas lancée en administrateur)
	SourceSearching = 3,      // service trouvé, tampon du journal pas encore trouvé
	SourceActive = 4,         // états reçus en temps réel
	SourceNoModule = 5,       // service Pimax trouvé, mais sans le module de suivi des manettes attendu
	SourceIncompatible = 6,   // le journal n'a plus le format attendu (mise à jour de Pimax ?)
};

// Version de Pimax comparée à la dernière version où la lecture a été vérifiée.
enum PimaxCheck : int32_t {
	CheckUnknown = 0,         // version pas encore lue
	CheckValidated = 1,       // même version que lors de la dernière vérification réussie
	CheckVerifying = 2,       // nouvelle version : vérification en cours (lignes mémoire comparées au fichier)
	CheckNewValidated = 3,    // nouvelle version vérifiée pendant cette session
};

struct SharedBlock {
	uint32_t magic;
	uint32_t version;
	volatile LONG settingsSeq;    // impair pendant une écriture
	int32_t preset;
	FilterSettings settings;
	// Rempli par la couche :
	char appName[128];
	uint32_t pid;
	double heartbeat;             // horodatage de la dernière image vue par la couche
	SharedHand hands[2];          // 0 = gauche, 1 = droite
	// Rempli par l'application compagnon :
	double trackHeartbeat;        // dernière lecture du service Pimax
	volatile LONG trackSource;    // TrackSource
	int32_t pad2;
	SharedTrack track[2];         // 0 = gauche, 1 = droite
	char pimaxVersion[96];        // version du service et du module de suivi Pimax
	volatile LONG pimaxCheck;     // PimaxCheck
	int32_t pad4;
	uint64_t matchedLines;        // lignes du fichier retrouvées d'abord en mémoire (vérification)
	// Rempli par la couche : identité du jeu en cours.
	char exeName[128];            // nom du programme du jeu, en minuscules (ex. cfss-win64-shipping.exe)
	char exePath[520];            // chemin complet (UTF-8)
	uint32_t steamAppId;          // numéro Steam du jeu (0 : pas un jeu Steam)
	int32_t pad6;
	char profileSection[160];     // section des réglages utilisée (UTF-8), vide : réglages par défaut
	// Enregistrement des mouvements (diagnostic) : demandé par l'application, état rendu par la couche.
	volatile LONG recordPoses;    // 1 : la couche enregistre les poses
	int32_t pad7;
	uint64_t recordedFrames;      // lignes écrites depuis le début de l'enregistrement en cours
	char recordFile[260];         // nom du fichier en cours (UTF-8), vide : aucun
};

inline double NowSeconds() {
	static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart / (double)freq.QuadPart;
}

// Dossier %LOCALAPPDATA%\SteadyGrip (créé si besoin).
inline std::wstring DataDir() {
	PWSTR p = nullptr;
	std::wstring dir;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
		dir = std::wstring(p) + L"\\SteadyGrip";
		CoTaskMemFree(p);
	} else {
		dir = L".";
	}
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

inline std::wstring IniPath() { return DataDir() + L"\\settings.ini"; }

// Réglages : section [Filtre] = réglages généraux ; section [Jeu:<nom>] = profil propre à un jeu
// (nom transmis par le jeu à OpenXR, par exemple « CFSS0.0.1.0 » pour CrossFire: Sierra Squad).
constexpr wchar_t kGeneralSection[] = L"Filtre";

inline std::wstring GameSection(const char* appName) {
	std::wstring w = L"Jeu:";
	for (const char* c = appName; c && *c; c++) w += (*c == '[' || *c == ']' || *c == '=') ? L'_' : (wchar_t)(unsigned char)*c;
	return w;
}

inline double IniGet(const wchar_t* section, const wchar_t* key, double def) {
	wchar_t buf[64];
	GetPrivateProfileStringW(section, key, L"", buf, 64, IniPath().c_str());
	return buf[0] ? _wtof(buf) : def;
}

inline void IniSet(const wchar_t* section, const wchar_t* key, double v) {
	wchar_t buf[64];
	swprintf_s(buf, L"%g", v);
	WritePrivateProfileStringW(section, key, buf, IniPath().c_str());
}

inline std::wstring IniGetStr(const wchar_t* section, const wchar_t* key) {
	wchar_t buf[256];
	GetPrivateProfileStringW(section, key, L"", buf, 256, IniPath().c_str());
	return buf;
}

inline void IniSetStr(const wchar_t* section, const wchar_t* key, const std::wstring& v) {
	WritePrivateProfileStringW(section, key, v.c_str(), IniPath().c_str());
}

inline bool HasProfile(const std::wstring& section) {
	wchar_t buf[16];
	GetPrivateProfileStringW(section.c_str(), L"Preset", L"", buf, 16, IniPath().c_str());
	return buf[0] != 0;
}

inline void DeleteProfile(const std::wstring& section) { WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, IniPath().c_str()); }

inline void LoadSettings(int32_t& preset, FilterSettings& s, const wchar_t* section = kGeneralSection) {
	preset = (int32_t)IniGet(section, L"Preset", PresetMedium);
	if (preset < PresetOff || preset > PresetCustom) preset = PresetMedium;
	s = Preset(preset == PresetCustom ? PresetMedium : preset);
	if (preset == PresetCustom) {
		s.stillTauMs = IniGet(section, L"LissageArretMs", s.stillTauMs);
		s.movingTauMs = IniGet(section, L"LissageMouvementMs", s.movingTauMs);
		s.teleportBlendMs = IniGet(section, L"EtalementTeleportMs", s.teleportBlendMs);
		s.orientSmoothing = std::clamp(IniGet(section, L"StabilisationOrientation", s.orientSmoothing), 0.0, 100.0);
	}
	s.enabled = preset != PresetOff && IniGet(section, L"Active", 1) != 0;
	s.lossGuard = IniGet(section, L"ContenirPertes", 1) != 0;
	s.coupleMode = (int)IniGet(section, L"LierMains", CoupleNever);
	if (s.coupleMode < CoupleAuto || s.coupleMode > CoupleGrips) s.coupleMode = CoupleNever;
	s.maxLostDriftM = std::clamp(IniGet(section, L"DeriveMaxCm", s.maxLostDriftM * 100) / 100.0, 0.05, 0.60);
	s.maxReachM = std::clamp(IniGet(section, L"PorteeBrasCm", s.maxReachM * 100) / 100.0, 0.50, 1.20);
	s.armModel = IniGet(section, L"ModeleBras", 1) != 0;
	s.learnedArm = IniGet(section, L"ModeleBrasAppris", 0) != 0;
	s.gripFix = IniGet(section, L"CorrigerGrip", 0) != 0;
	s.gripHoldMs = std::clamp(IniGet(section, L"GripMaintienMs", s.gripHoldMs), 30.0, 400.0);
}

// Réglages d'un jeu : son profil s'il en a un, sinon les réglages généraux.
inline void LoadSettingsForGame(const char* appName, int32_t& preset, FilterSettings& s) {
	const std::wstring sec = GameSection(appName);
	LoadSettings(preset, s, appName && *appName && HasProfile(sec) ? sec.c_str() : kGeneralSection);
}

// Identifiant d'un jeu dans la bibliothèque : « steam:<numéro> » pour un jeu Steam, sinon « exe:<programme> ».
inline std::string GameId(uint32_t steamAppId, const std::string& exeName) {
	if (steamAppId) return "steam:" + std::to_string(steamAppId);
	return exeName.empty() ? std::string() : "exe:" + exeName;
}

inline std::wstring Widen(const std::string& u) {
	if (u.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), w.data(), n);
	return w;
}

inline std::string Narrow(const std::wstring& w) {
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string u(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), u.data(), n, nullptr, nullptr);
	return u;
}

// Section de réglages d'un jeu : on cherche d'abord par numéro Steam, puis par programme, puis par le nom
// OpenXR (profils créés avant la 0.6). Renvoie une chaîne vide si le jeu n'a pas de réglages à lui.
inline std::wstring FindGameSection(uint32_t steamAppId, const std::string& exeName, const char* appName) {
	if (steamAppId) {
		const std::wstring s = GameSection(("steam:" + std::to_string(steamAppId)).c_str());
		if (HasProfile(s)) return s;
	}
	if (!exeName.empty()) {
		const std::wstring s = GameSection(("exe:" + exeName).c_str());
		if (HasProfile(s)) return s;
	}
	if (appName && *appName) {
		const std::wstring s = GameSection(appName);
		if (HasProfile(s)) return s;
	}
	return {};
}

inline void SaveSettings(int32_t preset, const FilterSettings& s, const wchar_t* section = kGeneralSection) {
	IniSet(section, L"Preset", preset);
	IniSet(section, L"Active", s.enabled ? 1 : 0);
	IniSet(section, L"LissageArretMs", s.stillTauMs);
	IniSet(section, L"LissageMouvementMs", s.movingTauMs);
	IniSet(section, L"EtalementTeleportMs", s.teleportBlendMs);
	IniSet(section, L"ContenirPertes", s.lossGuard ? 1 : 0);
	IniSet(section, L"LierMains", s.coupleMode);
	IniSet(section, L"DeriveMaxCm", std::round(s.maxLostDriftM * 100));
	IniSet(section, L"PorteeBrasCm", std::round(s.maxReachM * 100));
	IniSet(section, L"ModeleBras", s.armModel ? 1 : 0);
	IniSet(section, L"ModeleBrasAppris", s.learnedArm ? 1 : 0);
	IniSet(section, L"StabilisationOrientation", std::round(s.orientSmoothing));
	IniSet(section, L"CorrigerGrip", s.gripFix ? 1 : 0);
	IniSet(section, L"GripMaintienMs", std::round(s.gripHoldMs));
}

// Ouvre (ou crée) la mémoire partagée. Accessible aux processus élevés comme non élevés.
class SharedMemory {
public:
	bool open() {
		SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, FALSE };
		PSECURITY_DESCRIPTOR sd = nullptr;
		// Tout le monde : accès complet ; niveau d'intégrité moyen pour qu'un jeu lancé en administrateur
		// et l'application compagnon (non élevée) puissent tous deux écrire.
		if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;ME)", SDDL_REVISION_1, &sd, nullptr))
			sa.lpSecurityDescriptor = sd;
		map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE, 0, sizeof(SharedBlock), kSharedName);
		const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
		if (sd) LocalFree(sd);
		if (!map_) map_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kSharedName);
		if (!map_) return false;
		block_ = (SharedBlock*)MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock));
		if (!block_) return false;
		if (!existed || block_->magic != kSharedMagic || block_->version != kSharedVersion) {
			ZeroMemory(block_, sizeof(SharedBlock));
			int32_t preset;
			FilterSettings s;
			LoadSettings(preset, s);
			block_->preset = preset;
			block_->settings = s;
			block_->version = kSharedVersion;
			MemoryBarrier();
			block_->magic = kSharedMagic;
		}
		return true;
	}

	~SharedMemory() {
		if (block_) UnmapViewOfFile(block_);
		if (map_) CloseHandle(map_);
	}

	SharedBlock* block() const { return block_; }

	// Lecture cohérente des réglages (protocole « seqlock »).
	bool readSettings(FilterSettings& out, int32_t& preset) const {
		if (!block_) return false;
		for (int i = 0; i < 8; i++) {
			const LONG a = block_->settingsSeq;
			if (a & 1) { YieldProcessor(); continue; }
			MemoryBarrier();
			out = block_->settings;
			preset = block_->preset;
			MemoryBarrier();
			if (block_->settingsSeq == a) return true;
		}
		return false;
	}

	void writeSettings(const FilterSettings& s, int32_t preset) {
		if (!block_) return;
		InterlockedIncrement(&block_->settingsSeq);
		block_->settings = s;
		block_->preset = preset;
		InterlockedIncrement(&block_->settingsSeq);
	}

private:
	HANDLE map_ = nullptr;
	SharedBlock* block_ = nullptr;
};

} // namespace sg
