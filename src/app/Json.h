// SteadyGrip - JSON minimal pour les messages entre l'interface (page web) et l'application.
// Écriture : petites fonctions qui assemblent du texte. Lecture : un objet à un seul niveau
// (chaînes, nombres, booléens), ce que l'interface envoie.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace sg {

inline std::string JsonStr(const std::string& s) {
	std::string o = "\"";
	for (unsigned char c : s) {
		switch (c) {
		case '"': o += "\\\""; break;
		case '\\': o += "\\\\"; break;
		case '\n': o += "\\n"; break;
		case '\r': o += "\\r"; break;
		case '\t': o += "\\t"; break;
		default:
			if (c < 0x20) {
				char b[8];
				snprintf(b, sizeof(b), "\\u%04x", c);
				o += b;
			} else {
				o += (char)c;
			}
		}
	}
	return o + "\"";
}

inline std::string JsonNum(double v) {
	char b[32];
	snprintf(b, sizeof(b), "%.10g", v);
	return b;
}

inline std::string JsonBool(bool v) { return v ? "true" : "false"; }

// Valeur lue : texte (chaîne, ou nombre / booléen tels qu'écrits).
struct JsonValue {
	std::string text;
	bool isString = false;
	double num() const { return std::strtod(text.c_str(), nullptr); }
	bool truthy() const { return text == "true" || (!isString && num() != 0); }
};

// Lit un objet JSON plat : {"cmd":"set","id":"steam:123","value":42,"on":true}. Les valeurs imbriquées
// (objets, tableaux) sont ignorées. Renvoie faux si le texte n'est pas un objet.
inline bool JsonParseFlat(const std::string& s, std::map<std::string, JsonValue>& out) {
	size_t i = 0;
	auto ws = [&] { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; };
	auto readString = [&](std::string& dst) -> bool {
		if (i >= s.size() || s[i] != '"') return false;
		for (i++; i < s.size(); i++) {
			const char c = s[i];
			if (c == '"') { i++; return true; }
			if (c != '\\') { dst += c; continue; }
			if (++i >= s.size()) return false;
			switch (s[i]) {
			case 'n': dst += '\n'; break;
			case 'r': dst += '\r'; break;
			case 't': dst += '\t'; break;
			case 'b': dst += '\b'; break;
			case 'f': dst += '\f'; break;
			case 'u': {
				if (i + 4 >= s.size()) return false;
				unsigned cp = (unsigned)std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
				i += 4;
				// Paire de substitution UTF-16
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
					const unsigned lo = (unsigned)std::strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16);
					if (lo >= 0xDC00 && lo <= 0xDFFF) {
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						i += 6;
					}
				}
				if (cp < 0x80) dst += (char)cp;
				else if (cp < 0x800) { dst += (char)(0xC0 | (cp >> 6)); dst += (char)(0x80 | (cp & 0x3F)); }
				else if (cp < 0x10000) { dst += (char)(0xE0 | (cp >> 12)); dst += (char)(0x80 | ((cp >> 6) & 0x3F)); dst += (char)(0x80 | (cp & 0x3F)); }
				else { dst += (char)(0xF0 | (cp >> 18)); dst += (char)(0x80 | ((cp >> 12) & 0x3F)); dst += (char)(0x80 | ((cp >> 6) & 0x3F)); dst += (char)(0x80 | (cp & 0x3F)); }
				break;
			}
			default: dst += s[i]; // \" \\ \/
			}
		}
		return false;
	};
	auto skipNested = [&]() -> bool {
		int depth = 0;
		bool inStr = false;
		for (; i < s.size(); i++) {
			const char c = s[i];
			if (inStr) {
				if (c == '\\') i++;
				else if (c == '"') inStr = false;
				continue;
			}
			if (c == '"') inStr = true;
			else if (c == '{' || c == '[') depth++;
			else if (c == '}' || c == ']') {
				if (--depth == 0) { i++; return true; }
			}
		}
		return false;
	};
	ws();
	if (i >= s.size() || s[i] != '{') return false;
	i++;
	ws();
	if (i < s.size() && s[i] == '}') return true;
	while (i < s.size()) {
		ws();
		std::string key;
		if (!readString(key)) return false;
		ws();
		if (i >= s.size() || s[i] != ':') return false;
		i++;
		ws();
		JsonValue v;
		if (i < s.size() && s[i] == '"') {
			v.isString = true;
			if (!readString(v.text)) return false;
		} else if (i < s.size() && (s[i] == '{' || s[i] == '[')) {
			if (!skipNested()) return false;
			v.text.clear();
		} else {
			const size_t start = i;
			while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') i++;
			v.text = s.substr(start, i - start);
			if (v.text.empty()) return false;
		}
		out[key] = v;
		ws();
		if (i < s.size() && s[i] == ',') { i++; continue; }
		if (i < s.size() && s[i] == '}') return true;
		return false;
	}
	return false;
}

} // namespace sg
