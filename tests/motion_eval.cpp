// Évalue les à-coups pendant les déplacements de l'arme : rejoue une session TrackProbe à 72 Hz à travers
// le filtre (position) et la stabilisation de l'orientation, et mesure l'irrégularité du mouvement d'une
// image à l'autre (écart entre le pas d'une image et la moyenne des pas voisins), quand la manette bouge.
//   motion_eval session.csv
#include "../src/common/PoseFilter.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace sg;

struct Row { double t; int dev; unsigned flags; V3 p; Q q; V3 v, w; };

static double Pct(std::vector<double> v, double p) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t)(p * v.size()))];
}

int main(int argc, char** argv) {
	std::ifstream f(argv[1]);
	std::string line;
	std::getline(f, line);
	std::vector<Row> R;
	while (std::getline(f, line)) {
		double v[17]; char c; std::stringstream ss(line);
		for (int i = 0; i < 17; i++) { ss >> v[i]; ss >> c; }
		Row r{ v[0], (int)v[1], (unsigned)v[2], { v[3], v[4], v[5] }, { v[6], v[7], v[8], v[9] }, { v[10], v[11], v[12] }, { v[13], v[14], v[15] } };
		if (r.dev == 2) R.push_back(r);
	}
	struct Cfg { const char* name; double still, moving, orient; };
	const Cfg cfgs[] = { { "brut (sans SteadyGrip)", -1, 0, 0 }, { "tes réglages (150 ms, 6 ms, orientation 100)", 150, 6, 100 },
		{ "orientation 50", 150, 6, 50 }, { "orientation 0", 150, 6, 0 }, { "mouvement 12 ms, orientation 100", 150, 12, 100 },
		{ "mouvement 12 ms, orientation 50", 150, 12, 50 }, { "mouvement 20 ms", 150, 20, 100 }, { "mouvement 30 ms", 150, 30, 100 }, { "préréglage Moyen", 60, 6, 50 } };
	std::printf("Manette droite, 72 Hz, images où la manette bouge (0,15 à 1,5 m/s)\n");
	std::printf("%-46s | à-coups de position (mm/image) | à-coups d'orientation (°/image)\n", "");
	for (const Cfg& c : cfgs) {
		FilterSettings s = Preset(2);
		s.enabled = c.still > 0;
		if (s.enabled) { s.stillTauMs = c.still; s.movingTauMs = c.moving; }
		PoseFilter filt;
		OrientSmoother sm;
		std::vector<V3> P;
		std::vector<Q> Qs;
		std::vector<double> speed;
		size_t idx = 0;
		for (double t = R.front().t; t < R.back().t; t += 1.0 / 72) {
			while (idx + 1 < R.size() && R[idx + 1].t <= t) idx++;
			const Row& r = R[idx];
			PoseIn in;
			in.t = t; in.p = r.p; in.q = r.q; in.v = r.v; in.w = r.w;
			in.poseValid = (r.flags & 3) == 3; in.velValid = true;
			V3 op; Q oq;
			filt.process(in, s, op, oq);
			if (!s.enabled) { op = r.p; oq = r.q; }
			oq = sm.apply(t, oq, c.orient);
			P.push_back(op); Qs.push_back(oq); speed.push_back(r.v.norm());
		}
		std::vector<double> jp, jq, sg;
		for (size_t i = 2; i + 2 < P.size(); i++) {
			if (speed[i] < 0.15 || speed[i] > 1.5) continue;
			const V3 fit = (P[i - 2] * -3.0 + P[i - 1] * 12.0 + P[i] * 17.0 + P[i + 1] * 12.0 + P[i + 2] * -3.0) * (1.0 / 35.0);
			sg.push_back((P[i] - fit).norm() * 1000);
		}
		for (size_t i = 4; i + 4 < P.size(); i++) {
			if (speed[i] < 0.15 || speed[i] > 1.5) continue;
			// pas de l'image i, comparé à la moyenne des pas i-3..i+3
			V3 avg; V3 avgR;
			for (int k = -3; k <= 3; k++) {
				avg = avg + (P[i + k] - P[i + k - 1]);
				Q d = Qs[i + k] * Qs[i + k - 1].conj(); if (d.w < 0) d = Q{ -d.w, -d.x, -d.y, -d.z };
				avgR = avgR + qlog(d);
			}
			avg = avg * (1.0 / 7); avgR = avgR * (1.0 / 7);
			Q d = Qs[i] * Qs[i - 1].conj(); if (d.w < 0) d = Q{ -d.w, -d.x, -d.y, -d.z };
			jp.push_back(((P[i] - P[i - 1]) - avg).norm() * 1000);
			jq.push_back((qlog(d) - avgR).norm() * 57.2958);
		}
		double sgMean = 0; for (double x : sg) sgMean += x; sgMean /= std::max<size_t>(1, sg.size());
		std::printf("%-46s | méd %.2f  95e %.2f  max %5.1f   | méd %.3f  95e %.3f  max %.2f | journal : %.2f mm\n", c.name, Pct(jp, .5), Pct(jp, .95), Pct(jp, 1), Pct(jq, .5), Pct(jq, .95), Pct(jq, 1), sgMean);
	}
	return 0;
}
