// SteadyGrip - filtre de stabilisation des manettes.
//
// Principe : le runtime extrapole la pose des manettes avec l'IMU, puis la recale brutalement à chaque
// nouvelle mesure des caméras (environ 25 fois par seconde). Le filtre suit la trajectoire prédite par
// les vitesses rapportées par le runtime, et absorbe chaque recalage dans un « décalage » qui décroît
// ensuite vers zéro. Résultat :
//   - aucun retard ajouté au mouvement réel (la sortie bouge exactement comme la pose brute) ;
//   - les recalages sont étalés dans le temps : lentement quand la manette est quasi immobile
//     (visée stable, là où le tremblement se voit), presque instantanément quand elle bouge vite ;
//   - les gros recalages (retour dans le champ des caméras après une position de repos) sont étalés
//     sur une durée fixe au lieu de téléporter l'arme.
//
// Code portable (aucune dépendance), utilisé par la couche OpenXR et par le banc de test.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

namespace sg {

struct V3 {
	double x = 0, y = 0, z = 0;
	V3 operator+(const V3& o) const { return { x + o.x, y + o.y, z + o.z }; }
	V3 operator-(const V3& o) const { return { x - o.x, y - o.y, z - o.z }; }
	V3 operator*(double s) const { return { x * s, y * s, z * s }; }
	double norm() const { return std::sqrt(x * x + y * y + z * z); }
};

struct Q {
	double w = 1, x = 0, y = 0, z = 0;
	Q operator*(const Q& b) const {
		return { w * b.w - x * b.x - y * b.y - z * b.z,
				 w * b.x + x * b.w + y * b.z - z * b.y,
				 w * b.y - x * b.z + y * b.w + z * b.x,
				 w * b.z + x * b.y - y * b.x + z * b.w };
	}
	Q conj() const { return { w, -x, -y, -z }; }
	Q normalized() const {
		const double n = std::sqrt(w * w + x * x + y * y + z * z);
		return n > 0 ? Q{ w / n, x / n, y / n, z / n } : Q{};
	}
};

inline Q qexp(const V3& v) { // vecteur rotation (rad) -> quaternion
	const double a = v.norm();
	if (a < 1e-12) return Q{ 1, v.x / 2, v.y / 2, v.z / 2 }.normalized();
	const double s = std::sin(a / 2) / a;
	return { std::cos(a / 2), v.x * s, v.y * s, v.z * s };
}

inline V3 qlog(Q q) { // quaternion -> vecteur rotation (rad)
	q = q.normalized();
	if (q.w < 0) q = { -q.w, -q.x, -q.y, -q.z };
	const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
	if (s < 1e-12) return { 2 * q.x, 2 * q.y, 2 * q.z };
	const double a = 2 * std::atan2(s, q.w);
	return { q.x / s * a, q.y / s * a, q.z / s * a };
}

struct FilterSettings {
	bool enabled = true;
	double stillTauMs = 60;        // constante d'étalement des recalages quand la manette est immobile
	double movingTauMs = 6;        // ... quand elle bouge vite
	double stillSpeed = 0.05;      // m/s : en dessous, la manette est considérée immobile
	double movingSpeed = 0.60;     // m/s : au-dessus, elle est considérée en mouvement rapide
	double stillAngSpeed = 0.5;    // rad/s
	double movingAngSpeed = 4.0;   // rad/s
	double teleportMm = 30;        // recalage traité comme une téléportation
	double teleportDeg = 20;
	double teleportBlendMs = 100;  // durée minimale d'étalement d'une téléportation
	double teleportSpeed = 3.0;    // m/s : vitesse maximale de rattrapage (allonge l'étalement des très grands sauts)
	double gapMs = 150;            // sans recalage depuis cette durée : la manette est sans doute hors champ
	double gapSnapMm = 5;          // après un trou, un recalage supérieur est étalé comme une téléportation
	double maxOffsetMm = 80;       // garde-fou : le décalage ne peut jamais dépasser cette valeur
	double maxRotOffsetDeg = 3;    // l'orientation (gyroscope) est fiable : on n'en lisse que les petits recalages
	// Pertes de suivi signalées par le service Pimax (lu par l'application compagnon) :
	bool lossGuard = true;         // contenir l'arme pendant les pertes
	double maxLostDriftM = 0.30;   // pendant une perte, déplacement maximal accepté
	double maxReachM = 0.85;       // distance maximale manette - buste (portée de bras)
	bool armModel = true;          // pendant une réinitialisation, la main tourne autour du coude
	bool learnedArm = false;       // ... ou suit le modèle de bras appris sur les gestes du joueur (s'il a assez appris)
	double forearmM = 0.30;        // longueur de l'avant-bras (modèle de bras)
	double releaseMinMs = 60;      // retour au champ : 60 ms + 400 ms par mètre d'écart, 300 ms au plus
	double releaseMsPerM = 400;
	double releaseMaxMs = 300;
	double maxCoupledDriftM = 0.05; // main liée à l'autre : écart maximal accepté avec l'estimation de Pimax
	int coupleMode = 2;            // lier la main perdue à l'autre : 0 automatique, 1 toujours, 2 jamais, 3 deux grips serrés
	double rigidTolM = 0.04;       // automatique : mains solidaires si leur position relative a varié de moins de 4 cm...
	double rigidWindowS = 1.5;     // ... sur les mesures (mains toutes deux suivies) de la dernière seconde et demie
	// Stabilisation de l'orientation (visée) : 0 = désactivée, 100 = maximum.
	double orientSmoothing = 50;
	// Bouton grip des manettes Pimax qui se relâche par instants (la main s'ouvre et se referme) :
	// un relâchement plus court que gripHoldMs est ignoré.
	bool gripFix = false;
	double gripHoldMs = 60;     // relâchements parasites mesurés (Gunman Contract) : 29 ms au plus
};

// Anti-rebond d'un bouton grip (une action du jeu, pour une main). Le grip est « serré » au-dessus de kPress ;
// tant qu'il est serré, la valeur rendue au jeu est la plus forte vue depuis l'appui, et un passage sous
// kPress n'est accepté comme relâchement qu'après holdMs sans remonter. Le relâchement réel arrive donc
// holdMs plus tard ; les relâchements parasites plus courts disparaissent.
class GripFilter {
public:
	static constexpr double kPress = 0.5;
	struct Stats {
		uint64_t masked = 0;       // relâchements parasites masqués
		double maskedMaxMs = 0;    // le plus long
		uint64_t shortReleases = 0; // relâchements acceptés puis grip resserré moins de 0,4 s après : délai peut-être trop court
	};
	// t en secondes, raw entre 0 et 1 (1 pour un booléen appuyé). Renvoie la valeur à rendre au jeu.
	double apply(double t, double raw, double holdMs, Stats& st) {
		const bool high = raw >= kPress;
		if (high) {
			if (pressed_ && dipStart_ >= 0) { // remonté pendant le délai : relâchement parasite
				st.masked++;
				st.maskedMaxMs = std::max(st.maskedMaxMs, (t - dipStart_) * 1000.0);
			} else if (!pressed_ && releasedAt_ >= 0 && t - releasedAt_ < 0.4) {
				st.shortReleases++;
			}
			pressed_ = true;
			dipStart_ = -1;
			peak_ = std::max(peak_, raw);
			return peak_;
		}
		if (pressed_) {
			if (dipStart_ < 0) dipStart_ = t;
			if ((t - dipStart_) * 1000.0 < holdMs) return peak_;
			pressed_ = false; // relâchement confirmé
			releasedAt_ = t;
			dipStart_ = -1;
			peak_ = 0;
		}
		return raw;
	}
	void reset() { pressed_ = false; dipStart_ = -1; peak_ = 0; releasedAt_ = -1; }
	bool pressed() const { return pressed_; }
private:
	bool pressed_ = false;
	double dipStart_ = -1;
	double peak_ = 0;
	double releasedAt_ = -1;
};

enum CoupleMode : int { CoupleAuto = 0, CoupleAlways = 1, CoupleNever = 2, CoupleGrips = 3 };

// Préréglages proposés par l'application compagnon.
inline FilterSettings Preset(int level) {
	FilterSettings s;
	switch (level) {
	case 0: s.enabled = false; break;                                      // désactivé
	case 1: s.stillTauMs = 30; s.movingTauMs = 4; s.teleportBlendMs = 70; s.orientSmoothing = 25; break;  // léger
	case 2: break;                                                                                         // moyen (défaut)
	case 3: s.stillTauMs = 100; s.movingTauMs = 30; s.teleportBlendMs = 140; s.orientSmoothing = 75; break; // fort (meilleure session mesurée)
	}
	return s;
}

struct FilterStats {
	uint64_t corrections = 0;   // recalages > 0,2 mm absorbés
	uint64_t teleports = 0;     // recalages étalés comme téléportations
	uint64_t refinements = 0;   // même instant redemandé avec une pose différente (absorbé)
	double lastCorrMm = 0;
	double offsetMm = 0;        // écart actuel entre la sortie et la pose brute
	double offsetDeg = 0;
	bool outOfView = false;     // aucun recalage depuis gapMs
	bool velocityMissing = false;
};

struct PoseIn {
	double t = 0;               // secondes
	V3 p;                       // m
	Q q;
	V3 v;                       // m/s (repère de base)
	V3 w;                       // rad/s
	bool poseValid = false;
	bool velValid = false;      // vitesse linéaire et angulaire valides
};

class PoseFilter {
public:
	// Le repère de la vitesse angulaire varie selon les runtimes (repère de base selon la spec OpenXR,
	// repère local pour l'API PVR). Le filtre choisit automatiquement celui qui prédit le mieux.
	void reset() { *this = PoseFilter(); }

