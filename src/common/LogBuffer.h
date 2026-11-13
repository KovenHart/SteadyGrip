// LogBuffer.h - lecture du tampon d'écriture du journal des manettes de Pimax (repris de LostWatch).
//
// Le module de suivi des manettes écrit son journal (controller_*.txt) dans un tampon en mémoire,
// vidé sur disque par paquets. Ce fichier contient la partie portable (sans Windows) :
//  - découpage des lignes « 2026_11_12_6_18_31[warn]message » ;
//  - repérage du tampon dans un bloc de mémoire lu ;
//  - suivi du tampon d'une lecture à l'autre : nouvelles lignes, vidages (le tampon repart du début
//    et écrase les anciennes lignes), lignes coupées en deux par un vidage, restes périmés.
//
// Rien n'est jamais écrit dans la mémoire de Pimax : on ne fait que lire des copies.
#pragma once

#include <cstddef>
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace lw {

inline bool IsText(unsigned char c) {
	return c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7f);
}

// Fin de la zone de texte qui commence en `from`.
inline size_t TextEnd(const std::string& w, size_t from) {
	size_t i = from;
	while (i < w.size() && IsText((unsigned char)w[i])) i++;
	return i;
}

// Horodatage « AAAA_M_J_H_M_S[niveau] » en position `pos`.
struct Stamp {
	long long ts = 0;     // valeur comparable (secondes depuis une origine arbitraire)
	size_t end = 0;       // position juste après « ] »
	std::string level;
};

inline bool StampAt(const std::string& w, size_t pos, Stamp& out) {
	int f[6];
	size_t i = pos;
	for (int k = 0; k < 6; k++) {
		const size_t start = i;
		int v = 0;
		while (i < w.size() && w[i] >= '0' && w[i] <= '9' && i - start < 4) v = v * 10 + (w[i++] - '0');
		const size_t n = i - start;
		if (k == 0 ? n != 4 : (n < 1 || n > 2)) return false;
		f[k] = v;
		if (k < 5) {
			if (i >= w.size() || w[i] != '_') return false;
			i++;
		}
	}
	if (i >= w.size() || w[i] != '[') return false;
	const size_t ls = ++i;
	while (i < w.size() && ((w[i] >= 'a' && w[i] <= 'z') || (w[i] >= 'A' && w[i] <= 'Z'))) i++;
	if (i == ls || i - ls > 8 || i >= w.size() || w[i] != ']') return false;
	if (f[1] < 1 || f[1] > 12 || f[2] < 1 || f[2] > 31 || f[3] > 23 || f[4] > 59 || f[5] > 60) return false;
	out.level = w.substr(ls, i - ls);
	out.end = i + 1;
	out.ts = ((((long long)f[0] * 13 + f[1]) * 32 + f[2]) * 24 + f[3]) * 3600 + f[4] * 60 + f[5];
	return true;
}

// Prochain horodatage à partir de `from` (avant `limit`).
inline size_t NextStamp(const std::string& w, size_t from, size_t limit) {
	Stamp s;
	for (size_t i = from; i + 12 < limit && i < w.size(); i++) {
		if (w[i] < '0' || w[i] > '9') continue;
		if (i > 0 && w[i - 1] >= '0' && w[i - 1] <= '9') continue; // milieu d'un nombre
		if (StampAt(w, i, s)) return i;
	}
	return std::string::npos;
}

struct Line {
	long long ts = 0;
	std::string key;     // ligne complète normalisée, pour comparer mémoire et fichier
	std::string msg;
};

inline std::string RTrim(std::string s) {
	while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
	return s;
}

// Ligne qui commence en `pos` (horodatage), terminée par '\n' ou par l'horodatage suivant.
// Renvoie false si la ligne n'est pas encore complète. `end` = position après la ligne.
inline bool LineAt(const std::string& w, size_t pos, size_t textEnd, Line& out, size_t& end) {
	Stamp s;
	if (!StampAt(w, pos, s)) return false;
	size_t nl = w.find('\n', s.end);
	if (nl != std::string::npos && nl < textEnd) {
		const size_t nxt = NextStamp(w, s.end, nl);
		end = nxt != std::string::npos ? nxt : nl + 1;
	} else {
		const size_t nxt = NextStamp(w, s.end, textEnd);
		if (nxt == std::string::npos) return false; // pas de fin visible : ligne en cours d'écriture
		end = nxt;
	}
	out.ts = s.ts;
	out.msg = RTrim(w.substr(s.end, end - s.end));
	out.key = w.substr(pos, s.end - pos) + out.msg;
	return !out.msg.empty();
}

