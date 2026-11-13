# SteadyGrip : développement

*English: [DEVELOPMENT.md](DEVELOPMENT.md)* · [Retour à l'accueil](../README.md)

Compiler depuis les sources, construire et publier le paquet, organisation du code. Pour simplement utiliser SteadyGrip, voir l'[accueil](../README.md).

## Les programmes

| Fichier | Rôle |
|---|---|
| `SteadyGrip.dll` (`src/layer/`) | La couche OpenXR implicite, chargée dans le jeu : lissage des recalages, stabilisation de l'orientation, contention des pertes, carte des pertes, journal. |
| `SteadyGripApp.exe` (`src/app/`) | L'application compagnon (administrateur) : installation de la couche et des raccourcis, réglages, bibliothèque de jeux et jaquettes, lecture en mémoire de l'état de suivi Pimax. |
| `ui/` | L'interface de l'application (HTML, CSS, JavaScript), affichée par WebView2. |
| `src/common/` | Partagé par les deux : filtre (`PoseFilter.h`), mémoire partagée et réglages (`Shared.h`), fichiers de Steam (`Steam.h`), lecture du tampon du journal Pimax (`LogBuffer.h`). |

## Prérequis

- Windows 10 ou 11, 64 bits.
- Visual Studio 2022 avec la charge **« Développement Desktop en C++ »** (CMake est inclus), et Git.
- Une connexion Internet à la première configuration : CMake télécharge les en-têtes OpenXR (GitHub) et le SDK WebView2 (paquet NuGet `Microsoft.Web.WebView2`, en-têtes et chargeur statique).

## Compiler

`build.bat` compile, lance les tests et rassemble le résultat dans `dist\` :

```
dist\
  SteadyGrip.dll                         la couche
  XR_APILAYER_NOVENDOR_steadygrip.json   sa déclaration pour OpenXR
  SteadyGripApp.exe                      l'application
  ui\                                    son interface
```

À la main :

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Ces fichiers doivent rester ensemble. Lancer `SteadyGripApp.exe` puis **Installer** déclare la couche dans `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` (avec le chemin du `.json` de ce dossier) et crée les raccourcis. En ligne de commande : `SteadyGripApp.exe --install` ou `--uninstall` (couche et raccourcis).

L'interface peut s'ouvrir dans un navigateur (`ui/index.html`) : sans l'application, elle affiche des données d'exemple.

## Tests

| Programme | Ce qu'il vérifie |
|---|---|
| `test_lossguard.exe` | maintien de la manette pendant une perte, modèle de bras, retour proportionné, liaison par les grips, angles de la carte des pertes, stabilisation de l'orientation |
| `test_app.exe` | messages JSON entre l'interface et l'application, lecture des fichiers de Steam |
| `bench.exe`, `orient_eval.exe`, `motion_eval.exe` (dans `tests/`) | rejouent une session TrackProbe (CSV) pour mesurer le lissage |

## Version et distribution

- Le numéro de version est dans `VERSION` (et `kVersion` dans `src/common/Shared.h`).
- GitHub Actions (`.github/workflows/build.yml`) compile chaque envoi sur Windows, lance les tests et joint le paquet `SteadyGrip-<version>-win64.zip` au résultat (onglet Actions).
- Pour publier : `git tag v<version>` puis `git push origin v<version>`. L'étiquette doit correspondre à `VERSION`. Un brouillon de version est créé dans Releases avec le paquet : le relire, puis « Publish release ».
- Le paquet contient `Installer.bat`, `Desinstaller.bat`, `LISEZ-MOI.txt`, `README-EN.txt` et le programme dans `programme\`. `Installer.bat` marche aussi depuis les sources : il compile d'abord avec `build.bat`.

## Organisation du code

- `src/common/PoseFilter.h` : `PoseFilter` (recalages étalés sans retard ajouté), `OrientSmoother` (One Euro sur l'orientation), `BodyFrame` (repère du buste), `LossGuard` (perte, réinitialisation, retour, liaison des mains), `HeadAngles` et `LossZones` (carte des pertes). Sans dépendance : utilisé aussi par les tests sur Linux.
- `src/layer/layer.cpp` : fonctions OpenXR interceptées, identification des mains et des grips, journal, bilans par période, fichier `SteadyGrip_pertes.csv`.
- `src/app/PimaxReader.h` : recherche du tampon du journal dans `pi_server.exe` (lecture seule), vérification par le fichier, version de Pimax, passage en « incompatible ».
- `src/app/app.cpp` : fenêtre WebView2, messages avec l'interface, réglages par jeu, ajout automatique du jeu lancé, raccourcis.
- `src/app/Library.h`, `src/common/Steam.h` : bibliothèque de jeux, jeux Steam installés, jaquettes.
- Mémoire partagée `Local\SteadyGripShared_v8` (`Shared.h`) : réglages (application → couche), statistiques par main et identité du jeu (couche → application), état de suivi Pimax (application → couche).
