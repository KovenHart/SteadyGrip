// Rejoue un enregistrement de mouvements (Paramètres › Diagnostic) dans LossGuard, avec le modèle du coude
// puis avec le modèle de bras appris, et compare l'écart au retour de chaque perte avec réinitialisation.
// Seules les réinitialisations de moins de 60 s sont comptées (au-delà : manette posée, menus).
// Usage : replay_eval poses_....csv [partie_precedente.csv] [durée max en s, 60 par défaut]  (la seconde, si donnée, sert d'apprentissage préalable)
#include "../src/common/PoseFilter.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace sg;
static double g_maxHold = 60.0;  // secondes ; 3e argument pour changer (une visée à la lunette peut durer 30 s)
struct Row { double t; int hand, state; V3 p, h; Q q, hq; };
static std::vector<double> run(const std::vector<Row>& rows, const std::vector<Row>& pre, int hand, bool learned, size_t& used) {
	FilterSettings s;
	s.learnedArm = learned;
	LossGuard g;
	LearnedArm arm;
	g.setLearnedArm(&arm);
	{ // apprentissage préalable (partie précédente)
		LossGuard p;
		p.setLearnedArm(&arm);
		for (const Row& r : pre) if (r.hand == hand) p.apply(r.t, r.state, r.p, r.q, r.p, true, r.h, r.hq, false, {}, {}, s);
	}
	std::vector<double> gaps;
	int last = 0;
	bool hadHold = false;
	double holdT0 = 0;
	for (const Row& r : rows) {
		if (r.hand != hand) continue;
		g.apply(r.t, r.state, r.p, r.q, r.p, true, r.h, r.hq, false, {}, {}, s);
		const int ph = g.phase();
		if (ph == LossGuard::Holding && last != LossGuard::Holding && g.usedLearnedArm()) used++;
		if (ph == LossGuard::Holding && !hadHold) { hadHold = true; holdT0 = r.t; }
		if (ph == LossGuard::Releasing && last != LossGuard::Releasing) {
			// Les réinitialisations trop longues (manette posée, menus) ne sont pas comptées.
			if (hadHold && r.t - holdT0 < g_maxHold) gaps.push_back(g.lastReturnGapM() * 100);
			hadHold = false;
		}
		last = ph;
	}
	return gaps;
}
static bool load(const char* path, std::vector<Row>& rows) {
	FILE* f = std::fopen(path, "rb");
	if (!f) return false;
	char line[1024];
	while (std::fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == 't') continue;
		double v[33];
		int n = 0;
		for (char* tok = std::strtok(line, ";\r\n"); tok && n < 33; tok = std::strtok(nullptr, ";\r\n")) v[n++] = std::atof(tok);
		if (n < 33) continue;
		Row r;
		r.t = v[0]; r.hand = (int)v[1]; r.state = (int)v[2];
		r.p = { v[4], v[5], v[6] }; r.q = { v[10], v[7], v[8], v[9] };
		r.h = { v[26], v[27], v[28] }; r.hq = { v[32], v[29], v[30], v[31] };
		rows.push_back(r);
	}
	std::fclose(f);
	return true;
}
static double q(std::vector<double> v, double f) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t)(f * v.size()))];
}
int main(int argc, char** argv) {
	if (argc < 2) { std::printf("usage : replay_eval poses.csv [precedente.csv]\n"); return 1; }
	if (argc > 3) g_maxHold = std::atof(argv[3]);
	std::vector<Row> rows, pre;
	if (!load(argv[1], rows) || (argc > 2 && !load(argv[2], pre))) { std::printf("fichier introuvable\n"); return 1; }
	std::vector<double> all[2];
	for (int learned = 0; learned < 2; learned++) {
		for (int hand = 0; hand < 2; hand++) {
			size_t used = 0;
			const auto gaps = run(rows, pre, hand, learned != 0, used);
			all[learned].insert(all[learned].end(), gaps.begin(), gaps.end());
			std::printf("%-14s main %s : %3zu reinit., ecart au retour median %5.1f cm, 90 %% %5.1f cm%s\n", learned ? "modele appris" : "coude", hand ? "droite" : "gauche",
				gaps.size(), q(gaps, 0.5), q(gaps, 0.9), learned ? (" (appris utilise " + std::to_string(used) + " fois)").c_str() : "");
		}
	}
	std::printf("TOTAL coude : median %.1f cm, 90 %% %.1f cm | appris : median %.1f cm, 90 %% %.1f cm\n",
		q(all[0], 0.5), q(all[0], 0.9), q(all[1], 0.5), q(all[1], 0.9));
	return 0;
}
