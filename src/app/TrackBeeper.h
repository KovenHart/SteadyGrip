#pragma once
// Bips de suivi (option, Paramètres) : un bip grave quand Pimax perd une manette, un bip aigu quand il la
// retrouve. Le son vient du côté de la manette (gauche ou droite). Joué par l'application compagnon, hors du
// jeu : aucun effet sur les images. Seulement pendant qu'un jeu tourne avec SteadyGrip.
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>
#include "../common/Shared.h"

namespace sg {

class TrackBeeper {
public:
	// Délais : une perte de moins de 0,2 s ne fait pas de bip (Pimax clignote parfois) ; le bip de retour
	// attend 0,15 s de suivi continu, et n'est joué qu'après un bip de perte.
	static constexpr double kLostDelayS = 0.20, kBackDelayS = 0.15;

	void start(SharedBlock* b) {
		block_ = b;
		for (int hand = 0; hand < 2; hand++) {
			MakeWav(lost_[hand], 330.0, hand);
			MakeWav(back_[hand], 880.0, hand);
		}
		stop_ = false;
		thread_ = std::thread([this] { run(); });
	}
	void stop() {
		stop_ = true;
		if (thread_.joinable()) thread_.join();
		PlaySoundW(nullptr, nullptr, 0);
	}
	void setEnabled(bool on) { enabled_ = on; }
	// Bouton « Écouter » : bip grave puis aigu, à gauche puis à droite.
	void test() { test_ = true; }

	// Son stéréo 16 bits 44,1 kHz : 120 ms, attaque et extinction douces ; côté de la manette plus fort.
	static void MakeWav(std::vector<uint8_t>& out, double freq, int hand) {
		const uint32_t rate = 44100, n = rate * 120 / 1000;
		const uint32_t dataBytes = n * 4;
		out.assign(44 + dataBytes, 0);
		auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); };
		auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&out[at], &v, 2); };
		std::memcpy(&out[0], "RIFF", 4); put32(4, 36 + dataBytes); std::memcpy(&out[8], "WAVEfmt ", 8);
		put32(16, 16); put16(20, 1); put16(22, 2); put32(24, rate); put32(28, rate * 4); put16(32, 4); put16(34, 16);
		std::memcpy(&out[36], "data", 4); put32(40, dataBytes);
		const double near = 0.30, far = 0.08;   // volume du côté de la manette / de l'autre côté
		for (uint32_t i = 0; i < n; i++) {
			const double t = (double)i / rate, env = std::min({ 1.0, t / 0.01, (0.12 - t) / 0.03 });
			const double v = std::sin(2 * 3.14159265358979 * freq * t) * std::max(0.0, env);
			const int16_t l = (int16_t)(v * (hand == 0 ? near : far) * 32767), r = (int16_t)(v * (hand == 1 ? near : far) * 32767);
			put16(44 + i * 4, (uint16_t)l);
			put16(44 + i * 4 + 2, (uint16_t)r);
		}
	}

private:
	void play(const std::vector<uint8_t>& w) { PlaySoundW((LPCWSTR)w.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT); }

	void run() {
		double lostSince[2] = { 0, 0 }, trackedSince[2] = { 0, 0 };
		bool beeped[2] = { false, false };
		while (!stop_) {
			Sleep(20);
			if (test_.exchange(false)) {
				for (int hand = 0; hand < 2; hand++) {
					play(lost_[hand]);
					Sleep(350);
					play(back_[hand]);
					Sleep(450);
				}
				continue;
			}
			const double now = NowSeconds();
			const bool gameRunning = block_ && now - block_->heartbeat < 1.0 && block_->trackSource == SourceActive && now - block_->trackHeartbeat < 0.5;
			for (int hand = 0; hand < 2; hand++) {
				const int st = gameRunning ? (int)block_->track[hand].state : TrackUnknown;
				if (!enabled_ || st == TrackUnknown) {
					lostSince[hand] = trackedSince[hand] = 0;
					beeped[hand] = false;
					continue;
				}
				if (st == TrackLost || st == TrackIniting) {
					trackedSince[hand] = 0;
					if (!lostSince[hand]) lostSince[hand] = now;
					if (!beeped[hand] && now - lostSince[hand] >= kLostDelayS) {
						play(lost_[hand]);
						beeped[hand] = true;
					}
				} else {
					lostSince[hand] = 0;
					if (!trackedSince[hand]) trackedSince[hand] = now;
					if (beeped[hand] && now - trackedSince[hand] >= kBackDelayS) {
						play(back_[hand]);
						beeped[hand] = false;
					}
				}
			}
		}
	}

	SharedBlock* block_ = nullptr;
	std::vector<uint8_t> lost_[2], back_[2];
	std::thread thread_;
	std::atomic<bool> stop_{ false }, enabled_{ false }, test_{ false };
};

} // namespace sg
