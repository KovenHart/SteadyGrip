// Évalue la stabilisation de l'orientation sur une session TrackProbe réelle (manette droite, 90 Hz) :
// tremblement image à image quand la manette est presque immobile, et écart à l'orientation brute
// pendant les rotations rapides (retard).
#include "../src/common/PoseFilter.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace sg;

int main(int argc, char** argv) {
	if (argc < 2) { std::printf("orient_eval session.csv [appareil=2]\n"); return 1; }
	const int dev = argc > 2 ? std::atoi(argv[2]) : 2;
	std::ifstream f(argv[1]);
	std::string line;
	std::getline(f, line);
	struct S { double t; Q q; };
	std::vector<S> v;
	double nextT = -1;
	while (std::getline(f, line)) {
		std::stringstream ss(line);
		std::string c[20];
		int n = 0;
		while (n < 20 && std::getline(ss, c[n], ',')) n++;
		if (n < 9 || std::atoi(c[1].c_str()) != dev) continue;
		const double t = std::atof(c[0].c_str());
		if (t < nextT) continue;
		nextT = (nextT < 0 ? t : nextT) + 1 / 90.0;
		v.push_back({ t, Q{ std::atof(c[5].c_str()), std::atof(c[6].c_str()), std::atof(c[7].c_str()), std::atof(c[8].c_str()) } });
	}
	std::printf("%zu images à 90 Hz\n", v.size());
	// Bruit haute fréquence : écart entre une orientation et la moyenne centrée de ses voisines (±5 images,
	// ±55 ms), mesuré quand la moyenne bouge lentement (visée, arme tenue presque immobile).
	auto hfNoise = [](const std::vector<Q>& q, const std::vector<bool>& sel) {
		double sum = 0;
		int n = 0;
		for (size_t i = 5; i + 5 < q.size(); i++) {
			if (!sel[i]) continue;
			V3 m;
			for (int k = -5; k <= 5; k++) {
				Q d = q[i + k] * q[i].conj();
				if (d.w < 0) d = Q{ -d.w, -d.x, -d.y, -d.z };
				m = m + qlog(d);
			}
			sum += (m * (1.0 / 11)).norm();
			n++;
		}
		return n ? sum / n * 180 / 3.14159265 : 0.0;
	};
	std::vector<Q> raw;
	for (auto& s : v) raw.push_back(s.q);
	std::vector<bool> slow(v.size(), false), fast(v.size(), false);
	for (size_t i = 5; i + 5 < v.size(); i++) {
		Q d = raw[i + 5] * raw[i - 5].conj();
		if (d.w < 0) d = Q{ -d.w, -d.x, -d.y, -d.z };
		const double sp = qlog(d).norm() / (10 / 90.0);
		slow[i] = sp < 0.15;  // moins de 9°/s : visée
		fast[i] = sp > 2.0;   // plus de 115°/s : mouvement rapide
	}
	int ns = 0, nf = 0;
	for (size_t i = 0; i < v.size(); i++) { ns += slow[i]; nf += fast[i]; }
	std::printf("visée : %d images, mouvements rapides : %d images\n", ns, nf);
	std::printf("réglage | bruit de visée (°)        | écart en mouvement rapide (°, moyen / 95e centile)\n");
	for (double strength : { 0.0, 20.0, 40.0, 60.0, 80.0, 100.0 }) {
		OrientSmoother sm;
		if (getenv("BETA")) sm.beta = atof(getenv("BETA"));
		if (getenv("DCUT")) sm.dCutoffHz = atof(getenv("DCUT"));
		std::vector<Q> out;
		std::vector<double> errs;
		for (size_t i = 0; i < v.size(); i++) {
			out.push_back(sm.apply(v[i].t, v[i].q, strength));
			if (fast[i]) {
				Q e = v[i].q * out.back().conj();
				if (e.w < 0) e = Q{ -e.w, -e.x, -e.y, -e.z };
				errs.push_back(qlog(e).norm() * 180 / 3.14159265);
			}
		}
		const double nIn = hfNoise(raw, slow), nOut = hfNoise(out, slow);
		std::sort(errs.begin(), errs.end());
		double mean = 0;
		for (double e : errs) mean += e;
		mean = errs.empty() ? 0 : mean / errs.size();
		std::printf("  %3.0f   | %.4f (brut %.4f, -%2.0f %%) | %.2f / %.2f\n", strength, nOut, nIn, 100 * (1 - nOut / nIn), mean,
			errs.empty() ? 0 : errs[errs.size() * 95 / 100]);
	}
	return 0;
}