	const FilterStats& stats() const { return stats_; }

	// Ajoute une pose brute et renvoie la pose filtrée. Pour un même instant, renvoie le même résultat.
	void process(const PoseIn& in, const FilterSettings& s, V3& outP, Q& outQ) {
		outP = in.p;
		outQ = in.q;
		if (!in.poseValid) {
			has_ = false;
			return;
		}
		stats_.velocityMissing = !in.velValid;
		if (!s.enabled) {
			has_ = false;
			histN_ = 0;
			posOff_ = {};
			rotOff_ = {};
			tpOff_ = {};
			tpRot_ = {};
			updateStatsOffsets();
			return;
		}
		if (has_ && std::abs(in.t - last_.t) < kSameInstant) {
			// Même instant demandé une seconde fois (Unity demande chaque heure d'affichage deux fois : pour
			// l'image suivante, puis pour l'image en cours, avec des capteurs plus frais). L'écart entre les deux
			// réponses du runtime est un recalage comme un autre : on l'absorbe dans le décalage, pour que la pose
			// rendue pour cet instant ne change pas. Sans cela, ce recalage passait tel quel dans l'image.
			const V3 ePos = in.p - last_.p;
			const Q eRot = (in.q * last_.q.conj()).normalized();
			const double eMm = ePos.norm() * 1000.0;
			const double eDeg = qlog(eRot).norm() * 180.0 / 3.14159265358979;
			if (eMm < s.teleportMm && eDeg < s.teleportDeg) {
				posOff_ = posOff_ - ePos;
				if (eDeg < s.maxRotOffsetDeg) rotOff_ = (rotOff_ * eRot.conj()).normalized();
				const double offMm = posOff_.norm() * 1000.0;
				if (offMm > s.maxOffsetMm) posOff_ = posOff_ * (s.maxOffsetMm / offMm);
				const double rotOffRad = qlog(rotOff_).norm(), rotLim = s.maxRotOffsetDeg * 3.14159265358979 / 180.0;
				if (rotOffRad > rotLim) rotOff_ = qexp(qlog(rotOff_) * (rotLim / rotOffRad));
				if (eMm > 0.2) stats_.refinements++;
				const double t0 = last_.t;
				last_ = in;
				last_.t = t0; // l'instant de référence ne bouge pas
				if (histN_ > 0) hist_[0] = last_;
			}
			outP = in.p + posOff_ + tpOff_;
			outQ = (tpRot_ * rotOff_ * in.q).normalized();
			updateStatsOffsets();
			return;
		}
		if (has_ && in.t < last_.t) { // requête dans le passé : on applique le décalage courant sans l'actualiser
			outP = in.p + posOff_ + tpOff_;
			outQ = (tpRot_ * rotOff_ * in.q).normalized();
			return;
		}
		const double dt = has_ ? in.t - last_.t : 0;
		if (!has_ || dt > 0.25) {
			// Premier échantillon ou interruption longue : on repart de la pose brute.
			has_ = true;
			last_ = in;
			histN_ = 0;
			pushHistory(in);
			lastCorrT_ = in.t;
			posOff_ = {};
			rotOff_ = {};
			tpOff_ = {};
			tpRot_ = {};
			tpEnd_ = -1;
			updateStatsOffsets();
			return;
		}

		// 1) Prédiction à partir de la pose précédente et des vitesses (moyenne des deux extrémités).
		//    Si le runtime ne fournit pas les vitesses, on les estime sur les dernières poses brutes
		//    (régression sur ~50 ms, qui dilue les recalages au lieu de les recopier).
		V3 vA = last_.v, vB = in.v, wA = last_.w, wB = in.w;
		const bool estimated = !in.velValid;
		if (estimated) {
			estimateVelocity(vA, wA);
			vB = vA;
			wB = wA;
		}
		const V3 pPred = last_.p + (vA + vB) * (0.5 * dt);
		const Q dq = qexp((wA + wB) * (0.5 * dt));
		const Q qPredWorld = (dq * last_.q).normalized();
		const Q qPredLocal = (last_.q * dq).normalized();
		Q qPred = qPredWorld;
		if (!estimated) {
			const double errWorld = qlog(in.q * qPredWorld.conj()).norm();
			const double errLocal = qlog(in.q * qPredLocal.conj()).norm();
			// Moyenne glissante des erreurs des deux conventions ; on garde la meilleure.
			errWorldAvg_ += 0.02 * (errWorld - errWorldAvg_);
			errLocalAvg_ += 0.02 * (errLocal - errLocalAvg_);
			if (errLocalAvg_ < errWorldAvg_) qPred = qPredLocal;
		}

		// 2) Innovation = recalage apporté par le runtime depuis la pose précédente.
		const V3 ePos = in.p - pPred;
		const Q eRot = (in.q * qPred.conj()).normalized(); // repère de base
		const double eMm = ePos.norm() * 1000.0;
		const double eDeg = qlog(eRot).norm() * 180.0 / 3.14159265358979;

		const bool wasGap = (in.t - lastCorrT_) * 1000.0 > s.gapMs;
		stats_.outOfView = wasGap;
		if (eMm > 0.2) {
			stats_.corrections++;
			stats_.lastCorrMm = eMm;
			lastCorrT_ = in.t;
		}
		const bool big = eMm > s.teleportMm || eDeg > s.teleportDeg || (wasGap && eMm > s.gapSnapMm);
		// 3) La sortie continue sur la trajectoire prédite : on absorbe l'innovation, soit dans le décalage
		//    « téléportation » (résorbé linéairement), soit dans le décalage ordinaire (résorbé en exponentielle).
		if (big) {
			stats_.teleports++;
			const double durMs = std::max(s.teleportBlendMs, eMm / std::max(0.1, s.teleportSpeed));
			// Le décalage ordinaire en cours est versé dans la téléportation, qui reprend tout à zéro.
			// Seule la position est étalée : un grand écart d'orientation est appliqué immédiatement.
			tpOff_ = tpOff_ + posOff_ - ePos;
			posOff_ = {};
			rotOff_ = {};
			tpEnd_ = in.t + durMs / 1000.0;
		} else {
			posOff_ = posOff_ - ePos;
			if (eDeg < s.maxRotOffsetDeg) rotOff_ = (rotOff_ * eRot.conj()).normalized();
			else rotOff_ = {};
			// Résorption linéaire de la téléportation en cours : elle atteint zéro exactement à tpEnd_.
			if (in.t >= tpEnd_) {
				tpOff_ = {};
				tpRot_ = {};
			} else {
				const double keep = 1.0 - dt / (tpEnd_ - last_.t);
				tpOff_ = tpOff_ * keep;
				tpRot_ = qexp(qlog(tpRot_) * keep);
			}
		}

		// 4) Décroissance du décalage, plus ou moins rapide selon la vitesse de la manette.
		double tauMs;
		{
			const double lin = vB.norm(), ang = wB.norm();
			const double a1 = (lin - s.stillSpeed) / std::max(1e-6, s.movingSpeed - s.stillSpeed);
			const double a2 = (ang - s.stillAngSpeed) / std::max(1e-6, s.movingAngSpeed - s.stillAngSpeed);
			const double a = std::clamp(std::max(a1, a2), 0.0, 1.0);
			// Interpolation géométrique : la constante chute vite dès que la manette se met en mouvement.
			tauMs = s.stillTauMs * std::pow(std::max(1e-3, s.movingTauMs) / std::max(1e-3, s.stillTauMs), a);
		}
		const double k = std::exp(-dt * 1000.0 / std::max(0.1, tauMs));
		posOff_ = posOff_ * k;
		rotOff_ = qexp(qlog(rotOff_) * k);

		// 5) Garde-fous.
		const double offMm = posOff_.norm() * 1000.0;
		if (offMm > s.maxOffsetMm) posOff_ = posOff_ * (s.maxOffsetMm / offMm);
		const double rotOffRad = qlog(rotOff_).norm(), rotLim = s.maxRotOffsetDeg * 3.14159265358979 / 180.0;
		if (rotOffRad > rotLim) rotOff_ = qexp(qlog(rotOff_) * (rotLim / rotOffRad));
		const double tpMm = tpOff_.norm() * 1000.0;
		if (tpMm > 1000.0) tpOff_ = tpOff_ * (1000.0 / tpMm);

		last_ = in;
		pushHistory(in);
		outP = in.p + posOff_ + tpOff_;
		outQ = (tpRot_ * rotOff_ * in.q).normalized();
		updateStatsOffsets();
	}

private:
	static constexpr int kHist = 6;
	static constexpr double kSameInstant = 0.002; // deux demandes à moins de 2 ms d'écart visent la même image

