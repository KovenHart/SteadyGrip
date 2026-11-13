# SteadyGrip

Stabilisation des manettes pour les casques dont les manettes sont suivies par les caméras du casque (Pimax Dream Air SLAM, etc.). SteadyGrip se compose de deux éléments :

- **une couche OpenXR implicite** (`SteadyGrip.dll`) : une fois installée, elle se charge automatiquement dans tous les jeux OpenXR ;
- **une application compagnon** (`SteadyGripApp.exe`) : elle installe la couche, garde la liste de tes jeux (avec leur jaquette), règle la stabilisation de chacun en direct et montre ce qui se passe pendant la partie. Son interface reprend les couleurs de Pimax Play, au choix **Pimax Classic** ou **Pimax EVO** (Paramètres › Apparence).

Seule la pose des manettes est modifiée. La pose du casque n'est jamais touchée.

*English: [README.en.md](README.en.md) · Nouveautés : [CHANGELOG.md](CHANGELOG.md)*

> SteadyGrip est un projet indépendant, sans lien avec Pimax. Il ne modifie aucun fichier ni aucun programme de Pimax : il lit seulement, en mémoire, l'état de suivi que le service Pimax écrit dans son propre journal.

## Télécharger

La dernière version compilée est dans [Releases](../../releases) : décompresse le fichier `SteadyGrip-<version>-win64.zip` dans un dossier définitif, puis suis la section [Installation](#installation).

## Ce que fait le filtre

Les mesures TrackProbe ont montré qu'entre deux images des caméras, la position des manettes est extrapolée par l'IMU, puis recalée d'un coup à chaque nouvelle mesure optique (environ 25 fois par seconde). Ces recalages sont la source des micro-tremblements. Quand une manette sort du champ des caméras (arme baissée en position de repos), elle n'est plus suivie que par l'IMU : au retour dans le champ, le recalage peut atteindre 30 à 45 cm.

Le filtre suit la trajectoire prévue par les vitesses que donne le runtime, et répartit chaque recalage dans le temps :

- **aucun retard ajouté au mouvement réel** : la sortie bouge exactement comme la pose brute, seuls les recalages sont étalés ;
- manette quasi immobile (visée stable) : étalement lent, réglé par « Lissage à l'arrêt » ;
- manette en mouvement rapide : étalement presque instantané, réglé par « Lissage en mouvement » ;
- gros recalage, ou recalage après un passage hors champ : étalement linéaire, réglé par « Étalement des téléportations », sans dépasser 3 m/s.

Si le runtime ne fournit pas les vitesses, la couche les estime elle-même à partir des dernières poses (régression sur environ 50 ms). Sur les sessions TrackProbe, ce mode de secours donne des résultats proches de ceux obtenus avec les vraies vitesses.

## Résultats sur les sessions TrackProbe (rejouées à 90 Hz, préréglage Moyen)

| Session | Sauts d'une image à l'autre de plus de 1 mm (brut → filtré) | 95e centile (brut → filtré) | Écart moyen ajouté |
|---|---|---|---|
| Manettes en main, casque porté (G / D) | 264 → 2 / 87 → 3 | 1,04 → 0,38 / 0,70 → 0,27 mm | 0,5–0,6 mm |
| Manettes tenues fixes (G / D) | 30 → 0 / 432 → 23 (> 3 mm : 165 → 0) | 0,69 → 0,17 / 3,29 → 0,70 mm | 0,3–1,0 mm |
| Visée et repos (téléportations) | 287 mm → 41 mm max par image / 449 mm → 38 mm | — | — |

Le banc de test est dans `tests/bench.cpp` : `bench <fichier.csv> [hz] [préréglage 0-3] [t_début] [t_fin]`.

## Pertes de suivi signalées par Pimax

Le service Pimax sait quand il perd une manette, mais ne le transmet pas aux jeux : il l'écrit seulement dans son journal (`C:\ProgramData\Pimax\slam\controller_*.txt`), qui n'arrive sur disque que toutes les 30 à 40 s. L'application compagnon lit ces lignes **dans le tampon d'écriture du journal, en mémoire du service Pimax**, dès qu'elles y sont écrites (moins de 2 ms avant la téléportation correspondante, mesuré avec LostWatch).

- **Lecture seule** : rien n'est écrit chez Pimax, aucun code n'est chargé dans ses processus, aucun fichier n'est modifié.
- C'est pour cela que **l'application demande les droits d'administrateur** au lancement : le service Pimax tourne avec des droits élevés. Le jeu, lui, reste lancé normalement.
- L'état de chaque manette passe ensuite par la mémoire partagée jusqu'à la couche, dans le jeu.

Ce que fait la couche (interrupteur **Garder l'arme en main hors du champ**), dans le **repère du buste** (le cou et le cap horizontal de la tête, lissé) : regarder son arme ou pencher la tête ne la déplace pas.

| État Pimax | Comportement |
|---|---|
| Suivie | normal (lissage seul) |
| Perdue | on garde l'estimation de Pimax, mais son déplacement depuis le début de la perte est limité (**Dérive maximale**, 30 cm par défaut), et la manette reste à portée de bras du buste (**Portée de bras**, 85 cm) |
| Réinitialisation (environ 0,5 s après la perte) | l'estimation de position de Pimax est abandonnée. **Modèle de bras** : la main tourne autour d'un coude estimé, selon l'orientation de la manette (gyroscope, toujours fiable) ; lever le poignet lève l'arme. Sans modèle de bras : arme figée |
| De nouveau suivie | retour progressif vers la vraie position, d'autant plus long que l'écart est grand (60 ms + 400 ms par mètre, 300 ms au plus) |

Pendant la partie, l'application affiche l'état de chaque main (vue par les caméras, hors champ, retour en douceur). Le journal note à chaque reprise l'**écart à la reprise** : la distance entre la position affichée par SteadyGrip et la vraie position quand Pimax retrouve la manette (ce qui reste à rattraper en douceur).

**Lier la main perdue à l'autre main** : l'interrupteur **Armes à deux mains** choisit le mode « deux grips serrés » (sinon : jamais). Les modes « automatique » et « toujours » restent possibles dans `settings.ini` (`LierMains=0` ou `1`). La liaison garde la main perdue à la même position par rapport à l'autre main. Désactivé par défaut : sur des données réelles, les poignets pivotent trop (16 à 30 cm de variation vue depuis l'autre manette) pour que ce soit fiable en permanence.

- **Deux grips serrés (arme à deux mains)** : la main perdue n'est liée que si les deux boutons grip sont serrés au début de la perte, l'autre main est suivie et les mains sont à 8–80 cm l'une de l'autre. Lâcher un grip pendant la perte rend la main indépendante (sans saut). La couche lit l'état des grips au passage, quand le jeu le demande (actions liées à `/input/squeeze`) ; le journal indique « Bouton grip lu pendant le jeu » pour chaque main. Si le jeu ne lit pas les grips séparément pour chaque main, la liaison ne s'active jamais (le journal le dit à chaque perte).
- Pendant la perte (avant la réinitialisation), la main liée peut encore s'écarter de 5 cm vers l'estimation de Pimax.
- Mesuré dans CrossFire (session du 13/11, main droite) : sur les pertes courtes, la main liée revient avec un écart médian de 0 cm, 3 cm au plus (114 pertes). La main indépendante revient avec 2 cm médians et jusqu'à 39 cm (84 pertes). Pendant les réinitialisations, le gain n'est pas établi : 11 cm médians sur 9 cas seulement.

### Mises à jour de Pimax

L'application lit la version du service Pimax et de son module de suivi des manettes, et la compare à la dernière version où la lecture a été vérifiée (section `[Pimax]` de `settings.ini`). Nouvelle version : « vérification en cours » jusqu'à ce que deux lignes lues en mémoire soient retrouvées ensuite dans le fichier du journal (quelques pertes de suivi suffisent), puis « nouvelle version, lecture vérifiée ».

L'application passe en **« incompatible »** si :
- le journal contient des noms d'états inconnus ;
- le fichier note des pertes que la mémoire ne montre jamais ;
- le tampon est perdu trois fois de suite sans qu'aucune ligne ne soit retrouvée.

La couche n'utilise alors plus le signal de perte. Le lissage et la stabilisation de l'orientation restent actifs, comme sans l'application. Si le service n'a plus son module de suivi habituel (`LedObjectPoseEstimator.dll`), l'application l'indique aussi.

### Carte des pertes

À chaque perte, la couche note où était la main vue du casque (repère des caméras) : azimut (positif à droite), élévation (négative sous le regard) et distance.
- Le journal l'indique sur la ligne « perte » : « main à 35° à droite, 62° sous le regard, 41 cm ».
- Chaque bilan donne, par zone, le nombre de **pertes par minute passée dans la zone**. Ce sont des taux et non des totaux : une zone où la main passe peu de temps n'est pas avantagée.
- En fin de session, la carte de toute la session est notée.

```
    [droite] carte des pertes (pertes par minute passée dans la zone) :
        hauteur : face (+15° à -25°) 2.1/min (11 en 310 s), bas (-25° à -55°) 6.0/min (4 en 40 s)
        côté : devant (±20°) 1.8/min (...), droite (20° à 55°) 5.2/min (...)
        distance : près du visage (< 25 cm) 9.4/min (...), 25 à 45 cm 2.0/min (...)
```

Chaque perte est aussi ajoutée à `%LOCALAPPDATA%\SteadyGrip\SteadyGrip_pertes.csv` (une ligne par perte : date, jeu, main, angles, distance, durée, réinitialisation, écart à la reprise, grips, liaison). Ce fichier sert à tracer une vraie carte et à appuyer le rapport à Pimax.

Si l'application compagnon n'est pas lancée, ou pas en administrateur, la couche fonctionne comme avant (lissage seul).

### Pistes testées et écartées (0.5)

- **Prolonger l'élan de la main pendant les pertes courtes.** Pendant l'état « perdue », Pimax intègre déjà l'IMU : l'écart à la reprise est de 1 à 2 cm (médiane, sessions en jeu). Pendant la réinitialisation, Pimax fige la pose avec une vitesse nulle : il n'y a plus d'élan à prolonger. Sur les pertes de plus de 0,6 s des sessions TrackProbe, le modèle de bras actuel reste le meilleur : 7 cm d'écart médian, contre 16 cm en suivant l'estimation de Pimax et 17 cm en figeant la main.
- **Compenser le retard de la stabilisation de l'orientation par une prédiction.** Que la vitesse soit lissée ou prise au gyroscope, la prédiction réinjecte le tremblement. À tremblement égal, le gain est d'au plus 0,06°, et les vitesses angulaires du runtime prédisent moins bien que les orientations elles-mêmes. Le retard actuel, moins de 1° en moyenne, est le prix de la stabilité.

- **Contrôler l'état Pimax par les recalages (0.5, retiré en 0.5.1).** L'idée : ignorer une « perte » pendant laquelle la pose reçoit encore des recalages. Sur la pose brute de TrackProbe, il n'y en a aucun pendant les réinitialisations. Mais en jeu, la pose de la poignée est décalée du centre de la manette : quand on tourne le poignet, sa position bouge alors que Pimax a figé la position. Ces mouvements passaient pour des recalages, si bien que presque toutes les réinitialisations étaient ignorées puis reprises (165 fois sur une session de 10 minutes). La main alternait entre la pose figée de Pimax et le modèle de bras, d'où des dérives visibles.

## Stabilisation de l'orientation (visée)

Dans beaucoup de jeux de tir, la visée dépend surtout de l'orientation de la main qui tient la poignée. Un filtre adaptatif (« One Euro ») lisse fortement l'orientation quand l'arme est presque immobile, et presque pas quand elle tourne vite. Mesuré sur des sessions réelles (réglage 50) : bruit de visée réduit de 40 à 70 %, retard en mouvement rapide inférieur à 1° en moyenne. Réglage de 0 (désactivé) à 100 ; 25 / 50 / 75 dans les préréglages Léger / Moyen / Fort. `orient_eval session.csv` refait la mesure sur une session TrackProbe.

## Mes jeux et leurs réglages

- **Un jeu lancé s'ajoute tout seul** à « Mes jeux ». La couche reconnaît le jeu par son programme ; pour un jeu installé par Steam, elle retrouve aussi son numéro Steam (fichiers `appmanifest_*.acf` de la bibliothèque Steam), même si son nom OpenXR est obscur (`CFSS0.0.1.0` pour CrossFire: Sierra Squad).
- **Ajouter un jeu à la main** : carte « Ajouter un jeu ». Onglet *Depuis Steam* : la liste des jeux installés avec Steam. Onglet *Autre jeu (.exe)* : choisis le programme qui lance le jeu, un nom, et une image si tu veux.
- **Jaquettes** : pour un jeu Steam, l'image est copiée depuis le cache de Steam sur ton PC, sinon téléchargée une fois sur le serveur d'images de Steam. Elles sont gardées dans `%LOCALAPPDATA%\SteadyGrip\jaquettes`. « Changer l'image » sur la page d'un jeu en met une autre.
- **Réglages** : un jeu utilise les **réglages par défaut** tant que tu ne changes rien sur sa page ; dès le premier changement, il a les siens. « Revenir aux réglages par défaut » les efface. Les réglages du jeu en cours s'appliquent tout de suite, sans relancer le jeu.
- **Stabilité de la visée** : Désactivée, Réactive, Équilibrée, Très stable (les préréglages ci-dessous). Les curseurs des **réglages avancés** donnent un réglage « personnalisé ».

Tout est dans `%LOCALAPPDATA%\SteadyGrip\settings.ini` : `[Filtre]` pour les réglages par défaut, `[Jeu:steam:<numéro>]` ou `[Jeu:exe:<programme>]` pour les réglages d'un jeu, `[Biblio:…]` et `[Bibliotheque]` pour la liste des jeux, `[Interface]` pour le thème. Les réglages par jeu créés avant la 0.6 (`[Jeu:<nom OpenXR>]`) sont repris automatiquement au premier lancement du jeu.

## Compilation

Prérequis : Visual Studio 2022 (charge de travail « Développement Desktop en C++ ») et Git. À la première configuration, CMake télécharge les en-têtes OpenXR (GitHub) et le SDK WebView2 de Microsoft (paquet NuGet `Microsoft.Web.WebView2`) : il faut une connexion Internet.

```bat
build.bat
```

`build.bat` compile, lance les tests et rassemble tout dans `dist\`. À la main :

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

GitHub Actions compile chaque envoi et prépare un brouillon de version pour chaque étiquette `v*` qui correspond au fichier `VERSION`.

Tout est produit dans `build\bin\Release\` : `SteadyGrip.dll`, `XR_APILAYER_NOVENDOR_steadygrip.json`, `SteadyGripApp.exe` et le dossier `ui` (l'interface). Ils doivent rester ensemble. `test_app.exe` vérifie la lecture des messages de l'interface et des fichiers de Steam. `test_lossguard.exe` vérifie le maintien de la manette pendant une perte, le modèle de bras, la liaison par les grips, les angles de la carte des pertes et la stabilisation de l'orientation (doit afficher `OK`).

## Installation

1. Copie les trois fichiers et le dossier `ui` dans un dossier définitif, par exemple `C:\Program Files\SteadyGrip`.
2. Lance `SteadyGripApp.exe` (Windows demande les droits administrateur) et clique sur **Installer** dans le bandeau du haut. La couche s'enregistre dans `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`.
3. Avant chaque session de jeu : lance `SteadyGripApp.exe`. Le bandeau du haut doit dire « Tout est prêt : lance ton jeu ». S'il demande de faire sortir une manette du champ, fais-le quelques secondes.
4. Lance un jeu OpenXR : il apparaît dans « Mes jeux » avec la mention « EN COURS », et sa page montre l'état de chaque main.

L'interface utilise WebView2, le moteur web de Windows, déjà installé avec Windows 10 et 11. S'il manque, l'application le dit : installe « Microsoft Edge WebView2 Runtime » depuis le site de Microsoft.

Pour désinstaller : Paramètres › **Désinstaller**. Pour désactiver la stabilisation d'un jeu : « Désactivée » dans sa stabilité de la visée. Pour un seul lancement, tu peux aussi définir la variable d'environnement `DISABLE_XR_APILAYER_NOVENDOR_steadygrip=1`.

## Réglages

Les réglages sont enregistrés dans `%LOCALAPPDATA%\SteadyGrip\settings.ini` et appliqués en direct, même pendant le jeu.

| Préréglage | Lissage à l'arrêt | Lissage en mouvement | Étalement des téléportations | Stabilisation de l'orientation |
|---|---|---|---|---|
| Léger (« Réactive ») | 30 ms | 4 ms | 70 ms | 25 |
| Moyen (« Équilibrée ») | 60 ms | 6 ms | 100 ms | 50 |
| Fort (« Très stable ») | 100 ms | 30 ms | 140 ms | 75 |

## Comparer des réglages pendant une partie

La couche note dans le journal (`%LOCALAPPDATA%\SteadyGrip\SteadyGrip.log`) chaque changement de réglages, une fois le curseur relâché depuis 1 s, et fait le bilan de la période précédente (si elle a duré au moins 5 s) :

```
=== Bilan sur 94 s (changement de réglages) - réglages : préréglage Personnalisé, lissage à l'arrêt 150 ms, en mouvement 6 ms, ...
    [droite] à-coups en mouvement : 0.41 mm/image (Pimax seul 0.73) ; tremblement de visée : 0.021°/image (Pimax seul 0.046) ; 9 pertes, 3 réinitialisations, écart moyen à la reprise 5 cm
Réglages modifiés : préréglage Personnalisé, lissage à l'arrêt 150 ms, en mouvement 20 ms, ...
```

- **À-coups en mouvement** : écart entre la position d'une image et la trajectoire lissée de ses voisines (5 images), quand la manette bouge (0,15 à 1,5 m/s), hors pertes. Un mouvement régulier, même rapide, donne zéro : seuls les sauts comptent. Plus bas = mouvement plus fluide.
- **Tremblement de visée** : variation de l'orientation d'une image à l'autre quand la manette est presque immobile. Plus bas = visée plus stable.
- « Pimax seul » : la même mesure sur la pose brute, avant SteadyGrip, pendant la même période.

Pour comparer deux réglages : joue au moins une minute avec chacun, en faisant les mêmes gestes.

## Réglages conseillés dans les jeux

**CrossFire: Sierra Squad** (testé avec un Pimax Dream Air SLAM) :
- **Épaulement automatique de l'arme : désactivé**, et **visée automatique des lunettes (sniper) : désactivée.** Elles ramènent la manette juste sous le casque, là où les caméras ne la voient presque plus (31 à 37 pertes par minute à moins de 25 cm du casque, contre moins de 2 entre 25 et 45 cm).
- **Tenir l'arme un peu en avant du visage**, plutôt que plaquée sous le casque : sur la même session, la main droite est passée de 37 pertes en 181 s à 6 en 145 s.
- **Impact de la seconde main sur la visée : 50 %** (80 % par défaut). L'arme est orientée surtout par la main qui tient la poignée, dont l'orientation (gyroscope) est très stable. 30 % marche aussi ; 50 % a donné le meilleur ressenti.

Avec SteadyGrip sur « Très stable » et « Armes à deux mains », ces réglages donnent la visée la plus stable mesurée : tremblement réduit de 70 à 76 %.

## Dépannage

- Journal : `%LOCALAPPDATA%\SteadyGrip\SteadyGrip.log`. Il indique si la couche s'est chargée dans le jeu et liste les espaces des manettes détectés.
- Si le jeu n'apparaît pas « EN COURS », il n'utilise peut-être pas OpenXR (jeu OpenVR / SteamVR natif), ou il utilise un autre runtime OpenXR que celui de Pimax Play.
- « Relance SteadyGrip en administrateur » : l'application n'a pas été lancée en administrateur.
- Le journal de la couche note chaque phase : « perte : dérive contenue », « réinitialisation Pimax : modèle de bras », « reprise : retour progressif ».
- « Version de Pimax non reconnue » : une mise à jour de Pimax a changé le journal. SteadyGrip continue à lisser, sans le signal de perte. Envoie `SteadyGrip.log` et le dernier `controller_*.txt` pour adapter la lecture.
- « Main gauche/droite : pas de données » alors que le jeu tourne : le jeu ne précise pas la main à la création de ses espaces de pose. Le filtrage s'applique quand même, mais les statistiques ne peuvent pas être attribuées à une main.

## Licence

MIT, voir [LICENSE](LICENSE). Composants tiers : [THIRD-PARTY.md](THIRD-PARTY.md).
