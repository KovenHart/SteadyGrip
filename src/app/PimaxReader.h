// SteadyGrip - lecture en temps réel de l'état de suivi des manettes noté par le service Pimax.
//
// Le service Pimax écrit chaque changement d'état (« ctrl: 1 Switch from Tracking to Lost ») dans son
// journal C:\ProgramData\Pimax\slam\controller_*.txt, mais ce fichier n'est écrit sur disque que par
// paquets. On lit donc ces lignes dans le tampon d'écriture, en mémoire du processus Pimax, dès qu'elles
// y apparaissent. LECTURE SEULE : rien n'est écrit chez Pimax, aucun code n'est chargé dans ses processus.
// Nécessite les droits d'administrateur (le service Pimax tourne avec des droits élevés).
//
// Mises à jour de Pimax : la version du service et du module de suivi est comparée à la dernière version
// où la lecture a été vérifiée (lignes vues en mémoire puis retrouvées dans le fichier). Si le journal
// change de format ou ne peut plus être lu, la source passe à « incompatible » : la couche cesse alors
// d'utiliser le signal de perte (le lissage reste actif), au lieu d'appliquer des états faux.
//
// Les états sont déposés dans la mémoire partagée, où la couche OpenXR les lit pendant le jeu.
#pragma once

#include "../common/LogBuffer.h"
#include "../common/Shared.h"

#include <psapi.h>
#include <winver.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <atomic>
#include <filesystem>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")

namespace sg {

class PimaxReader {
public:
	~PimaxReader() { stop(); }

	void start(SharedBlock* block) {
		block_ = block;
		stop_ = false;
		thread_ = std::thread([this] { run(); });
	}

	void stop() {
		stop_ = true;
		if (thread_.joinable()) thread_.join();
		if (block_) {
			InterlockedExchange(&block_->trackSource, SourceOff);
			for (auto& t : block_->track) InterlockedExchange(&t.state, TrackUnknown);
		}
	}

private:
	SharedBlock* block_ = nullptr;
	std::atomic<bool> stop_{ false };
	std::thread thread_;

	void setSource(TrackSource s) { InterlockedExchange(&block_->trackSource, s); }

	void setState(int ctrl, int state) {
		if (ctrl < 0 || ctrl > 1) return;
		SharedTrack& t = block_->track[ctrl]; // manette 0 du journal = gauche (vérifié avec LostWatch)
		if (t.state == state) return;
		t.changed = NowSeconds();
		t.transitions++;
		MemoryBarrier();
		InterlockedExchange(&t.state, state);
	}

	static int StateFromName(const std::string& n) {
		if (n == "Tracking") return TrackTracking;
		if (n == "Lost") return TrackLost;
		if (n == "Initing") return TrackIniting;
		return TrackUnknown;
	}

