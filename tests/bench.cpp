// Banc de test : rejoue un CSV TrackProbe à la fréquence d'affichage (72 ou 90 Hz) à travers le filtre
// et compare la pose brute et la pose filtrée.
//   bench <fichier.csv> [hz] [preset 0-3] [t_debut] [t_fin]
#include "../src/common/PoseFilter.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace sg;

struct Row { double t; int dev; unsigned flags; V3 p; Q q; V3 v, w; };

// « Saut visible » d'une trajectoire à la fréquence d'affichage : écart entre la pose d'une image et la
// pose extrapolée depuis l'image précédente avec la vitesse brute (identique pour brut et filtré).
struct StepStats {
	std::vector<double> mm, deg;
	void add(double a, double b) { mm.push_back(a); deg.push_back(b); }
	static double pct(std::vector<double> v, double p) {
		if (v.empty()) return 0;
		std::sort(v.begin(), v.end());
		return v[std::min(v.size() - 1, (size_t)(p * v.size()))];
	}
	int over(double th) const { return (int)std::count_if(mm.begin(), mm.end(), [&](double x) { return x > th; }); }
};

int main(int argc, char** argv) {
	if (argc < 2) { std::printf("bench fichier.csv [hz] [preset] [t0] [t1]\n"); return 1; }
	const double hz = argc > 2 ? std::atof(argv[2]) : 90;
	const int preset = argc > 3 ? std::atoi(argv[3]) : 2;
	const bool noVel = getenv("SANS_VITESSE") != nullptr;
	const double t0 = argc > 4 ? std::atof(argv[4]) : 0, t1 = argc > 5 ? std::atof(argv[5]) : 1e9;
	const FilterSettings s = Preset(preset);

	std::ifstream f(argv[1]);
	std::string line;
	std::getline(f, line);
	std::vector<Row> rows[3];
	while (std::getline(f, line)) {
		double v[17]; char c; std::stringstream ss(line);
		for (int i = 0; i < 17; i++) { ss >> v[i]; ss >> c; }
		Row r{ v[0], (int)v[1], (unsigned)v[2], { v[3], v[4], v[5] }, { v[6], v[7], v[8], v[9] }, { v[10], v[11], v[12] }, { v[13], v[14], v[15] } };
		if (r.dev >= 0 && r.dev < 3) rows[r.dev].push_back(r);
	}

	const char* names[3] = { "Casque", "Manette G", "Manette D" };
	for (int dev = 1; dev < 3; dev++) {
		const auto& R = rows[dev];
		if (R.size() < 100) continue;
		PoseFilter filt;
		StepStats raw, out;
		std::vector<double> devMm, devDeg;
		size_t idx = 0;
		bool havePrev = false;
		V3 prevRaw, prevOut, prevV;
		Q prevRawQ, prevOutQ;
		double prevT = 0;
		int gapFrames = 0;
		for (double t = R.front().t; t < R.back().t; t += 1.0 / hz) {
			while (idx + 1 < R.size() && R[idx + 1].t <= t) idx++;
			const Row& r = R[idx];
			PoseIn in;
			in.t = r.t; in.p = r.p; in.q = r.q; in.v = r.v; in.w = r.w;
			in.poseValid = (r.flags & 3) == 3; in.velValid = !noVel;
			V3 op; Q oq;
			filt.process(in, s, op, oq);
			if (r.t < t0 || r.t > t1) { havePrev = false; continue; }
			if (filt.stats().outOfView) gapFrames++;
			devMm.push_back((op - r.p).norm() * 1000);
			devDeg.push_back(qlog(oq * r.q.conj()).norm() * 57.2958);
			if (havePrev && r.t > prevT) {
				const double dt = r.t - prevT;
				const V3 dp = (prevV + r.v) * (0.5 * dt);
				raw.add((r.p - (prevRaw + dp)).norm() * 1000, 0);
				out.add((op - (prevOut + dp)).norm() * 1000, 0);
			}
			prevRaw = r.p; prevOut = op; prevV = r.v; prevRawQ = r.q; prevOutQ = oq; prevT = r.t; havePrev = true;
		}
		std::printf("%s (%.0f Hz, préréglage %d)\n", names[dev], hz, preset);
		std::printf("  sauts par image  brut   : méd %.2f  p95 %.2f  p99 %.2f  max %6.1f mm | >1 mm %4d  >3 mm %4d  >10 mm %3d\n",
			StepStats::pct(raw.mm, .5), StepStats::pct(raw.mm, .95), StepStats::pct(raw.mm, .99), StepStats::pct(raw.mm, 1), raw.over(1), raw.over(3), raw.over(10));
		std::printf("                   filtré : méd %.2f  p95 %.2f  p99 %.2f  max %6.1f mm | >1 mm %4d  >3 mm %4d  >10 mm %3d\n",
			StepStats::pct(out.mm, .5), StepStats::pct(out.mm, .95), StepStats::pct(out.mm, .99), StepStats::pct(out.mm, 1), out.over(1), out.over(3), out.over(10));
		std::printf("  écart filtré/brut      : méd %.2f  p95 %.2f  max %.1f mm | méd %.3f p95 %.3f max %.2f deg\n",
			StepStats::pct(devMm, .5), StepStats::pct(devMm, .95), StepStats::pct(devMm, 1),
			StepStats::pct(devDeg, .5), StepStats::pct(devDeg, .95), StepStats::pct(devDeg, 1));
		std::printf("  recalages %llu, téléportations étalées %llu, images « hors champ » %d\n",
			(unsigned long long)filt.stats().corrections, (unsigned long long)filt.stats().teleports, gapFrames);
	}
	return 0;
}