	void pushHistory(const PoseIn& in) {
		for (int i = kHist - 1; i > 0; i--) hist_[i] = hist_[i - 1];
		hist_[0] = in;
		histN_ = std::min(histN_ + 1, kHist);
	}

	// Vitesses estimées par moindres carrés sur l'historique (pose la plus récente = hist_[0]).
	void estimateVelocity(V3& v, V3& w) const {
		v = {};
		w = {};
		if (histN_ < 2) return;
		double tm = 0;
		for (int i = 0; i < histN_; i++) tm += hist_[i].t;
		tm /= histN_;
		double stt = 0;
		V3 sp, sr;
		for (int i = 0; i < histN_; i++) {
			const double dt = hist_[i].t - tm;
			stt += dt * dt;
			sp = sp + hist_[i].p * dt;
			// Orientation relative à la plus récente, en vecteur rotation (repère de base).
			sr = sr + qlog(hist_[i].q * hist_[0].q.conj()) * dt;
		}
		if (stt <= 0) return;
		V3 pm;
		for (int i = 0; i < histN_; i++) pm = pm + hist_[i].p;
		pm = pm * (1.0 / histN_);
		V3 cov;
		for (int i = 0; i < histN_; i++) cov = cov + (hist_[i].p - pm) * (hist_[i].t - tm);
		v = cov * (1.0 / stt);
		w = sr * (1.0 / stt);
	}