	static void EnableDebugPrivilege() {
		HANDLE tok;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return;
		TOKEN_PRIVILEGES tp{};
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid)) AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
		CloseHandle(tok);
	}

	// Processus qui a chargé le module de suivi des manettes (normalement pi_server.exe).
	static HANDLE OpenPimax(bool& denied, bool& noModule) {
		denied = false;
		noModule = false;
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE) return nullptr;
		PROCESSENTRY32W pe{ sizeof(pe) };
		HANDLE found = nullptr;
		for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
			HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pe.th32ProcessID);
			if (!h) {
				if (_wcsicmp(pe.szExeFile, L"pi_server.exe") == 0) denied = true;
				continue;
			}
			HMODULE mods[1024];
			DWORD needed = 0;
			if (EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
				const DWORD n = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
				for (DWORD i = 0; i < n && !found; i++) {
					wchar_t name[MAX_PATH];
					if (GetModuleBaseNameW(h, mods[i], name, MAX_PATH) && _wcsicmp(name, L"LedObjectPoseEstimator.dll") == 0) found = h;
				}
			}
			if (!found) {
				if (_wcsicmp(pe.szExeFile, L"pi_server.exe") == 0) noModule = true;
				CloseHandle(h);
			}
		}
		CloseHandle(snap);
		return found;
	}

	// Version d'un fichier (ressource de version, sinon date de modification).
	static std::wstring FileVersion(const std::wstring& path) {
		DWORD dummy = 0;
		const DWORD n = GetFileVersionInfoSizeW(path.c_str(), &dummy);
		if (n) {
			std::string buf(n, '\0');
			VS_FIXEDFILEINFO* fi = nullptr;
			UINT len = 0;
			if (GetFileVersionInfoW(path.c_str(), 0, n, buf.data()) && VerQueryValueW(buf.data(), L"\\", (LPVOID*)&fi, &len) && fi) {
				wchar_t v[64];
				swprintf_s(v, L"%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
				return v;
			}
		}
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
			SYSTEMTIME st;
			FileTimeToSystemTime(&fad.ftLastWriteTime, &st);
			wchar_t v[64];
			swprintf_s(v, L"du %04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
			return v;
		}
		return L"?";
	}

	// « pi_server 1.2.3.4, module 5.6.7.8 » : identifie la version de Pimax en service.
	static std::wstring PimaxVersion(HANDLE h) {
		wchar_t exe[MAX_PATH] = L"", dll[MAX_PATH] = L"";
		GetModuleFileNameExW(h, nullptr, exe, MAX_PATH);
		HMODULE mods[1024];
		DWORD needed = 0;
		if (EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
			const DWORD n = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
			for (DWORD i = 0; i < n; i++) {
				wchar_t name[MAX_PATH];
				if (GetModuleBaseNameW(h, mods[i], name, MAX_PATH) && _wcsicmp(name, L"LedObjectPoseEstimator.dll") == 0) {
					GetModuleFileNameExW(h, mods[i], dll, MAX_PATH);
					break;
				}
			}
		}
		return L"pi_server " + FileVersion(exe) + L", module de suivi " + FileVersion(dll);
	}

	void publishVersion(const std::wstring& v) {
		char u[sizeof(block_->pimaxVersion)];
		WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, u, sizeof(u), nullptr, nullptr);
		u[sizeof(u) - 1] = 0;
		memcpy(block_->pimaxVersion, u, sizeof(u));
	}

	void setCheck(PimaxCheck c) { InterlockedExchange(&block_->pimaxCheck, c); }

	struct Found {
		uintptr_t addr = 0;
		size_t len = 0;
		int lines = 0;
		uintptr_t regionEnd = 0;
	};

	// Zone de texte qui contient le plus de lignes du journal autour d'un « Switch from ».
	bool scan(HANDLE h, Found& best) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		uintptr_t a = (uintptr_t)si.lpMinimumApplicationAddress;
		const uintptr_t maxA = (uintptr_t)si.lpMaximumApplicationAddress;
		const std::string needle = " Switch from ";
		const size_t kChunk = 8u << 20, kOverlap = 64u << 10;
		std::string chunk;
		best = Found{};
		MEMORY_BASIC_INFORMATION mbi;
		while (a < maxA && !stop_ && VirtualQueryEx(h, (LPCVOID)a, &mbi, sizeof(mbi)) == sizeof(mbi)) {
			const uintptr_t base = (uintptr_t)mbi.BaseAddress, size = mbi.RegionSize;
			const bool readable = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) &&
				!(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && size <= (1024ull << 20);
			if (readable) {
				for (uintptr_t off = 0; off < size && !stop_;) {
					const size_t n = (size_t)std::min<uintptr_t>(kChunk, size - off);
					chunk.resize(n);
					SIZE_T got = 0;
					ReadProcessMemory(h, (LPCVOID)(base + off), chunk.data(), n, &got);
					chunk.resize(got);
					size_t skipUntil = 0;
					for (size_t f = chunk.find(needle); f != std::string::npos; f = chunk.find(needle, f + 1)) {
						if (f < skipUntil) continue;
						const lw::Block b = lw::BlockAround(chunk, f);
						skipUntil = b.end;
						if (b.lines > best.lines) best = { base + off + b.start, b.end - b.start, b.lines, base + size };
					}
					if (off + n >= size) break;
					off += n - kOverlap;
				}
			}
			a = base + size;
		}
		return best.lines >= 2;
	}

	static std::filesystem::path NewestLog() {
		namespace fs = std::filesystem;
		fs::path best;
		fs::file_time_type bestTime{};
		std::error_code ec;
		for (const auto& e : fs::directory_iterator("C:\\ProgramData\\Pimax\\slam", ec)) {
			const auto name = e.path().filename().string();
			if (name.rfind("controller_2", 0) != 0 || e.path().extension() != ".txt") continue;
			const auto t = e.last_write_time(ec);
			if (best.empty() || t > bestTime) { best = e.path(); bestTime = t; }
		}
		return best;
	}

	void run() {
		EnableDebugPrivilege();
		timeBeginPeriod(1); // Sleep(1) = 1 ms au lieu de 15 ms
		static const std::regex re(R"(ctrl: (\d+) Switch from (\w+) to (\w+))");
		HANDLE proc = nullptr;
		uintptr_t B = 0;
		size_t readLen = 0;
		lw::Tracker tracker;
		double lastOpen = -10, lastScan = -10, lastFile = 0;
		std::string w;
		// Vérification : chaque ligne qui arrive dans le fichier doit avoir été vue d'abord en mémoire.
		std::unordered_map<std::string, int> seen;
		std::filesystem::path logPath;
		HANDLE logFile = INVALID_HANDLE_VALUE;
		LONGLONG logOffset = 0;
		std::string pending;
		int missStreak = 0;
		long long startTs = 0;
		// Vérification de la version et détection d'un changement de format du journal.
		std::wstring version;
		int unknownStates = 0;        // lignes « Switch from » avec un état inconnu
		int fruitlessRescans = 0;     // nouvelles recherches sans aucune ligne retrouvée entre-temps
		int fileLossLinesUnseen = 0;  // lignes de perte dans le fichier alors que le tampon reste introuvable
		bool incompatible = false;
		uint64_t matched = 0;
		auto markIncompatible = [&] {
			if (incompatible) return;
			incompatible = true;
			B = 0;
			for (int c = 0; c < 2; c++) setState(c, TrackUnknown);
			setSource(SourceIncompatible);
		};

		while (!stop_) {
			const double now = NowSeconds();
			block_->trackHeartbeat = now;
			if (!proc && now - lastOpen > 2) {
				lastOpen = now;
				bool denied = false, noModule = false;
				proc = OpenPimax(denied, noModule);
				if (!proc) setSource(denied ? SourceDenied : noModule ? SourceNoModule : SourceNoPimax);
				else {
					version = PimaxVersion(proc);
					publishVersion(version);
					const bool known = version == IniGetStr(L"Pimax", L"VersionVerifiee");
					setCheck(known ? CheckValidated : CheckVerifying);
					unknownStates = fruitlessRescans = fileLossLinesUnseen = 0;
					matched = 0;
					incompatible = false;
					setSource(SourceSearching);
				}
			}
			if (proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) { // service Pimax arrêté
				CloseHandle(proc);
				proc = nullptr;
				B = 0;
				incompatible = false;
				for (int c = 0; c < 2; c++) setState(c, TrackUnknown);
				setSource(SourceNoPimax);
			}
			if (proc && !B && now - lastScan > (incompatible ? 30 : 3)) {
				lastScan = now;
				Found f;
				if (scan(proc, f)) {
					B = f.addr;
					readLen = (size_t)std::min<uintptr_t>(std::clamp<size_t>(f.len * 2 + 4096, 16384, 131072), f.regionEnd - B);
					tracker = lw::Tracker();
					w.resize(readLen);
					SIZE_T got = 0;
					ReadProcessMemory(proc, (LPCVOID)B, w.data(), readLen, &got);
					w.resize(got);
					tracker.Update(w);
					startTs = tracker.InitMaxTs();
					seen.clear();
					missStreak = 0;
					setSource(SourceActive);
				}
			}
			if (proc && B) {
				w.resize(readLen);
				SIZE_T got = 0;
				ReadProcessMemory(proc, (LPCVOID)B, w.data(), readLen, &got);
				if (got == 0) {
					B = 0;
					setSource(SourceSearching);
				} else {
					w.resize(got);
					for (const auto& l : tracker.Update(w)) {
						std::smatch m;
						if (!std::regex_search(l.msg, m, re)) continue;
						setState(std::stoi(m[1]), StateFromName(m[3]));
						seen[l.key]++;
					}
				}
			}
			// Contrôle par le fichier, une fois par seconde : 3 lignes manquées de suite -> nouvelle recherche.
			if (now - lastFile > 1.0) {
				lastFile = now;
				const auto newest = NewestLog();
				if (!newest.empty() && newest != logPath) {
					if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
					logFile = CreateFileW(newest.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
						OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
					logPath = newest;
					LARGE_INTEGER size{};
					if (logFile != INVALID_HANDLE_VALUE) GetFileSizeEx(logFile, &size);
					logOffset = size.QuadPart;
					pending.clear();
				}
				LARGE_INTEGER size{};
				if (logFile != INVALID_HANDLE_VALUE && GetFileSizeEx(logFile, &size) && size.QuadPart > logOffset) {
					std::string buf((size_t)(size.QuadPart - logOffset), '\0');
					LARGE_INTEGER pos;
					pos.QuadPart = logOffset;
					SetFilePointerEx(logFile, pos, nullptr, FILE_BEGIN);
					DWORD read = 0;
					ReadFile(logFile, buf.data(), (DWORD)buf.size(), &read, nullptr);
					buf.resize(read);
					logOffset += read;
					pending += buf;
					size_t consumed = 0;
					for (const auto& l : lw::SplitLines(pending, consumed)) {
						const bool isSwitch = l.msg.find("Switch from") != std::string::npos;
						// Tampon introuvable alors que le fichier note des pertes : le format a sans doute changé.
						if (!B && proc && (l.msg.find("Lost") != std::string::npos || l.msg.find("Initing") != std::string::npos)) {
							if (++fileLossLinesUnseen >= 4) markIncompatible();
						}
						if (!B || l.ts <= startTs || !isSwitch) continue;
						auto it = seen.find(l.key);
						if (it != seen.end()) {
							if (--it->second <= 0) seen.erase(it);
							missStreak = 0;
							fruitlessRescans = 0;
							fileLossLinesUnseen = 0;
							matched++;
							if (incompatible && unknownStates < 3) { // la lecture refonctionne
								incompatible = false;
								setSource(SourceActive);
							}
							InterlockedIncrement64((volatile LONG64*)&block_->matchedLines);
							// Nouvelle version de Pimax : la lecture est vérifiée dès que deux lignes concordent.
							if (matched >= 2 && block_->pimaxCheck == CheckVerifying) {
								IniSetStr(L"Pimax", L"VersionVerifiee", version);
								setCheck(CheckNewValidated);
							}
						} else if (++missStreak >= 3) {
							B = 0; // le tampon a changé de place : on le cherche à nouveau
							missStreak = 0;
							if (++fruitlessRescans >= 3) markIncompatible(); // jamais retrouvé : la lecture ne fonctionne plus
							else setSource(SourceSearching);
						}
					}
					pending.erase(0, consumed);
				}
			}
			Sleep(1);
		}
		if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
		if (proc) CloseHandle(proc);
		timeEndPeriod(1);
	}
};

} // namespace sg
