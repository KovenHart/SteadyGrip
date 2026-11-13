// SteadyGrip - bibliothèque des jeux de l'application : jeux ajoutés (Steam ou programme .exe) et leurs
// jaquettes. Tout est dans settings.ini :
//   [Bibliotheque] Jeux=steam:1234|exe:monjeu.exe|...
//   [Biblio:<id>]  Nom=..., Programme=<chemin>, Steam=<numéro>, Image=<fichier dans le dossier des jaquettes>
// Les réglages d'un jeu restent dans [Jeu:<id>] (voir Shared.h), les réglages par défaut dans [Filtre].
#pragma once

#include "../common/Shared.h"
#include "../common/Steam.h"

#include <urlmon.h>

#include <cctype>
#include <cwctype>

#include <string>
#include <vector>

#pragma comment(lib, "urlmon.lib")

namespace sg {

struct GameEntry {
	std::string id;      // steam:<numéro> ou exe:<programme en minuscules>
	std::string name;    // UTF-8
	std::string exe;     // chemin du programme (jeux hors Steam), UTF-8
	uint32_t steamAppId = 0;
	std::string image;   // nom du fichier de jaquette dans CoversDir(), vide si aucune
};

inline std::wstring CoversDir() {
	const std::wstring d = DataDir() + L"\\jaquettes";
	CreateDirectoryW(d.c_str(), nullptr);
	return d;
}

// Nom de fichier sûr pour un identifiant (steam:123 -> steam_123).
inline std::string SafeFileName(const std::string& id) {
	std::string o;
	for (char c : id) o += (isalnum((unsigned char)c) || c == '-' || c == '.') ? c : '_';
	return o;
}

inline std::vector<std::string> SplitIds(const std::string& s) {
	std::vector<std::string> out;
	size_t start = 0;
	while (start <= s.size()) {
		const size_t p = s.find('|', start);
		const std::string part = s.substr(start, p == std::string::npos ? std::string::npos : p - start);
		if (!part.empty()) out.push_back(part);
		if (p == std::string::npos) break;
		start = p + 1;
	}
	return out;
}

inline std::wstring BiblioSection(const std::string& id) { return L"Biblio:" + Widen(id); }

inline std::vector<GameEntry> LoadGameLibrary() {
	std::vector<GameEntry> out;
	for (const auto& id : SplitIds(Narrow(IniGetStr(L"Bibliotheque", L"Jeux")))) {
		const std::wstring sec = BiblioSection(id);
		GameEntry g;
		g.id = id;
		g.name = Narrow(IniGetStr(sec.c_str(), L"Nom"));
		g.exe = Narrow(IniGetStr(sec.c_str(), L"Programme"));
		g.steamAppId = (uint32_t)IniGet(sec.c_str(), L"Steam", 0);
		g.image = Narrow(IniGetStr(sec.c_str(), L"Image"));
		if (g.name.empty()) g.name = id;
		out.push_back(g);
	}
	return out;
}

inline void SaveLibraryOrder(const std::vector<GameEntry>& lib) {
	std::string ids;
	for (const auto& g : lib) ids += (ids.empty() ? "" : "|") + g.id;
	IniSetStr(L"Bibliotheque", L"Jeux", Widen(ids));
}

inline void SaveEntry(const GameEntry& g) {
	const std::wstring sec = BiblioSection(g.id);
	IniSetStr(sec.c_str(), L"Nom", Widen(g.name));
	IniSetStr(sec.c_str(), L"Programme", Widen(g.exe));
	IniSet(sec.c_str(), L"Steam", g.steamAppId);
	IniSetStr(sec.c_str(), L"Image", Widen(g.image));
}

// Ajoute (ou met à jour) un jeu ; renvoie faux s'il était déjà dans la bibliothèque.
inline bool AddToLibrary(const GameEntry& g) {
	auto lib = LoadGameLibrary();
	for (auto& e : lib)
		if (e.id == g.id) return false;
	lib.push_back(g);
	SaveEntry(g);
	SaveLibraryOrder(lib);
	return true;
}

// Retire un jeu de la bibliothèque (ses réglages et sa jaquette sont supprimés aussi).
inline void RemoveFromLibrary(const std::string& id) {
	auto lib = LoadGameLibrary();
	std::vector<GameEntry> keep;
	for (auto& e : lib) {
		if (e.id != id) { keep.push_back(e); continue; }
		if (!e.image.empty()) DeleteFileW((CoversDir() + L"\\" + Widen(e.image)).c_str());
	}
	SaveLibraryOrder(keep);
	WritePrivateProfileStringW(BiblioSection(id).c_str(), nullptr, nullptr, IniPath().c_str());
	DeleteProfile(GameSection(id.c_str()));
}

inline bool FileExists(const std::wstring& p) {
	const DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Jaquette d'un jeu Steam : d'abord dans le cache de Steam sur ce PC (aucun téléchargement), sinon sur le
// serveur d'images de Steam. Renvoie le nom du fichier dans CoversDir(), vide en cas d'échec.
// Appelée hors du fil de l'interface (le téléchargement peut prendre quelques secondes).
inline std::string FetchSteamCover(uint32_t appId) {
	if (!appId) return {};
	const std::string file = "steam_" + std::to_string(appId) + ".jpg";
	const std::wstring dst = CoversDir() + L"\\" + Widen(file);
	if (FileExists(dst)) return file;
	const std::wstring root = SteamRoot();
	const std::wstring id = std::to_wstring(appId);
	if (!root.empty()) {
		const std::wstring candidates[] = {
			root + L"\\appcache\\librarycache\\" + id + L"_library_600x900.jpg",       // ancienne disposition
			root + L"\\appcache\\librarycache\\" + id + L"\\library_600x900.jpg",      // nouvelle disposition
		};
		for (const auto& c : candidates)
			if (FileExists(c) && CopyFileW(c.c_str(), dst.c_str(), FALSE)) return file;
	}
	const std::wstring url = L"https://cdn.cloudflare.steamstatic.com/steam/apps/" + id + L"/library_600x900.jpg";
	if (SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), dst.c_str(), 0, nullptr)) && FileExists(dst)) return file;
	DeleteFileW(dst.c_str());
	return {};
}

// Copie une image choisie par l'utilisateur dans le dossier des jaquettes ; renvoie son nom de fichier.
inline std::string ImportImage(const std::string& id, const std::wstring& source) {
	const size_t dot = source.find_last_of(L'.');
	std::wstring ext = dot == std::wstring::npos ? L".jpg" : source.substr(dot);
	for (auto& c : ext) c = (wchar_t)towlower(c);
	if (ext != L".jpg" && ext != L".jpeg" && ext != L".png" && ext != L".webp") return {};
	// Nom nouveau à chaque import : l'interface ne garde pas l'ancienne image en cache.
	const std::string file = SafeFileName(id) + "_" + std::to_string(GetTickCount64()) + Narrow(ext);
	return CopyFileW(source.c_str(), (CoversDir() + L"\\" + Widen(file)).c_str(), FALSE) ? file : std::string();
}

} // namespace sg