	void updateStatsOffsets() {
		stats_.offsetMm = (posOff_ + tpOff_).norm() * 1000.0;
		stats_.offsetDeg = qlog(tpRot_ * rotOff_).norm() * 180.0 / 3.14159265358979;
	}

	bool has_ = false;
	PoseIn last_;
	V3 posOff_;
	Q rotOff_;
	double lastCorrT_ = 0;
	PoseIn hist_[kHist];
	int histN_ = 0;
	V3 tpOff_;
	Q tpRot_;
	double tpEnd_ = -1;
	double errWorldAvg_ = 0, errLocalAvg_ = 0;
	FilterStats stats_;
};

// États de suivi d'une manette, tels que notés par le service Pimax.
enum TrackState : int32_t { TrackUnknown = 0, TrackTracking = 1, TrackLost = 2, TrackIniting = 3 };

// Repère du buste, estimé à partir du casque : origine au cou (le point qui ne bouge pas quand on
// hoche la tête), orientée selon le cap horizontal de la tête, lissé. Quand on baisse les yeux vers son
// arme ou qu'on penche la tête, le buste, lui, ne bouge pas.
class BodyFrame {
public:
	void update(double t, const V3& hp, const Q& hq, double tauS) {
		pos_ = hp + rotate(hq, V3{ 0, -0.10, 0.08 }); // cou : 10 cm sous les yeux, 8 cm en arrière
		const V3 f = rotate(hq, V3{ 0, 0, -1 }), u = rotate(hq, V3{ 0, 1, 0 });
		// Cap : direction du regard projetée à l'horizontale ; tête baissée ou levée, le haut de la tête
		// donne la direction (vers l'avant tête baissée, vers l'arrière tête levée).
		const double sgn = f.y < 0 ? 1.0 : -1.0;
		const double vx = f.x + u.x * sgn, vz = f.z + u.z * sgn;
		const double target = std::atan2(-vx, -vz);
		if (!init_ || t - lastT_ > 1.0) {
			yaw_ = target;
			init_ = true;
		} else {
			double d = target - yaw_;
			while (d > 3.14159265358979) d -= 2 * 3.14159265358979;
			while (d < -3.14159265358979) d += 2 * 3.14159265358979;
			const double dt = std::max(0.0, t - lastT_);
			yaw_ += d * (1 - std::exp(-dt / std::max(1e-3, tauS)));
		}
		lastT_ = t;
		q_ = qexp(V3{ 0, yaw_, 0 });
	}
	V3 toLocal(const V3& p) const { return rotate(q_.conj(), p - pos_); }
	V3 toWorld(const V3& l) const { return pos_ + rotate(q_, l); }
	Q toLocal(const Q& q) const { return q_.conj() * q; }
	const V3& pos() const { return pos_; }
	const Q& rot() const { return q_; }