// Découpe un texte (lu dans le fichier) en lignes complètes. `consumed` = octets traités.
inline std::vector<Line> SplitLines(const std::string& text, size_t& consumed) {
	std::vector<Line> out;
	consumed = 0;
	size_t p = NextStamp(text, 0, text.size());
	while (p != std::string::npos) {
		Line l;
		size_t end;
		if (!LineAt(text, p, text.size() + 1, l, end)) break;
		out.push_back(l);
		consumed = end;
		p = NextStamp(text, end, text.size());
	}
	return out;
}

// Repère, autour d'un passage « Switch from » trouvé en `hit`, la zone de texte qui l'entoure
// et compte ses lignes horodatées. Le tampon du journal est la zone qui en contient le plus.
struct Block {
	size_t start = 0, end = 0;
	int lines = 0;
};

inline Block BlockAround(const std::string& w, size_t hit) {
	Block b;
	size_t s = hit;
	while (s > 0 && IsText((unsigned char)w[s - 1])) s--;
	b.start = s;
	b.end = TextEnd(w, hit);
	for (size_t p = NextStamp(w, b.start, b.end); p != std::string::npos; p = NextStamp(w, p + 1, b.end)) b.lines++;
	return b;
}

// Suivi du tampon d'une lecture à l'autre.
//
// Le tampon est rempli de façon séquentielle à partir de son début ; quand il est vidé sur disque,
// l'écriture repart du début et écrase l'ancien contenu, mais ce qui suit le point d'écriture reste
// en place (« restes »). On mémorise, pour chaque position, la dernière ligne vue à cet endroit :
// une ligne identique au même endroit est un reste, pas une nouvelle ligne.
class Tracker {
public:
	int wraps = 0;              // vidages observés
	size_t bufferEstimate = 0;  // taille estimée du tampon (octets), connue après un vidage
	int joined = 0;             // lignes coupées par un vidage et recollées
	bool Inited() const { return inited_; }
	size_t Pos() const { return P_; }
	long long InitMaxTs() const { return initMaxTs_; }

	// Première lecture : on se cale sur la fin des lignes déjà écrites, sans rien signaler.
	void Init(const std::string& w) {
		const size_t te = TextEnd(w, 0);
		known_.clear();
		for (size_t p = NextStamp(w, 0, te); p != std::string::npos;) {
			Line l;
			size_t end;
			if (!LineAt(w, p, te, l, end)) break;
			known_.push_back({ p, end, l.key, l.ts });
			initKeys_.insert(l.key);
			if (l.ts > initMaxTs_) initMaxTs_ = l.ts;
			p = NextStamp(w, end, te);
		}
		// Lignes actuelles = suite continue depuis la première, à horodatage croissant.
		P_ = 0;
		lastTs_ = 0;
		cur_ = 0;
		for (size_t i = 0; i < known_.size(); i++) {
			if (i > 0 && (known_[i].pos != known_[i - 1].end || known_[i].ts < known_[i - 1].ts)) break;
			P_ = known_[i].end;
			lastTs_ = known_[i].ts;
			cur_ = i + 1;
		}
		copy_ = w.substr(0, P_);
		prev_ = w;
		initWin_ = w;
		inited_ = true;
	}

