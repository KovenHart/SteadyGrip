// Simulation du rythme de demandes de Unity : chaque heure d'affichage est demandée deux fois
// (une fois pour l'image suivante, puis pour l'image en cours avec des capteurs plus frais).
#include "../src/common/PoseFilter.h"
#include <cstdio>
#include <random>
#include <vector>
using namespace sg;
static V3 truth(double t) { return { 0.25 * std::sin(2.1 * t), 0.12 * std::sin(3.3 * t + 1), 0.08 * std::cos(1.7 * t) }; }
static V3 vel(double t) { return { 0.25 * 2.1 * std::cos(2.1 * t), 0.12 * 3.3 * std::cos(3.3 * t + 1), -0.08 * 1.7 * std::sin(1.7 * t) }; }
int main(int argc, char** argv) {
	const bool unity = argc > 1 && argv[1][0] == 'u';
	std::mt19937 rng(7);
	std::normal_distribution<double> n(0, 1);
	FilterSettings s; s.stillTauMs = 100; s.movingTauMs = 20; s.teleportBlendMs = 70;
	PoseFilter f;
	const double P = 1.0 / 72;
	std::vector<V3> raw, out;
	V3 err{};
	double nextOpt = 0;
	auto errAt = [&](double t) { while (t >= nextOpt) { err = V3{ n(rng), n(rng), n(rng) } * 0.0012; nextOpt += 0.04; } return err; };
	for (int k = 1; k < 72 * 60; k++) {
		const double D = k * P;
		V3 o; Q q;
		PoseIn in; in.poseValid = in.velValid = true; in.q = Q{};
		if (unity) { // demande « image suivante » : prédiction plus lointaine, plus bruitée
			in.t = D; in.p = truth(D) + errAt(D - 0.02) + V3{ n(rng), n(rng), n(rng) } * 0.0008; in.v = vel(D);
			f.process(in, s, o, q);
		}
		// demande « image en cours », celle qui est affichée
		in.t = D; in.p = truth(D) + errAt(D) + V3{ n(rng), n(rng), n(rng) } * 0.0003; in.v = vel(D);
		f.process(in, s, o, q);
		raw.push_back(in.p); out.push_back(o);
	}
	auto jerk = [](const std::vector<V3>& w) {
		double sum = 0; int c = 0;
		for (size_t i = 2; i + 2 < w.size(); i++) {
			const V3 fit = (w[i - 2] * -3.0 + w[i - 1] * 12.0 + w[i] * 17.0 + w[i + 1] * 12.0 + w[i + 2] * -3.0) * (1.0 / 35.0);
			sum += (w[i] - fit).norm() * 1000; c++;
		}
		return sum / c;
	};
	std::printf("%s : à-coups brut %.3f mm/image, filtré %.3f (%+.0f %%)\n", unity ? "Unity (2 demandes)" : "1 demande/image",
		jerk(raw), jerk(out), 100 * (jerk(out) / jerk(raw) - 1));
}