	static V3 rotate(const Q& q, const V3& v) {
		const Q r = q * Q{ 0, v.x, v.y, v.z } * q.conj();
		return { r.x, r.y, r.z };
	}

private:
	bool init_ = false;
	double yaw_ = 0, lastT_ = 0;
	V3 pos_;
	Q q_;
};

// Modèle de bras appris sur les gestes du joueur. Pendant que la manette est suivie, on mémorise des couples
// (orientation de la manette et inclinaison de la tête, dans le repère du buste) -> position de la main dans ce
// repère. Pendant une réinitialisation de Pimax, seule l'orientation (IMU) reste fiable : la position est
// estimée par les 15 postures mémorisées les plus proches (moyenne pondérée par l'inverse de la distance).
// Mesuré hors jeu sur quatre parties (replay_eval, modèle appris sur les trois autres) : écart au retour
// de 10 à 13 cm en médiane au lieu de 12 à 27 cm ; 24 à 30 cm au lieu de 26 à 70 cm pour les 10 % les pires.
class LearnedArm {
public:
	static constexpr size_t kMax = 20000;    // environ 14 minutes de jeu suivi (une image sur trois)
	static constexpr size_t kMin = 1500;     // environ une minute : en dessous, on garde le modèle du coude
	static constexpr int kK = 15;
	struct Sample { float f[7]; float p[3]; };