	std::vector<Line> Update(const std::string& w) {
		std::vector<Line> out;
		if (!inited_) {
			Init(w);
			return out;
		}
		// Le début déjà lu a-t-il changé ?
		if (P_ > 0) {
			size_t m = 0;
			const size_t n = std::min(P_, w.size());
			while (m < n && w[m] == copy_[m]) m++;
			if (m < P_) {
				// Le tampon est en train de changer : on attend une lecture stable avant de conclure.
				if (prev_ != w) {
					prev_ = w;
					return out;
				}
				const size_t firstPos = cur_ > 0 ? CurrentFirstPos() : 0;
				if (m < firstPos + 24) HandleWrap(w, out); // réécrit depuis le début : vidage
				else Rewind(m);                           // on était allé trop loin
			}
		}
		const size_t te = TextEnd(w, 0);
		for (;;) {
			size_t q = P_;
			Stamp st;
			if (CurrentCount() == 0 && !StampAt(w, q, st)) { // fragment de tête après un vidage
				q = NextStamp(w, q, std::min(te, q + 600));
				if (q == std::string::npos) break;
			}
			Line l;
			size_t end;
			if (!LineAt(w, q, std::max(te, q), l, end)) break;
			if (l.ts < lastTs_ || IsLeftover(q, l.key)) break;
			if (prev_.size() < end || prev_.compare(q, end - q, w, q, end - q) != 0) break; // en cours d'écriture
			Accept(q, end, l, out);
		}
		copy_ = w.substr(0, P_);
		prev_ = w;
		return out;
	}

private:
	struct Known { size_t pos, end; std::string key; long long ts; bool current = true; };
	bool inited_ = false;
	size_t P_ = 0;          // fin de la dernière ligne lue dans le cycle actuel du tampon
	long long lastTs_ = 0;
	size_t cur_ = 0;        // nombre de lignes du cycle actuel en tête de known_ (après Init)
	std::string copy_, prev_;
	std::vector<Known> known_; // dernière ligne vue à chaque position
	// Lignes déjà présentes à la première lecture : jamais signalées, même si on les retrouve
	// plus tard (fin d'une ligne coupée, position mal calée au démarrage).
	std::set<std::string> initKeys_;
	std::string initWin_;
	long long initMaxTs_ = 0;
	bool SeenAtStart(const Line& l) const { return l.ts < initMaxTs_ || (l.ts == initMaxTs_ && initKeys_.count(l.key)); }

	size_t CurrentCount() const {
		size_t n = 0;
		for (const auto& k : known_) n += k.current;
		return n;
	}
	size_t CurrentFirstPos() const {
		size_t best = std::string::npos;
		for (const auto& k : known_) if (k.current && k.pos < best) best = k.pos;
		return best == std::string::npos ? 0 : best;
	}
	bool IsLeftover(size_t pos, const std::string& key) const {
		for (const auto& k : known_) if (!k.current && k.pos == pos && k.key == key) return true;
		return false;
	}
	void Accept(size_t q, size_t end, const Line& l, std::vector<Line>& out) {
		known_.erase(std::remove_if(known_.begin(), known_.end(), [&](const Known& k) { return k.pos < end && k.end > q; }), known_.end());
		known_.push_back({ q, end, l.key, l.ts, true });
		if (!SeenAtStart(l)) out.push_back(l);
		lastTs_ = l.ts;
		P_ = end;
	}
	// Notre position était trop loin (restes pris pour des lignes actuelles au démarrage) :
	// on revient au début de la ligne qui contient le premier octet modifié.
	void Rewind(size_t m) {
		size_t back = 0;
		long long ts = 0;
		for (auto& k : known_) {
			if (!k.current) continue;
			if (k.end > m) k.current = false;
			else if (k.end > back) { back = k.end; ts = k.ts; }
		}
		P_ = back;
		lastTs_ = ts;
	}
	void HandleWrap(const std::string& w, std::vector<Line>& out) {
		wraps++;
		const size_t oldP = P_;
		const size_t oldEnd = TextEnd(w, std::min(oldP, w.size()));
		if (oldEnd > bufferEstimate) bufferEstimate = oldEnd;
		// Lignes écrites après notre dernière lecture, juste avant le vidage : encore en place.
		size_t p = oldP;
		while (p < oldEnd) {
			Line l;
			size_t end;
			if (!LineAt(w, p, oldEnd, l, end) || l.ts < lastTs_ || IsLeftover(p, l.key)) break;
			Accept(p, end, l, out);
			p = end;
		}
		// Ligne coupée par le vidage : son début est à la fin de l'ancien contenu, sa suite au début.
		const size_t first = NextStamp(w, 0, TextEnd(w, 0));
		if (first != std::string::npos && first > 0 && p < oldEnd && w[p] >= '0' && w[p] <= '9') {
			const std::string glued = w.substr(p, oldEnd - p) + w.substr(0, first);
			Line l;
			size_t end;
			// Au premier vidage, la ligne coupée peut dater d'avant notre première lecture.
			const bool predates = wraps == 1 && initWin_.size() >= oldEnd && initWin_.compare(p, oldEnd - p, w, p, oldEnd - p) == 0;
			if (!predates && LineAt(glued + "\n", 0, glued.size() + 1, l, end) && l.ts >= lastTs_ && !SeenAtStart(l)) {
				out.push_back(l);
				joined++;
				lastTs_ = l.ts;
			}
		}
		for (auto& k : known_) k.current = false; // tout devient « ancien contenu »
		P_ = 0;
		prev_.clear(); // chaque ligne doit être vue deux fois de suite, comme toujours
	}
};

} // namespace lw
