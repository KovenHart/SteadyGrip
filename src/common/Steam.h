// SteadyGrip - reconnaissance des jeux Steam (lecture seule des fichiers de Steam).
//
// Steam range chaque jeu dans <bibliothèque>\steamapps\common\<dossier>, et décrit chaque jeu installé dans
// <bibliothèque>\steamapps\appmanifest_<numéro>.acf (champs « appid », « name », « installdir »). Les
// bibliothèques sont listées dans <Steam>\steamapps\libraryfolders.vdf.
#pragma once

#ifdef _WIN32
#include "Shared.h" // Widen, Narrow, windows.h
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace sg {

inline bool _stricmp_portable(const std::string& a, const char* b) {
	size_t i = 0;
	for (; i < a.size() && b[i]; i++) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
		if (x != y) return false;
	}
	return i == a.size() && !b[i];
}

// Paires « clé » « valeur » d'un fichier VDF/ACF, dans l'ordre (les blocs { } sont ignorés : on aplatit).
inline std::vector<std::pair<std::string, std::string>> ParseVdf(const std::string& text) {
	std::vector<std::pair<std::string, std::string>> out;
	std::vector<std::string> tokens;
	auto flush = [&] {
		if (tokens.size() == 2) out.emplace_back(tokens[0], tokens[1]);
		tokens.clear();
	};
	for (size_t i = 0; i < text.size(); i++) {
		const char c = text[i];
		if (c == '\n' || c == '{' || c == '}') { flush(); continue; }
		if (c != '"') continue;
		std::string tok;
		for (i++; i < text.size() && text[i] != '"'; i++) {
			if (text[i] == '\\' && i + 1 < text.size()) {
				i++;
				tok += text[i] == 'n' ? '\n' : text[i]; // \\ -> \ , \" -> "
			} else {
				tok += text[i];
			}
		}
		tokens.push_back(tok);
	}
	flush();
	return out;
}

inline std::string VdfGet(const std::vector<std::pair<std::string, std::string>>& kv, const char* key) {
	for (const auto& [k, v] : kv)
		if (_stricmp_portable(k, key)) return v;
	return {};
}

inline std::string LowerAscii(std::string s) {
	for (auto& c : s) c = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
	return s;
}

// Découpe un chemin de jeu Steam : « D:\SteamLibrary\steamapps\common\CrossFire Sierra Squad\Game.exe »
// -> racine « D:\SteamLibrary », dossier « CrossFire Sierra Squad ». Faux si ce n'est pas un jeu Steam.
inline bool SplitSteamPath(const std::string& exePath, std::string& libraryRoot, std::string& installDir) {
	std::string norm = exePath;
	std::replace(norm.begin(), norm.end(), '/', '\\');
	const std::string low = LowerAscii(norm);
	const std::string key = "\\steamapps\\common\\";
	const size_t p = low.rfind(key);
	if (p == std::string::npos) return false;
	libraryRoot = norm.substr(0, p);
	const size_t start = p + key.size();
	const size_t end = norm.find('\\', start);
	installDir = norm.substr(start, end == std::string::npos ? std::string::npos : end - start);
	return !installDir.empty();
}

struct SteamGame {
	uint32_t appId = 0;
	std::string name;        // UTF-8
	std::string installDir;  // dossier sous steamapps\common
	std::string libraryRoot; // bibliothèque Steam
};

inline SteamGame ParseManifest(const std::string& text, const std::string& libraryRoot) {
	const auto kv = ParseVdf(text);
	SteamGame g;
	g.appId = (uint32_t)std::strtoul(VdfGet(kv, "appid").c_str(), nullptr, 10);
	g.name = VdfGet(kv, "name");
	g.installDir = VdfGet(kv, "installdir");
	g.libraryRoot = libraryRoot;
	return g;
}

// Dossiers des bibliothèques Steam listés dans libraryfolders.vdf (clés « path »).
inline std::vector<std::string> ParseLibraryFolders(const std::string& text) {
	std::vector<std::string> out;
	for (const auto& [k, v] : ParseVdf(text))
		if (_stricmp_portable(k, "path") && !v.empty()) out.push_back(v);
	return out;
}

#ifdef _WIN32
// --- Accès aux fichiers de Steam (Windows) -------------------------------------------------------
inline std::string ReadSmallFile(const std::wstring& path) {
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) return {};
	std::string data;
	LARGE_INTEGER size{};
	if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (8 << 20)) {
		data.resize((size_t)size.QuadPart);
		DWORD got = 0;
		ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr);
		data.resize(got);
	}
	CloseHandle(f);
	return data;
}

// Dossier d'installation de Steam (registre de l'utilisateur), vide si Steam est absent.
inline std::wstring SteamRoot() {
	wchar_t buf[MAX_PATH] = L"";
	DWORD size = sizeof(buf);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) {
		size = sizeof(buf);
		if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
			return {};
	}
	std::wstring p = buf;
	std::replace(p.begin(), p.end(), L'/', L'\\');
	return p;
}

inline std::vector<std::wstring> SteamLibraries() {
	std::vector<std::wstring> out;
	const std::wstring root = SteamRoot();
	if (root.empty()) return out;
	out.push_back(root);
	for (const auto& p : ParseLibraryFolders(ReadSmallFile(root + L"\\steamapps\\libraryfolders.vdf"))) {
		std::wstring w = Widen(p);
		std::replace(w.begin(), w.end(), L'/', L'\\');
		if (_wcsicmp(w.c_str(), root.c_str()) != 0) out.push_back(w);
	}
	return out;
}

// Jeux décrits par les fichiers appmanifest_*.acf d'une bibliothèque.
inline std::vector<SteamGame> SteamGamesIn(const std::wstring& library) {
	std::vector<SteamGame> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((library + L"\\steamapps\\appmanifest_*.acf").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		SteamGame g = ParseManifest(ReadSmallFile(library + L"\\steamapps\\" + fd.cFileName), Narrow(library));
		if (g.appId) out.push_back(g);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return out;
}

inline std::vector<SteamGame> InstalledSteamGames() {
	std::vector<SteamGame> out;
	for (const auto& lib : SteamLibraries())
		for (auto& g : SteamGamesIn(lib)) out.push_back(g);
	return out;
}

// Numéro Steam du jeu dont le programme est exePath (UTF-8), 0 si ce n'est pas un jeu Steam.
inline uint32_t SteamAppIdForExe(const std::string& exePath) {
	std::string root, dir;
	if (!SplitSteamPath(exePath, root, dir)) return 0;
	for (const auto& g : SteamGamesIn(Widen(root)))
		if (LowerAscii(g.installDir) == LowerAscii(dir)) return g.appId;
	return 0;
}
#endif

} // namespace sg
