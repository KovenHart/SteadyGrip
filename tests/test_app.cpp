// Tests sans casque de l'application : messages JSON de l'interface, fichiers de Steam.
#include "../src/app/Json.h"
#include "../src/common/Steam.h"
#include <cstdio>
using namespace sg;

static int fails = 0;
static void check(bool ok, const char* what) {
	std::printf("  %s : %s\n", ok ? "ok" : "ÉCHEC", what);
	if (!ok) fails++;
}

int main() {
	std::printf("1. Messages de l'interface\n");
	{
		std::map<std::string, JsonValue> m;
		check(JsonParseFlat(R"({"cmd":"set","id":"steam:123","key":"level","value":3})", m) && m["cmd"].text == "set" && m["id"].text == "steam:123" &&
			m["value"].num() == 3, "commande simple");
		m.clear();
		check(JsonParseFlat(R"({ "cmd" : "addExe", "path":"C:\\Jeux\\Mon \"jeu\"\\a.exe", "name":"\u00c9t\u00e9", "on":true, "off":false })", m) &&
			m["path"].text == "C:\\Jeux\\Mon \"jeu\"\\a.exe" && m["name"].text == "\xC3\x89t\xC3\xA9" && m["on"].truthy() && !m["off"].truthy(),
			"chemins Windows, guillemets, accents, booléens");
		m.clear();
		check(JsonParseFlat(R"({"cmd":"x","list":[1,{"a":"}"}],"obj":{"b":[2]},"after":"ok"})", m) && m["after"].text == "ok", "valeurs imbriquées ignorées");
		m.clear();
		check(!JsonParseFlat("pas du json", m) && !JsonParseFlat(R"({"a":)", m), "texte invalide refusé");
		check(JsonStr("a\"b\\c\n\x01") == "\"a\\\"b\\\\c\\n\\u0001\"", "échappement à l'écriture");
		// Aller-retour : ce que l'application écrit, l'interface le relit (et inversement pour les chaînes).
		m.clear();
		check(JsonParseFlat("{\"name\":" + JsonStr("Jeu \"spécial\" \\ test") + "}", m) && m["name"].text == "Jeu \"spécial\" \\ test", "aller-retour d'une chaîne");
	}

	std::printf("2. Fichiers de Steam\n");
	{
		const std::string acf = "\"AppState\"\n{\n\t\"appid\"\t\t\"1234560\"\n\t\"Universe\"\t\t\"1\"\n\t\"name\"\t\t\"CrossFire: Sierra Squad\"\n"
			"\t\"installdir\"\t\t\"CrossFire Sierra Squad\"\n\t\"InstalledDepots\"\n\t{\n\t\t\"1234561\"\n\t\t{\n\t\t\t\"manifest\"\t\t\"42\"\n\t\t}\n\t}\n}\n";
		const SteamGame g = ParseManifest(acf, "D:\\SteamLibrary");
		check(g.appId == 1234560 && g.name == "CrossFire: Sierra Squad" && g.installDir == "CrossFire Sierra Squad", "fichier appmanifest");
		const std::string vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"228980\"\t\t\"0\"\n\t\t}\n\t}\n"
			"\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n";
		const auto libs = ParseLibraryFolders(vdf);
		check(libs.size() == 2 && libs[0] == "C:\\Program Files (x86)\\Steam" && libs[1] == "D:\\SteamLibrary", "fichier libraryfolders");
		std::string root, dir;
		check(SplitSteamPath("D:\\SteamLibrary\\steamapps\\common\\CrossFire Sierra Squad\\CFSS\\Binaries\\Win64\\CFSS-Win64-Shipping.exe", root, dir) &&
			root == "D:\\SteamLibrary" && dir == "CrossFire Sierra Squad", "chemin d'un jeu Steam");
		check(SplitSteamPath("C:/Program Files (x86)/Steam/SteamApps/Common/Game/game.exe", root, dir) && dir == "Game", "barres obliques et majuscules");
		check(!SplitSteamPath("C:\\Jeux\\MonJeu\\MonJeu.exe", root, dir), "jeu hors Steam");
	}

	std::printf("%s\n", fails ? "ÉCHEC" : "OK");
	return fails;
}
