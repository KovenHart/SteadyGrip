// Tests sans casque : maintien de la manette pendant une perte (LossGuard), repère du buste,
// modèle de bras, retour proportionné, liaison des mains, stabilisation de l'orientation.
#include "../src/common/PoseFilter.h"
#include <cstdio>
#include <random>
#include <vector>
using namespace sg;

static int fails = 0;
static void check(bool ok, const char* what) {
	std::printf("  %s : %s\n", ok ? "ok" : "ÉCHEC", what);
	if (!ok) fails++;
}

// Tête qui tourne autour du cou (comme en vrai) : position des yeux = cou + rotation * (0, 0,10, -0,08).
static V3 HeadFromNeck(const V3& neck, const Q& hq) { return neck - BodyFrame::rotate(hq, V3{ 0, -0.10, 0.08 }); }
static const V3 kNeck{ 0, 1.5, 0 };
static const V3 none;
static const Q qid;

int main() {
	std::printf("1. Les deux mains perdues : buste, modèle de bras, retour proportionné\n");
	{
		FilterSettings s;
		LossGuard g;
		const Q head0;
		V3 out;
		double t = 0;
		const V3 start{ 0.1, 1.15, -0.45 };
		for (int i = 0; i < 90; i++, t += 1 / 90.0) out = g.apply(t, TrackTracking, start, qid, start, true, HeadFromNeck(kNeck, head0), head0, false, none, qid, s);
		// Perte : l'estimation de Pimax dérive vers le sol à 2 m/s.
		V3 p = start;
		for (int i = 0; i < 45; i++, t += 1 / 90.0) { p.y -= 2.0 / 90; out = g.apply(t, TrackLost, p, qid, p, true, HeadFromNeck(kNeck, head0), head0, false, none, qid, s); }
		check((out - start).norm() <= 0.301, "dérive limitée à 30 cm pendant la perte");
		const V3 held0 = out;
		// Réinitialisation ; on baisse les yeux de 60° vers l'arme pendant 1 s : l'arme ne doit pas bouger.
		const Q down = qexp({ -1.047, 0, 0 });
		for (int i = 0; i < 90; i++, t += 1 / 90.0) { p.y -= 0.02; out = g.apply(t, TrackIniting, p, qid, p, true, HeadFromNeck(kNeck, down), down, false, none, qid, s); }
		std::printf("     tête baissée de 60° : l'arme a bougé de %.1f cm\n", (out - held0).norm() * 100);
		check((out - held0).norm() < 0.01, "regarder son arme ne la déplace pas (repère du buste)");
		// On lève le poignet de 45° : avec le modèle de bras, la main monte.
		const Q wrist = qexp({ 0.785, 0, 0 });
		for (int i = 0; i < 20; i++, t += 1 / 90.0) out = g.apply(t, TrackIniting, p, wrist, p, true, HeadFromNeck(kNeck, down), down, false, none, qid, s);
		std::printf("     poignet levé de 45° : la main monte de %.1f cm\n", (out.y - held0.y) * 100);
		check(out.y - held0.y > 0.07, "modèle de bras : lever le poignet lève l'arme");
		// On tourne le corps de 90° (tête et buste) : l'arme suit le buste.
		const Q turn = qexp({ 0, 1.5708, 0 });
		for (int i = 0; i < 180; i++, t += 1 / 90.0) out = g.apply(t, TrackIniting, p, turn * wrist, p, true, HeadFromNeck(kNeck, turn), turn, false, none, qid, s);
		const V3 rel = out - kNeck;
		std::printf("     après un quart de tour : main en (%.2f, %.2f, %.2f) par rapport au cou\n", rel.x, rel.y, rel.z);
		check(rel.x < -0.3, "l'arme maintenue suit la rotation du corps");
		// Reprise : écart de 50 cm environ ; retour progressif et proportionné.
		const V3 truth = kNeck + V3{ -0.45, -0.15, 0.25 };
		V3 prev = out;
		double maxStep = 0;
		for (int i = 0; i < 45; i++, t += 1 / 90.0) {
			out = g.apply(t, TrackTracking, truth, turn, truth, true, HeadFromNeck(kNeck, turn), turn, false, none, qid, s);
			maxStep = std::max(maxStep, (out - prev).norm());
			prev = out;
		}
		std::printf("     reprise : écart %.0f cm, retour en %.0f ms, plus grand pas %.1f cm par image\n", g.lastReturnGapM() * 100, g.lastReturnMs(), maxStep * 100);
		check((out - truth).norm() < 1e-6 && maxStep < 0.08 && g.lastReturnMs() > 150, "retour progressif, plus long pour un grand écart");
		// Petit écart : retour court.
		for (int i = 0; i < 5; i++, t += 1 / 90.0) out = g.apply(t, TrackLost, truth + V3{ 0, 0, -0.02 * i }, turn, truth, true, HeadFromNeck(kNeck, turn), turn, false, none, qid, s);
		out = g.apply(t, TrackTracking, truth, turn, truth, true, HeadFromNeck(kNeck, turn), turn, false, none, qid, s);
		std::printf("     petit écart : %.0f cm, retour en %.0f ms\n", g.lastReturnGapM() * 100, g.lastReturnMs());
		check(g.lastReturnMs() < 120, "petit écart : retour court");
		s.lossGuard = false;
		const V3 o = g.apply(t, TrackIniting, { 1, 2, 3 }, qid, { 1, 2, 3 }, true, HeadFromNeck(kNeck, qid), qid, false, none, qid, s);
		check((o - V3{ 1, 2, 3 }).norm() < 1e-9, "option décochée : aucune modification");
	}

	std::printf("2. Arme à deux mains, liaison en mode automatique : main avant perdue, on épaule\n");
	{
		FilterSettings s;
		s.coupleMode = CoupleAuto;
		LossGuard left;
		const V3 hp = HeadFromNeck(kNeck, qid);
		V3 right{ 0.15, 0.95, -0.25 }, leftTrue{ 0.10, 1.05, -0.58 };
		V3 out;
		for (int i = 0; i < 45; i++) out = left.apply(i / 90.0, TrackTracking, leftTrue, qid, leftTrue, true, hp, qid, true, right, qid, s);
		const V3 gun0 = out - right;
		V3 est = leftTrue;
		const Q raise = qexp({ 0.9, 0, 0 });
		double worstAngle = 0;
		for (int i = 0; i < 60; i++) {
			const double a = i / 59.0, t = 0.5 + i / 90.0;
			const V3 r = V3{ 0.15, 0.95, -0.25 } + (V3{ 0.12, 1.45, -0.15 } - V3{ 0.15, 0.95, -0.25 }) * a;
			const Q q = qexp(qlog(raise) * a);
			est.y -= 0.01;
			out = left.apply(t, i < 20 ? TrackLost : TrackIniting, est, q, est, true, hp, qid, true, r, q, s);
			const V3 expected = BodyFrame::rotate(q, gun0);
			const V3 got = out - r;
			const double c = (got.x * expected.x + got.y * expected.y + got.z * expected.z) / (got.norm() * expected.norm());
			worstAngle = std::max(worstAngle, std::acos(std::clamp(c, -1.0, 1.0)) * 180 / 3.14159265);
		}
		std::printf("     écart d'angle maximal de l'arme pendant la montée : %.1f°\n", worstAngle);
		check(left.coupled() && left.lastRigid() && worstAngle < 12, "la main gauche suit la main droite");
	}

	std::printf("3. Épée et bouclier, mode automatique : l'épée sort du champ en plein coup\n");
	{
		FilterSettings s;
		s.coupleMode = CoupleAuto;
		LossGuard sword;
		const V3 hp = HeadFromNeck(kNeck, qid);
		const V3 shield{ -0.25, 1.25, -0.35 };
		for (int i = 0; i < 90; i++) {
			const double a = i / 90.0 * 6.28;
			const V3 p{ 0.25 + 0.2 * std::cos(a), 1.3 + 0.2 * std::sin(a), -0.4 };
			sword.apply(i / 90.0, TrackTracking, p, qid, p, true, hp, qid, true, shield, qid, s);
		}
		V3 truth{ 0.45, 1.3, -0.4 }, out;
		double worst = 0;
		for (int i = 0; i < 20; i++) {
			truth = truth + V3{ 0.005, 0.012, 0.004 };
			out = sword.apply(1 + i / 90.0, TrackLost, truth, qid, truth, true, hp, qid, true, shield, qid, s);
			worst = std::max(worst, (out - truth).norm());
		}
		check(!sword.coupled() && !sword.lastRigid() && worst < 0.001, "mains indépendantes : l'épée suit l'estimation de Pimax");
	}

	std::printf("4. Réglage par défaut (liaison « jamais ») avec une arme à deux mains\n");
	{
		FilterSettings s;
		LossGuard left;
		const V3 hp = HeadFromNeck(kNeck, qid), right{ 0.15, 0.95, -0.25 }, lp{ 0.10, 1.05, -0.58 };
		for (int i = 0; i < 45; i++) left.apply(i / 90.0, TrackTracking, lp, qid, lp, true, hp, qid, true, right, qid, s);
		for (int i = 0; i < 10; i++) left.apply(0.5 + i / 90.0, TrackLost, lp, qid, lp, true, hp, qid, true, right, qid, s);
		check(s.coupleMode == CoupleNever && !left.coupled(), "pas de liaison par défaut");
	}

	std::printf("5. Mode automatique, pertes courtes à répétition (cas réel de CrossFire)\n");
	{
		FilterSettings s;
		s.coupleMode = CoupleAuto;
		LossGuard left;
		const V3 hp = HeadFromNeck(kNeck, qid), right{ 0.15, 0.95, -0.25 }, lp{ 0.10, 1.05, -0.58 };
		double t = 0;
		for (int i = 0; i < 90; i++, t += 1 / 90.0) left.apply(t, TrackTracking, lp, qid, lp, true, hp, qid, true, right, qid, s);
		int coupledLosses = 0;
		for (int cycle = 0; cycle < 10; cycle++) {
			for (int i = 0; i < 18; i++, t += 1 / 90.0) left.apply(t, TrackLost, lp, qid, lp, true, hp, qid, true, right, qid, s);
			coupledLosses += left.coupled();
			for (int i = 0; i < 5; i++, t += 1 / 90.0) left.apply(t, TrackTracking, lp, qid, lp, true, hp, qid, true, right, qid, s);
		}
		check(coupledLosses == 10, "la décision « solidaires » tient malgré les pertes rapprochées");
	}

	std::printf("6. Stabilisation de l'orientation\n");
	{
		std::mt19937 rng(3);
		std::normal_distribution<double> n(0, 0.15 * 3.14159265 / 180); // tremblement de 0,15°
		OrientSmoother sm;
		double inJit = 0, outJit = 0;
		Q prevIn, prevOut;
		for (int i = 0; i < 900; i++) {
			const Q q = qexp({ n(rng), n(rng), n(rng) });
			const Q o = sm.apply(i / 90.0, q, 40);
			if (i > 90) {
				inJit += qlog(q * prevIn.conj()).norm();
				outJit += qlog(o * prevOut.conj()).norm();
			}
			prevIn = q;
			prevOut = o;
		}
		std::printf("     immobile : tremblement d'une image à l'autre réduit de %.0f %%\n", 100 * (1 - outJit / inJit));
		check(outJit < 0.4 * inJit, "le tremblement en visée est fortement réduit");
		// Rotation rapide (3 rad/s) : retard faible.
		double maxLag = 0;
		for (int i = 0; i < 90; i++) {
			const Q q = qexp({ 0, 3.0 * i / 90.0, 0 });
			const Q o = sm.apply(10 + i / 90.0, q, 40);
			if (i > 20) maxLag = std::max(maxLag, qlog(q * o.conj()).norm() * 180 / 3.14159265);
		}
		std::printf("     rotation rapide (170°/s) : retard maximal %.1f°\n", maxLag);
		check(maxLag < 4.0, "pas de retard sensible en mouvement rapide");
		// Le jeu demande deux fois la même pose par image : le résultat doit être le même qu'avec une seule demande.
		{
			std::mt19937 r2(5);
			OrientSmoother once, twice;
			double maxDiff = 0;
			for (int i = 0; i < 900; i++) {
				const Q q = qexp({ n(r2), 0.5 * std::sin(i / 30.0) + n(r2), n(r2) });
				const Q a = once.apply(i / 72.0, q, 75);
				twice.apply(i / 72.0, q, 75);
				const Q b = twice.apply(i / 72.0, q, 75);
				maxDiff = std::max(maxDiff, qlog(a * b.conj()).norm() * 180 / 3.14159265);
			}
			std::printf("     deux demandes par image : écart maximal %.5f°\n", maxDiff);
			check(maxDiff < 1e-6, "demandes répétées au même instant : même résultat");
		}
		OrientSmoother off;
		const Q q = qexp({ 0.1, 0.2, 0.3 });
		check(qlog(off.apply(0, q, 0) * q.conj()).norm() < 1e-12, "réglage 0 : aucune modification");
	}

	std::printf("7. Liaison « deux grips serrés »\n");
	{
		FilterSettings s;
		s.coupleMode = CoupleGrips;
		const V3 hp = HeadFromNeck(kNeck, qid), lp{ 0.10, 1.05, -0.58 };
		// a) Deux grips serrés : la main perdue suit l'autre main quand on épaule.
		{
			LossGuard left;
			V3 right{ 0.15, 0.95, -0.25 }, out;
			double t = 0;
			for (int i = 0; i < 45; i++, t += 1 / 90.0) out = left.apply(t, TrackTracking, lp, qid, lp, true, hp, qid, true, right, qid, s, true);
			const V3 gun0 = out - right;
			V3 est = lp;
			for (int i = 0; i < 60; i++, t += 1 / 90.0) {
				right = right + V3{ 0, 0.008, 0 };
				est.y -= 0.01; // l'estimation de Pimax tombe
				out = left.apply(t, i < 20 ? TrackLost : TrackIniting, est, qid, est, true, hp, qid, true, right, qid, s, true);
			}
			std::printf("     estimation Pimax tombée de 60 cm ; position relative à l'autre main conservée à %.1f cm près\n", (out - right - gun0).norm() * 100);
			check(left.coupled() && (out - right - gun0).norm() < 0.075, "deux grips serrés : la main perdue suit l'autre main (5 cm de tolérance pendant la perte)");
			// b) On lâche le grip avant : la main redevient indépendante, sans saut.
			const V3 before = out;
			out = left.apply(t, TrackIniting, est, qid, est, true, hp, qid, true, right, qid, s, false);
			std::printf("     grip relâché : saut de %.2f cm\n", (out - before).norm() * 100);
			check(!left.coupled() && (out - before).norm() < 0.005, "grip relâché pendant la perte : main indépendante, sans saut");
		}
		// c) Un seul grip serré au début de la perte : pas de liaison.
		{
			LossGuard left;
			const V3 right{ 0.15, 0.95, -0.25 };
			for (int i = 0; i < 45; i++) left.apply(i / 90.0, TrackTracking, lp, qid, lp, true, hp, qid, true, right, qid, s, false);
			for (int i = 0; i < 10; i++) left.apply(0.5 + i / 90.0, TrackLost, lp, qid, lp, true, hp, qid, true, right, qid, s, false);
			check(!left.coupled(), "un grip relâché : pas de liaison");
		}
		// d) Deux grips serrés mais mains à 1 m l'une de l'autre (pas une arme) : pas de liaison.
		{
			LossGuard left;
			const V3 farP{ 0.9, 1.3, -0.3 };
			for (int i = 0; i < 45; i++) left.apply(i / 90.0, TrackTracking, lp, qid, lp, true, hp, qid, true, farP, qid, s, true);
			for (int i = 0; i < 10; i++) left.apply(0.5 + i / 90.0, TrackLost, lp, qid, lp, true, hp, qid, true, farP, qid, s, true);
			check(!left.coupled(), "mains trop éloignées : pas de liaison malgré les grips");
		}
	}

	std::printf("8. Carte des pertes : angles vus du casque\n");
	{
		const V3 hp{ 0, 1.6, 0 };
		double az, el, d;
		HeadAngles(hp, qid, hp + V3{ 0, -0.4, 0 }, az, el, d);
		check(std::fabs(el + 90) < 1e-6 && std::fabs(d - 0.4) < 1e-9 && LossZones::El(el) == 4, "main sous le casque : -90°, zone « sous le casque »");
		HeadAngles(hp, qid, hp + V3{ 0.5, 0, 0 }, az, el, d);
		check(std::fabs(az - 90) < 1e-6 && LossZones::Az(az) == 4, "main à droite : +90°, zone « côté droit »");
		// Tête tournée de 90° vers la gauche : une main devant le corps (axe -z) est alors à droite du regard.
		const Q left90 = qexp({ 0, 1.5708, 0 });
		HeadAngles(hp, left90, hp + V3{ 0, 0, -0.5 }, az, el, d);
		check(std::fabs(az - 90) < 0.01, "les angles suivent la tête (repère des caméras)");
		HeadAngles(hp, qid, hp + V3{ 0, -0.3, -0.3 }, az, el, d);
		check(std::fabs(el + 45) < 1e-6 && LossZones::El(el) == 2 && LossZones::Az(az) == 2 && LossZones::Dist(d) == 1, "main devant, en bas à 45°");
	}

	std::printf("9. Grip qui se relâche par instants (anti-rebond)\n");
	{
		GripFilter f;
		GripFilter::Stats st;
		const double dt = 1 / 72.0, hold = 120;
		double t = 0, out = 0;
		bool dropped = false;
		// Grip serré à 0,9 pendant 3 s, avec un relâchement parasite (valeur 0) de 2 images toutes les 0,5 s.
		for (int i = 0; i < 216; i++, t += dt) {
			const double raw = (i % 36 == 10 || i % 36 == 11) ? 0.0 : 0.9;
			out = f.apply(t, raw, hold, st);
			if (out < GripFilter::kPress) dropped = true;
		}
		check(!dropped, "la main reste fermée malgré les relâchements parasites");
		check(st.masked == 6 && st.maskedMaxMs < 40, "relâchements parasites comptés");
		// Vrai relâchement : la main s'ouvre après le délai.
		double openedAt = -1;
		const double t0 = t;
		for (int i = 0; i < 36; i++, t += dt) {
			out = f.apply(t, 0.0, hold, st);
			if (out < GripFilter::kPress && openedAt < 0) openedAt = t;
		}
		std::printf("     vrai relâchement : main ouverte après %.0f ms\n", (openedAt - t0) * 1000);
		check(openedAt > 0 && openedAt - t0 >= 0.120 && openedAt - t0 < 0.140, "vrai relâchement accepté après le délai (120 ms ici)");
		// Valeur analogique qui oscille autour de 0,5 : la valeur rendue reste la plus forte vue.
		GripFilter g;
		GripFilter::Stats st2;
		double minOut = 1;
		for (int i = 0; i < 100; i++) minOut = std::min(minOut, g.apply(i * dt, i % 2 ? 0.45 : 0.62, hold, st2));
		check(minOut >= 0.62 - 1e-9, "grip analogique qui oscille autour du seuil : valeur stable");
		// Nouvel appui juste après un vrai relâchement : signalé (délai peut-être trop court).
		GripFilter h;
		GripFilter::Stats st3;
		t = 0;
		for (int i = 0; i < 10; i++, t += dt) h.apply(t, 1.0, hold, st3);
		for (int i = 0; i < 15; i++, t += dt) h.apply(t, 0.0, hold, st3);   // 208 ms : relâchement accepté
		for (int i = 0; i < 5; i++, t += dt) h.apply(t, 1.0, hold, st3);
		check(st3.shortReleases == 1 && st3.masked == 0, "relâchement suivi d'un appui rapide signalé dans le journal");
	}

	std::printf("10. Même instant demandé deux fois (Unity)\n");
	{
		// Chaque heure d'affichage est demandée deux fois : pour l'image suivante, puis pour l'image en cours,
		// avec une réponse un peu différente. La pose rendue doit rester celle de la première demande.
		FilterSettings s;
		PoseFilter f;
		const double P = 1.0 / 72;
		bool stable = true;
		for (int k = 1; k < 200; k++) {
			PoseIn in; in.poseValid = in.velValid = true; in.q = Q{};
			in.t = k * P; in.v = V3{ 0.3, 0, 0 };
			in.p = V3{ 0.3 * in.t, 0, 0 } + V3{ (k % 3) * 0.001, 0, 0 };
			V3 o1, o2; Q q1, q2;
			f.process(in, s, o1, q1);
			in.p = in.p + V3{ 0.002 * ((k % 2) ? 1 : -1), 0.001, 0 };   // seconde réponse, plus fraîche
			in.t += 0.0000005;                                         // même image, à quelques ns près
			f.process(in, s, o2, q2);
			if ((o2 - o1).norm() > 1e-9) stable = false;
		}
		check(stable, "la seconde demande d'un même instant rend la même pose");
		check(f.stats().refinements > 100, "les secondes demandes sont comptées");
	}

	std::printf("11. Modèle de bras appris\n");
	{
		// Joueur de synthèse : deux habitudes. Visée (manette vers l'avant, main devant le visage) et repos
		// (manette vers le bas, main le long du corps). Le casque reste immobile.
		const V3 hp{ 0, 1.6, 0 }, neck{ 0, 1.5, 0.08 };
		const Q hq{};
		const Q aimQ{}, restQ = qexp(V3{ -1.5708, 0, 0 });
		const V3 aimP = neck + V3{ 0.05, -0.05, -0.35 }, restP = neck + V3{ -0.20, -0.70, 0.0 };
		auto blend = [](const V3& a, const V3& b, double w) { return a + (b - a) * w; };
		const double dt = 1.0 / 72;
		FilterSettings s;
		s.learnedArm = true;
		LearnedArm arm;
		LossGuard g;
		g.setLearnedArm(&arm);
		double t = 0;
		// Moins d'une minute de jeu : pas encore prêt, le modèle du coude est gardé.
		for (int i = 0; i < 200; i++, t += dt) g.apply(t, TrackTracking, aimP, aimQ, aimP, true, hp, hq, false, {}, {}, s);
		g.apply(t, TrackIniting, aimP, aimQ, aimP, true, hp, hq, false, {}, {}, s); t += dt;
		check(!g.usedLearnedArm() && g.phase() == LossGuard::Holding, "pas assez appris : modèle du coude");
		for (int i = 0; i < 20; i++, t += dt) g.apply(t, TrackTracking, aimP, aimQ, aimP, true, hp, hq, false, {}, {}, s);
		// Apprentissage : le joueur passe de la visée au repos et inversement, pendant 3 minutes.
		for (int i = 0; i < 72 * 180; i++, t += dt) {
			const double w = 0.5 - 0.5 * std::cos(i * dt * 1.3);
			const Q q = qexp(V3{ -1.5708 * w, 0, 0 });
			const V3 p = blend(aimP, restP, w);
			g.apply(t, TrackTracking, p, q, p, true, hp, hq, false, {}, {}, s);
		}
		check(arm.ready(), "prêt après une minute de jeu suivi");
		// Perte en visée, puis réinitialisation pendant que le joueur baisse l'arme : Pimax ne sait plus rien
		// de la position (il la laisse sur place), seule l'orientation suit.
		V3 last = g.apply(t, TrackTracking, aimP, aimQ, aimP, true, hp, hq, false, {}, {}, s); t += dt;
		double maxStep = 0;
		V3 out;
		for (int i = 0; i < 144; i++, t += dt) {
			const double w = std::min(1.0, i / 72.0);
			const Q q = qexp(V3{ -1.5708 * w, 0, 0 });
			out = g.apply(t, i < 10 ? TrackLost : TrackIniting, aimP, q, aimP, true, hp, hq, false, {}, {}, s);
			maxStep = std::max(maxStep, (out - last).norm());
			last = out;
		}
		check(g.usedLearnedArm(), "modèle appris utilisé pendant la réinitialisation");
		check((out - restP).norm() < 0.06, "l'arme baissée est retrouvée au repos, le long du corps");
		check(maxStep < 0.03, "aucun saut d'une image à l'autre");
		// Option désactivée : le modèle continue d'apprendre, mais le modèle du coude est utilisé.
		FilterSettings off;
		off.learnedArm = false;
		LossGuard h;
		h.setLearnedArm(&arm);
		h.apply(t, TrackTracking, aimP, aimQ, aimP, true, hp, hq, false, {}, {}, off); t += dt;
		h.apply(t, TrackIniting, aimP, aimQ, aimP, true, hp, hq, false, {}, {}, off);
		check(!h.usedLearnedArm(), "option décochée : modèle du coude");
		// Sauvegarde et relecture.
		FILE* f = std::tmpfile();
		const bool saved = arm.save(f);
		std::rewind(f);
		LearnedArm back;
		const bool loaded = back.load(f);
		std::fclose(f);
		check(saved && loaded && back.size() == arm.size() && (back.predict(restQ, hq) - arm.predict(restQ, hq)).norm() < 1e-9, "modèle enregistré puis relu à l'identique");
	}

	std::printf("12. Main « suivie » hors de portée du bras (erreur de Pimax)\n");
	{
		// Cas réel (CrossFire, fusil de précision) : Pimax dit suivre la main mais la fait glisser à 1,25 m du cou.
		const V3 hp{ 0, 1.6, 0 }, neck{ 0, 1.5, 0.08 };
		const Q hq{};
		FilterSettings s;
		LossGuard g;
		LearnedArm arm;
		g.setLearnedArm(&arm);
		const double dt = 1.0 / 72;
		double t = 0, worst = 0, inside = 0;
		for (int i = 0; i < 200; i++, t += dt) {
			const double d = 0.40 + 0.85 * std::min(1.0, i / 100.0);   // de 40 cm à 1,25 m devant le cou
			const V3 p = neck + V3{ 0, 0, -d };
			const V3 o = g.apply(t, TrackTracking, p, Q{}, p, true, hp, hq, false, {}, {}, s);
			const double od = (o - neck).norm();
			worst = std::max(worst, od);
			if (d < 0.85) inside = std::max(inside, (o - p).norm());
		}
		check(worst <= s.maxReachM + LossGuard::kReachMarginM + 1e-9 && g.capped(), "position bloquée à la portée du bras (+10 cm)");
		check(inside < 1e-9, "à portée du bras : aucune modification");
		check(g.cappedFrames() > 0 && arm.size() < 200 / 3 + 1 - 30, "les positions hors de portée ne sont pas apprises");
		// Fichier enregistré avec une mémoire plus grande (versions précédentes) : on garde les plus récentes.
		FILE* f = std::tmpfile();
		const uint32_t n = 7000, nx = 0;
		std::fwrite(&n, 4, 1, f); std::fwrite(&nx, 4, 1, f);
		for (uint32_t i = 0; i < n; i++) {
			LearnedArm::Sample sm{};
			sm.p[0] = (float)i;
			std::fwrite(&sm, sizeof(sm), 1, f);
		}
		std::rewind(f);
		LearnedArm back;
		const bool ok = back.load(f);
		std::fclose(f);
		check(ok && back.size() == LearnedArm::kMax && back.predict(Q{}, Q{}).x > 7000 - LearnedArm::kMax, "ancien fichier plus grand : relu, postures récentes gardées");
	}

	std::printf("13. Instant redemandé plus tard (Zero Caliber 2)\n");
	{
		// Le jeu demande l'instant N+1, puis à nouveau l'instant N : la réponse pour N doit être celle déjà donnée,
		// même si Pimax a entre-temps livré une pose figée qui a modifié le décalage.
		FilterSettings s = Preset(3);
		PoseFilter pf;
		const double dt = 1.0 / 72;
		const V3 v{ 1.0, 0, 0 };
		std::vector<V3> first;
		double maxDiff = 0;
		V3 lastRaw;
		for (int i = 0; i < 200; i++) {
			const double t = i * dt;
			PoseIn in;
			in.t = t; in.v = v; in.q = Q{}; in.poseValid = in.velValid = true;
			in.p = (i % 7 == 3) ? lastRaw : V3{ t, 1.2, -0.3 };   // une pose figée de temps en temps
			lastRaw = in.p;
			V3 op; Q oq;
			pf.process(in, s, op, oq);
			first.push_back(op);
			if (i > 0) {   // l'instant précédent, redemandé, avec la pose fraîche
				PoseIn past = in;
				past.t = t - dt;
				past.p = V3{ t - dt, 1.2, -0.3 };
				V3 pp; Q pq;
				pf.process(past, s, pp, pq);
				maxDiff = std::max(maxDiff, (pp - first[i - 1]).norm());
			}
		}
		check(maxDiff < 1e-9 && pf.stats().pastReused == 199, "même réponse pour un instant redemandé plus tard");
		check(pf.stats().frozen > 20, "poses figées comptées");
	}

	std::printf("14. Chaque instant demandé deux fois : d'abord pose figée, puis pose fraîche\n");
	{
		// Cas réel (Zero Caliber 2) : à vitesse élevée, l'écart entre la pose figée et la pose fraîche (~33 mm) dépassait
		// le seuil de téléportation, et la sortie suivait alternativement l'une puis l'autre.
		FilterSettings s = Preset(3);
		PoseFilter pf;
		const double dt = 1.0 / 72;
		const V3 v{ 2.5, 0, 0 };
		V3 fresh{ 0, 1.2, -0.3 };
		double maxJerk = 0;
		std::vector<V3> outs;
		for (int i = 0; i < 150; i++) {
			const double t = i * dt;
			PoseIn a;
			a.t = t; a.v = v; a.q = Q{}; a.poseValid = a.velValid = true;
			a.p = fresh;                                   // 1re demande : pose de l'instant précédent (figée)
			V3 op; Q oq;
			pf.process(a, s, op, oq);
			outs.push_back(op);
			PoseIn b = a;
			b.t = t + 0.0001;
			fresh = V3{ t * 2.5, 1.2, -0.3 };
			b.p = fresh;                                   // 2e demande, même instant : pose fraîche
			pf.process(b, s, op, oq);
			outs.push_back(op);
		}
		// Écart entre les deux réponses d'un même instant (le jeu peut utiliser l'une ou l'autre).
		for (size_t i = 40; i + 1 < outs.size(); i += 2) maxJerk = std::max(maxJerk, (outs[i + 1] - outs[i]).norm());
		check(maxJerk < 0.005, "les deux réponses d'un même instant concordent (< 5 mm)");
	}

	std::printf("%s\n", fails ? "ÉCHEC" : "OK");
	return fails;
}