	static void features(const Q& ql, const Q& hl, float out[7]) {
		const V3 f = rot(ql, V3{ 0, 0, -1 }), u = rot(ql, V3{ 0, 1, 0 }), hf = rot(hl, V3{ 0, 0, -1 });
		out[0] = (float)f.x; out[1] = (float)f.y; out[2] = (float)f.z;
		out[3] = (float)u.x; out[4] = (float)u.y; out[5] = (float)u.z;
		out[6] = (float)(0.5 * hf.y);
	}
	// ql, hl : orientations de la manette et de la tête dans le repère du buste ; local : position de la main.
	void add(const Q& ql, const Q& hl, const V3& local) {
		if (tick_++ % 3) return;
		Sample sm;
		features(ql, hl, sm.f);
		sm.p[0] = (float)local.x; sm.p[1] = (float)local.y; sm.p[2] = (float)local.z;
		if (data_.size() < kMax) data_.push_back(sm);
		else { data_[next_] = sm; next_ = (next_ + 1) % kMax; }
	}
	bool ready() const { return data_.size() >= kMin; }
	size_t size() const { return data_.size(); }
	V3 predict(const Q& ql, const Q& hl) const {
		float f[7];
		features(ql, hl, f);
		float bestD[kK];
		size_t bestI[kK];
		int n = 0;
		for (size_t i = 0; i < data_.size(); i++) {
			const float* g = data_[i].f;
			float d = 0;
			for (int k = 0; k < 7; k++) d += (g[k] - f[k]) * (g[k] - f[k]);
			if (n == kK && d >= bestD[kK - 1]) continue;
			int j = n < kK ? n++ : kK - 1;
			while (j > 0 && bestD[j - 1] > d) { bestD[j] = bestD[j - 1]; bestI[j] = bestI[j - 1]; j--; }
			bestD[j] = d;
			bestI[j] = i;
		}
		V3 sum;
		double wsum = 0;
		for (int j = 0; j < n; j++) {
			const double w = 1.0 / std::max(1e-6, std::sqrt((double)bestD[j]));
			const float* p = data_[bestI[j]].p;
			sum = sum + V3{ p[0], p[1], p[2] } * w;
			wsum += w;
		}
		return wsum > 0 ? sum * (1.0 / wsum) : V3{};
	}
	bool save(FILE* f) const {
		const uint32_t n = (uint32_t)data_.size(), nx = (uint32_t)next_;
		return fwrite(&n, 4, 1, f) == 1 && fwrite(&nx, 4, 1, f) == 1 && (n == 0 || fwrite(data_.data(), sizeof(Sample), n, f) == n);
	}
	bool load(FILE* f) {
		uint32_t n = 0, nx = 0;
		if (fread(&n, 4, 1, f) != 1 || fread(&nx, 4, 1, f) != 1 || n > kMax || nx >= std::max<uint32_t>(n, 1)) return false;
		std::vector<Sample> d(n);
		if (n && fread(d.data(), sizeof(Sample), n, f) != n) return false;
		data_.swap(d);
		next_ = nx;
		return true;
	}

private:
	static V3 rot(const Q& q, const V3& v) {
		const Q r = q * Q{ 0, v.x, v.y, v.z } * q.conj();
		return { r.x, r.y, r.z };
	}
	std::vector<Sample> data_;
	size_t next_ = 0;
	uint32_t tick_ = 0;
};

// Stabilisation de l'orientation pour la visée (filtre « One Euro ») : très lissée quand la manette
// est presque immobile (tremblement de visée), quasiment pas dès qu'elle tourne vite (aucun retard
// sensible sur les mouvements). strength : 0 = désactivé, 100 = maximum.
class OrientSmoother {
public:
	double beta = 10.0;     // Hz ajoutés par rad/s de vitesse de rotation (réglé sur des sessions réelles)
	double dCutoffHz = 5.0; // lissage de la vitesse
	Q apply(double t, const Q& q, double strength) {
		// Le jeu demande souvent la pose plusieurs fois pour un même instant (une fois par image et par usage,
		// ou deux fois chaque heure d'affichage avec Unity, la seconde avec des capteurs plus frais) : on rend
		// alors la même orientation, sans faire avancer le filtre.
		if (strength > 0 && init_ && std::abs(t - lastT_) < 0.002) {
			last_ = q;
			corr_ = out_ * q.conj();
			return out_;
		}
		// Instant plus ancien : on applique la dernière correction.
		if (strength > 0 && init_ && t < lastT_ && lastT_ - t < 0.2) return (corr_ * q).normalized();
		if (strength <= 0 || !init_ || t - lastT_ > 0.2) {
			init_ = strength > 0;
			out_ = q;
			lastT_ = t;
			speed_ = 0;
			last_ = q;
			corr_ = Q{};
			return q;
		}
		const double dt = t - lastT_;
		lastT_ = t;
		Q d = q * last_.conj();
		if (d.w < 0) d = Q{ -d.w, -d.x, -d.y, -d.z };
		last_ = q;
		const double rawSpeed = qlog(d).norm() / dt;                         // rad/s
		speed_ += (rawSpeed - speed_) * alpha(dt, dCutoffHz);                 // vitesse lissée
		const double minCut = 6.0 * std::pow(0.06, std::clamp(strength, 0.0, 100.0) / 100.0); // 6 Hz -> 0,36 Hz
		const double cut = minCut + beta * speed_;
		Q e = q * out_.conj();
		if (e.w < 0) e = Q{ -e.w, -e.x, -e.y, -e.z };
		out_ = qexp(qlog(e) * alpha(dt, cut)) * out_;
		corr_ = out_ * q.conj();
		return out_;
	}

private:
	static double alpha(double dt, double cutoffHz) {
		const double tau = 1.0 / (2 * 3.14159265358979 * cutoffHz);
		return 1.0 / (1.0 + tau / dt);
	}
	bool init_ = false;
	double lastT_ = 0, speed_ = 0;
	Q out_, last_, corr_;
};

// Position de la manette vue depuis le casque (repère des caméras) : azimut (degrés, positif à droite),
// élévation (degrés, positive vers le haut, -90 = sous le casque) et distance (m).
inline void HeadAngles(const V3& hp, const Q& hq, const V3& p, double& azDeg, double& elDeg, double& distM) {
	const Q r = hq.conj() * Q{ 0, p.x - hp.x, p.y - hp.y, p.z - hp.z } * hq;
	const V3 l{ r.x, r.y, r.z };
	azDeg = std::atan2(l.x, -l.z) * 180.0 / 3.14159265358979;
	elDeg = std::atan2(l.y, std::sqrt(l.x * l.x + l.z * l.z)) * 180.0 / 3.14159265358979;
	distM = l.norm();
}

// Zones autour du casque pour la carte des pertes.
struct LossZones {
	static constexpr int kEl = 5, kAz = 5, kDist = 3;
	static int El(double el) { return el > 15 ? 0 : el > -25 ? 1 : el > -55 ? 2 : el > -75 ? 3 : 4; }
	static int Az(double az) { return az < -55 ? 0 : az < -20 ? 1 : az <= 20 ? 2 : az <= 55 ? 3 : 4; }
	static int Dist(double d) { return d < 0.25 ? 0 : d < 0.45 ? 1 : 2; }
	static const char* ElName(int i) {
		static const char* n[] = { "au-dessus du regard (> +15°)", "face (+15° à -25°)", "bas (-25° à -55°)", "très bas (-55° à -75°)", "sous le casque (< -75°)" };
		return n[i];
	}
	static const char* AzName(int i) {
		static const char* n[] = { "côté gauche (< -55°)", "gauche (-55° à -20°)", "devant (±20°)", "droite (20° à 55°)", "côté droit (> 55°)" };
		return n[i];
	}
	static const char* DistName(int i) {
		static const char* n[] = { "près du visage (< 25 cm)", "25 à 45 cm", "bras tendu (> 45 cm)" };
		return n[i];
	}
};

// Contient la manette pendant une perte de suivi signalée par le service Pimax.
//
// Tout est calculé dans le repère du BUSTE (voir BodyFrame), pas de la tête.
//  - Perte (Lost) : on garde l'estimation de Pimax (juste dans la plupart des cas), mais son déplacement
//    depuis le début de la perte est limité, et la manette reste à portée de bras.
//  - Réinitialisation (Initing, environ 0,5 s plus tard) : l'estimation de position de Pimax est
//    abandonnée. Modèle de bras : la main tourne autour d'un coude estimé, selon l'orientation de la
//    manette (gyroscope, toujours fiable) ; si on lève le poignet, l'arme monte.
//  - Reprise (Tracking) : retour progressif vers la vraie position, d'autant plus long que l'écart est
//    grand, au lieu d'une téléportation.
//
// Option : si l'autre main est suivie, la main perdue peut être liée à l'autre main : toujours, quand les
// deux mains se déplaçaient ensemble juste avant la perte (automatique), ou quand les deux grips sont
// serrés au début de la perte (arme tenue à deux mains). Désactivé par défaut.
class LossGuard {
public:
	enum Phase { Normal = 0, Contained = 1, Holding = 2, Releasing = 3 };

	Phase phase() const { return phase_; }
	// Modèle de bras appris à utiliser et à enrichir (partagé par les flux d'une même manette ; nul : aucun).
	void setLearnedArm(LearnedArm* a) { arm_ = a; }
	bool usedLearnedArm() const { return armUsed_; }
	bool coupled() const { return coupled_; }
	bool lastRigid() const { return coupleAllowed_; }
	int diagSamples() const { return diagN_; }
	double diagDeviation() const { return diagDev_; }
	double diagDistance() const { return diagDist_; }
	bool diagReused() const { return diagReused_; }
	uint64_t holds() const { return holds_; }
	// Dernière reprise : écart entre la position affichée et la vraie position (m), durée du retour (ms).
	double lastReturnGapM() const { return retGap_; }
	double lastReturnMs() const { return retMs_; }
	// Somme et nombre des écarts à la reprise (pour la moyenne affichée).
	double gapSumM() const { return gapSum_; }
	uint64_t gapCount() const { return gapCount_; }

	// t : secondes ; p, q : pose filtrée de la manette ; rawP : position brute (pour mesurer l'écart) ;
	// head : pose du casque ; partner : pose de l'autre main si elle est suivie. Même repère pour tout.
	V3 apply(double t, int state, const V3& p, const Q& q, const V3& rawP, bool headValid, const V3& hp, const Q& hq,
		bool partnerValid, const V3& pp, const Q& pq, const FilterSettings& s, bool gripsHeld = false) {
		if (headValid) body_.update(t, hp, hq, 0.3);
		if (!s.enabled || !s.lossGuard || !headValid) { // option désactivée (ou casque inconnu) : aucune modification
			phase_ = Normal;
			coupled_ = false;
			lastOut_ = body_.toLocal(p);
			return p;
		}
		if (state == TrackUnknown) state = TrackTracking;
		const V3 local = body_.toLocal(p);
		V3 outLocal = local;
		if (state == TrackTracking) {
			if (arm_ && phase_ == Normal) arm_->add(body_.toLocal(q), body_.toLocal(hq), body_.toLocal(rawP));
			if (partnerValid && s.coupleMode == CoupleAuto) hist_.push_back({ t, rotate(pq.conj(), p - pp) });
			while (!hist_.empty() && hist_.front().t < t - s.rigidWindowS) hist_.pop_front();
			if (phase_ == Contained || phase_ == Holding) startRelease(t, body_.toLocal(rawP), s);
			if (phase_ == Releasing) {
				const double a = std::clamp((t - relT0_) / std::max(1e-3, relDur_), 0.0, 1.0);
				const double w = a * a * (3 - 2 * a);
				outLocal = relFrom_ + (local - relFrom_) * w;
				if (a >= 1) phase_ = Normal;
			}
			coupled_ = false;
		} else {
			if (phase_ == Normal || phase_ == Releasing) { // début de la perte : on part de la dernière sortie
				lostStart_ = lastOut_;
				// Modèle appris : l'écart entre la main et la posture apprise est noté maintenant, avec la dernière
				// position fiable (pendant la perte, la position donnée par Pimax dérive).
				armUsed_ = false;
				armReady_ = s.armModel && s.learnedArm && arm_ && arm_->ready();
				if (armReady_) {
					armOff_ = lastOut_ - arm_->predict(body_.toLocal(q), body_.toLocal(hq));
					armT0_ = t;
				}
				coupled_ = false;
				phase_ = Contained;
				coupleAllowed_ = s.coupleMode == CoupleAlways || (s.coupleMode == CoupleAuto && rigid(t, s));
				if (s.coupleMode == CoupleGrips) {
					// Arme à deux mains : les deux grips sont serrés et les mains sont à une distance plausible.
					diagDist_ = partnerValid ? (p - pp).norm() : 0.0;
					coupleAllowed_ = gripsHeld && partnerValid && diagDist_ >= 0.08 && diagDist_ <= 0.80;
				}
			}
			// Arme lâchée d'une main pendant la perte : la main n'est plus liée (repart de sa position actuelle, sans saut).
			if (s.coupleMode == CoupleGrips && !gripsHeld) coupleAllowed_ = false;
			const bool enterHold = state == TrackIniting && phase_ != Holding;
			if (enterHold) {
				phase_ = Holding;
				holds_++;
			}
			const Q ql = body_.toLocal(q);
			if (partnerValid && coupleAllowed_) {
				if (!coupled_ || enterHold) {
					rel_ = rotate(pq.conj(), body_.toWorld(lastOut_) - pp);
					if (!coupled_) relEst_ = lastOut_ - local;
					coupled_ = true;
				}
				const V3 cl = body_.toLocal(pp + rotate(pq, rel_));
				outLocal = phase_ == Holding ? cl : cl + clampLen(local + relEst_ - cl, s.maxCoupledDriftM);
			} else {
				if (coupled_ || enterHold) {
					// Nouveau point de départ = position actuelle (aucun saut). Coude estimé derrière la main,
					// dans l'axe de la manette.
					anchor_ = lastOut_;
					elbow_ = lastOut_ - rotate(ql, V3{ 0, 0, -s.forearmM });
					coupled_ = false;
					// Modèle appris : posture apprise + écart noté au début de la perte (qui s'efface en ~1 s).
					// Le passage depuis la position affichée jusque-là est étalé (0,2 s), sans saut.
					armUsed_ = armReady_ && s.armModel && s.learnedArm;
					if (armUsed_) {
						armJump_ = lastOut_ - (arm_->predict(ql, body_.toLocal(hq)) + armOff_ * std::exp(-(t - armT0_) / kArmBlendS));
						armTj_ = t;
					}
				}
				if (phase_ == Holding && armUsed_)
					outLocal = arm_->predict(ql, body_.toLocal(hq)) + armOff_ * std::exp(-(t - armT0_) / kArmBlendS) +
						armJump_ * std::exp(-(t - armTj_) / kArmJumpS);
				else if (phase_ == Holding) outLocal = s.armModel ? elbow_ + rotate(ql, V3{ 0, 0, -s.forearmM }) : anchor_;
				else outLocal = lostStart_ + clampLen(local - lostStart_, s.maxLostDriftM);
			}
			outLocal = clampLen(outLocal, s.maxReachM);
		}
		lastOut_ = outLocal;
		return body_.toWorld(outLocal);
	}

private:
	static V3 rotate(const Q& q, const V3& v) { return BodyFrame::rotate(q, v); }
	static V3 clampLen(const V3& v, double maxLen) {
		const double n = v.norm();
		return n > maxLen && n > 0 ? v * (maxLen / n) : v;
	}
	bool rigid(double t, const FilterSettings& s) {
		while (!hist_.empty() && hist_.front().t < t - s.rigidWindowS) hist_.pop_front();
		diagN_ = (int)hist_.size();
		if (hist_.size() < 8) {
			diagReused_ = verdictT_ > 0 && t - verdictT_ < 5.0;
			return diagReused_ && verdict_;
		}
		diagReused_ = false;
		V3 mean;
		for (const auto& h : hist_) mean = mean + h.rel;
		mean = mean * (1.0 / hist_.size());
		diagDist_ = mean.norm();
		diagDev_ = 0;
		for (const auto& h : hist_) diagDev_ = std::max(diagDev_, (h.rel - mean).norm());
		verdict_ = diagDist_ >= 0.08 && diagDist_ <= 0.80 && diagDev_ <= s.rigidTolM;
		verdictT_ = t;
		return verdict_;
	}
	// Retour d'autant plus long que l'écart avec la vraie position est grand.
	void startRelease(double t, const V3& rawLocal, const FilterSettings& s) {
		relFrom_ = lastOut_;
		relT0_ = t;
		retGap_ = (rawLocal - lastOut_).norm();
		relDur_ = std::clamp(s.releaseMinMs + s.releaseMsPerM * retGap_, s.releaseMinMs, s.releaseMaxMs) / 1000.0;
		retMs_ = relDur_ * 1000.0;
		gapSum_ += retGap_;
		gapCount_++;
		phase_ = Releasing;
	}

	static constexpr double kArmBlendS = 1.0, kArmJumpS = 0.2;
	struct RelSample { double t; V3 rel; };
	LearnedArm* arm_ = nullptr;
	bool armUsed_ = false, armReady_ = false;
	V3 armOff_, armJump_;
	double armT0_ = 0, armTj_ = 0;
	BodyFrame body_;
	std::deque<RelSample> hist_;
	bool coupleAllowed_ = false, verdict_ = false, diagReused_ = false, coupled_ = false;
	double verdictT_ = 0, diagDev_ = 0, diagDist_ = 0;
	int diagN_ = 0;
	Phase phase_ = Normal;
	V3 lostStart_, anchor_, elbow_, lastOut_, relFrom_, rel_, relEst_;
	double relT0_ = 0, relDur_ = 0.15, retGap_ = 0, retMs_ = 0, gapSum_ = 0;
	uint64_t holds_ = 0, gapCount_ = 0;
};

} // namespace sg
