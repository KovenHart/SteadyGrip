# SteadyGrip : guide d'utilisation

*English: [GUIDE.en.md](GUIDE.en.md)* · [Retour à l'accueil](../README.md)

Tout ce que fait SteadyGrip, et ce qu'il faut savoir en cas de problème. Pour l'installer, voir l'[accueil](../README.md) ; les changements de chaque version sont dans le [CHANGELOG](../CHANGELOG.md).

## Pourquoi ça ne gêne pas le jeu

- La couche passe par le mécanisme officiel d'OpenXR (une DLL déclarée dans le registre, insérée par le chargeur entre le jeu et le runtime). Elle n'injecte rien dans le jeu et ne touche ni à l'image ni au casque : elle modifie seulement la pose des manettes que le jeu demande (`xrLocateSpace`, `xrLocateSpaces`), et lit au passage l'état des boutons grip (qu'elle ne corrige que si « Grip qui lâche tout seul » est activé pour le jeu).
- Elle ne fait que des calculs légers sur des positions : aucun coût mesurable sur les images par seconde.
- Le signal de perte vient de l'application : elle lit en mémoire, sans rien écrire, le journal du service Pimax. Rien n'est injecté chez Pimax et aucun fichier de Pimax n'est modifié.
- Sans l'application, ou si Pimax change le format de son journal, la couche continue à lisser, simplement sans le signal de perte.
- Pour un jeu, « Stabilité de la visée : Désactivée » rend la pose brute de Pimax ; Paramètres › Désinstaller retire la couche de tous les jeux.

## Mes jeux et leurs réglages

- **Un jeu lancé s'ajoute tout seul** à « Mes jeux ». La couche reconnaît le jeu par son programme ; pour un jeu installé par Steam, elle retrouve aussi son numéro Steam (fichiers `appmanifest_*.acf` de la bibliothèque Steam), même si son nom OpenXR est obscur (`CFSS0.0.1.0` pour CrossFire: Sierra Squad).
- **Ajouter un jeu à la main** : carte « Ajouter un jeu ». Onglet *Depuis Steam* : la liste des jeux installés avec Steam. Onglet *Autre jeu (.exe)* : choisissez le programme qui lance le jeu, un nom, et une image si vous voulez.
- **Jaquettes** : pour un jeu Steam, l'image est copiée depuis le cache de Steam sur votre PC, sinon téléchargée une fois sur le serveur d'images de Steam. Elles sont gardées dans `%LOCALAPPDATA%\SteadyGrip\jaquettes`. « Changer l'image » sur la page d'un jeu en met une autre.
- **Réglages** : un jeu utilise les **réglages par défaut** tant que vous ne changez rien sur sa page ; dès le premier changement, il a les siens. « Revenir aux réglages par défaut » les efface. Les réglages du jeu en cours s'appliquent tout de suite, sans relancer le jeu.
- **Stabilité de la visée** : Désactivée, Réactive, Équilibrée, Très stable (les préréglages ci-dessous). Les curseurs des **réglages avancés** donnent un réglage « personnalisé ».

Tout est dans `%LOCALAPPDATA%\SteadyGrip\settings.ini` : `[Filtre]` pour les réglages par défaut, `[Jeu:steam:<numéro>]` ou `[Jeu:exe:<programme>]` pour les réglages d'un jeu, `[Biblio:…]` et `[Bibliotheque]` pour la liste des jeux, `[Interface]` pour le thème. Les réglages par jeu créés avant la 0.6 (`[Jeu:<nom OpenXR>]`) sont repris automatiquement au premier lancement du jeu.

## Réglages

Les réglages sont enregistrés dans `%LOCALAPPDATA%\SteadyGrip\settings.ini` et appliqués en direct, même pendant le jeu.

| Préréglage | Lissage à l'arrêt | Lissage en mouvement | Étalement des téléportations | Stabilisation de l'orientation |
|---|---|---|---|---|
| Léger (« Réactive ») | 30 ms | 4 ms | 70 ms | 25 |
| Moyen (« Équilibrée ») | 60 ms | 6 ms | 100 ms | 50 |
| Fort (« Très stable ») | 100 ms | 30 ms | 140 ms | 75 |

## Ce que fait le filtre

Les mesures TrackProbe ont montré qu'entre deux images des caméras, la position des manettes est extrapolée par l'IMU, puis recalée d'un coup à chaque nouvelle mesure optique (environ 25 fois par seconde). Ces recalages sont la source des micro-tremblements. Quand une manette sort du champ des caméras (arme baissée en position de repos), elle n'est plus suivie que par l'IMU : au retour dans le champ, le recalage peut atteindre 30 à 45 cm.

Le filtre suit la trajectoire prévue par les vitesses que donne le runtime, et répartit chaque recalage dans le temps :

- **aucun retard ajouté au mouvement réel** : la sortie bouge exactement comme la pose brute, seuls les recalages sont étalés ;
- manette quasi immobile (visée stable) : étalement lent, réglé par « Lissage à l'arrêt » ;
- manette en mouvement rapide : étalement presque instantané, réglé par « Lissage en mouvement » (« Arme en mouvement » dans les réglages avancés, 0 à 80 ms, 30 ms en préréglage Fort). Monter à 45–60 ms supprime presque tous les à-coups restants en balayage rapide, au prix d'une main qui suit la pose de Pimax d'un peu plus loin. Mesuré sur trois enregistrements de Zero Caliber 2 (main gauche) : à-coups −75 % à 30 ms, −83 % à 45 ms, −86 % à 60 ms ; à-coups de plus d'1 cm : 60 → 8 → 8 ; écart moyen à la pose de Pimax 8,3 → 9,0 → 9,7 mm ;
- gros recalage, ou recalage après un passage hors champ : étalement linéaire, réglé par « Étalement des téléportations », sans dépasser 3 m/s.

Si le runtime ne fournit pas les vitesses, la couche les estime elle-même à partir des dernières poses (régression sur environ 50 ms). Sur les sessions TrackProbe, ce mode de secours donne des résultats proches de ceux obtenus avec les vraies vitesses.

## Résultats sur les sessions TrackProbe (rejouées à 90 Hz, préréglage Moyen)

| Session | Sauts d'une image à l'autre de plus de 1 mm (brut → filtré) | 95e centile (brut → filtré) | Écart moyen ajouté |
|---|---|---|---|
| Manettes en main, casque porté (G / D) | 264 → 2 / 87 → 3 | 1,04 → 0,38 / 0,70 → 0,27 mm | 0,5–0,6 mm |
| Manettes tenues fixes (G / D) | 30 → 0 / 432 → 23 (> 3 mm : 165 → 0) | 0,69 → 0,17 / 3,29 → 0,70 mm | 0,3–1,0 mm |
| Visée et repos (téléportations) | 287 mm → 41 mm max par image / 449 mm → 38 mm | — | — |

Le banc de test est dans `tests/bench.cpp` : `bench <fichier.csv> [hz] [préréglage 0-3] [t_début] [t_fin]`.

## Stabilisation de l'orientation (visée)

Dans beaucoup de jeux de tir, la visée dépend surtout de l'orientation de la main qui tient la poignée. Un filtre adaptatif (« One Euro ») lisse fortement l'orientation quand l'arme est presque immobile, et presque pas quand elle tourne vite. Mesuré sur des sessions réelles (réglage 50) : bruit de visée réduit de 40 à 70 %, retard en mouvement rapide inférieur à 1° en moyenne. Réglage de 0 (désactivé) à 100 ; 25 / 50 / 75 dans les préréglages Léger / Moyen / Fort. `orient_eval session.csv` refait la mesure sur une session TrackProbe.

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

Chaque perte est aussi ajoutée à `%LOCALAPPDATA%\SteadyGrip\SteadyGrip_pertes.csv` (une ligne par perte : date, jeu, main, angles, distance, durée, réinitialisation, écart à la reprise, grips, liaison). Ce fichier sert à tracer une vraie carte des angles morts.

Si l'application compagnon n'est pas lancée, ou pas en administrateur, la couche fonctionne comme avant (lissage seul).

### Pistes testées et écartées (0.5)

- **Prolonger l'élan de la main pendant les pertes courtes.** Pendant l'état « perdue », Pimax intègre déjà l'IMU : l'écart à la reprise est de 1 à 2 cm (médiane, sessions en jeu). Pendant la réinitialisation, Pimax fige la pose avec une vitesse nulle : il n'y a plus d'élan à prolonger. Sur les pertes de plus de 0,6 s des sessions TrackProbe, le modèle de bras actuel reste le meilleur : 7 cm d'écart médian, contre 16 cm en suivant l'estimation de Pimax et 17 cm en figeant la main.
- **Compenser le retard de la stabilisation de l'orientation par une prédiction.** Que la vitesse soit lissée ou prise au gyroscope, la prédiction réinjecte le tremblement. À tremblement égal, le gain est d'au plus 0,06°, et les vitesses angulaires du runtime prédisent moins bien que les orientations elles-mêmes. Le retard actuel, moins de 1° en moyenne, est le prix de la stabilité.

- **Contrôler l'état Pimax par les recalages (0.5, retiré en 0.5.1).** L'idée : ignorer une « perte » pendant laquelle la pose reçoit encore des recalages. Sur la pose brute de TrackProbe, il n'y en a aucun pendant les réinitialisations. Mais en jeu, la pose de la poignée est décalée du centre de la manette : quand on tourne le poignet, sa position bouge alors que Pimax a figé la position. Ces mouvements passaient pour des recalages, si bien que presque toutes les réinitialisations étaient ignorées puis reprises (165 fois sur une session de 10 minutes). La main alternait entre la pose figée de Pimax et le modèle de bras, d'où des dérives visibles.

### Main « suivie » hors de portée du bras

Il arrive que Pimax affiche une manette comme suivie alors qu'il se trompe de position : peu de LED visibles (arme contre le visage, en visée), reflet infrarouge. La main glisse alors loin du corps, puis saute à la bonne place quand Pimax se rattrape. Une main ne pouvant pas être plus loin du cou que la portée du bras (Réglages avancés, 85 cm par défaut), SteadyGrip bloque la position affichée à cette portée, plus 10 cm de marge : bras le long du corps, la position de visée de certains jeux, quelques centimètres devant le grip, atteint 94 à 99 cm du cou. L'erreur n'est pas supprimée, seulement réduite : dans un cas réel au fusil de précision, Pimax a placé la main à 1,25 m du cou, et l'écart maximal affiché passe de 84 à 58 cm. Ces positions ne sont pas apprises par le modèle de bras. Le journal note « main suivie hors de portée du bras ». Toujours actif quand « Contenir les pertes » l'est ; si ta main est bloquée bras tendu, augmente la portée du bras.

### Modèle de bras appris

Réglages avancés › Manette hors du champ › **« Modèle de bras appris sur tes gestes »**, activé par défaut depuis la 0.6.6 (il demande « Modèle de bras pendant les réinitialisations », coché lui aussi). Pendant une réinitialisation de Pimax, au lieu de faire tourner la main autour d'un coude supposé, SteadyGrip cherche dans les postures déjà vues celles où la manette était orientée de la même façon (et la tête inclinée de même), et place la main là où elle était alors, par rapport au buste.

- Le modèle apprend pendant que la manette est suivie, une image sur trois, y compris quand l'option est désactivée. Il garde les 5000 dernières postures par pose de manette, environ 3 min 30 de jeu suivi : il suit tes habitudes récentes plutôt que de tout accumuler, car c'est ce qui donne les meilleurs résultats en rejouant tes parties. Il faut environ une minute de jeu pour qu'il soit prêt ; avant, le modèle du coude est utilisé.
- Il décrit ton corps et ta façon de jouer, pas le jeu : un seul fichier pour tous les jeux, `%LOCALAPPDATA%\SteadyGrip\modeles\bras.bin`, lu au lancement d'un jeu et enregistré à sa fermeture. Le supprimer repart de zéro.
- Pas de saut : l'estimation part de la position affichée et rejoint la posture apprise en environ une seconde.
- Mesuré hors jeu en rejouant quatre parties enregistrées, chacune avec un modèle appris sur les trois autres ; écart à la reprise après une réinitialisation (jusqu'à 60 s : une visée à la lunette peut tenir la main contre le casque une demi-minute) :

  | Partie | Modèle du coude : médiane / 10 % les pires | Modèle appris : médiane / 10 % les pires |
  |---|---|---|
  | CrossFire (1) | 18 cm / 70 cm | 10 cm / 19 cm |
  | Gunman Contract | 27 cm / 61 cm | 14 cm / 28 cm |
  | CrossFire (2), arme à deux mains | 12 cm / 26 cm | 9 cm / 15 cm |
  | CrossFire (3), fusil de précision | 14 cm / 49 cm | 9 cm / 26 cm |

  Le modèle appris fait au moins aussi bien partout ; le gain le plus net porte sur les grosses erreurs.
- Limite : une main qui soutient une arme à deux mains dépend surtout de l'autre main. Pour celle-là, l'option « arme à deux mains » reste la plus juste.
- Le journal indique, à chaque réinitialisation, « (modèle appris) » ou « (modèle du coude : pas encore assez appris) ».

## Grip qui lâche tout seul

Avec le runtime OpenXR de Pimax, le bouton grip se relâche parfois une fraction de seconde alors qu'on le serre : la main s'ouvre et se referme, et l'arme peut tomber (Gunman Contract, par exemple). Pour ces jeux, activez **« Grip qui lâche tout seul »** sur la page du jeu.

- SteadyGrip corrige alors les actions du jeu liées au grip (`/input/squeeze`) : un relâchement plus court que le délai (60 ms par défaut, de 30 à 400 ms dans Réglages avancés › Bouton grip) est ignoré, et la valeur rendue au jeu reste celle du grip serré.
- Un vrai relâchement est pris en compte avec ce délai de retard.
- Le journal indique, à chaque bilan, le nombre de relâchements parasites masqués et le plus long. S'il signale des « relâchements suivis d'un nouvel appui en moins de 0,4 s », augmentez un peu le délai.
- Désactivé par défaut : dans les jeux où le grip fonctionne bien, il n'y a rien à corriger.

## Bips quand une manette sort du champ

Paramètres › Sons › **« Bip quand une manette sort du champ des caméras »**, désactivé par défaut. Un bip grave quand Pimax perd une manette, un bip aigu quand il la retrouve. Le son vient du côté de la manette concernée (gauche ou droite), pour ne pas le confondre avec les vibrations ou les sons du jeu. Le bouton « Écouter » fait entendre les quatre bips.

- Seulement pendant qu'un jeu tourne avec SteadyGrip, et pour les pertes de plus de 0,2 s : les clignotements très courts de Pimax ne bipent pas. Le bip aigu attend 0,15 s de suivi retrouvé, et ne suit qu'un bip grave.
- Joué par l'application SteadyGrip, pas par le jeu, sur la sortie audio par défaut de Windows : si le son du casque n'est pas la sortie par défaut, les bips sortent ailleurs.
- Utile pour apprendre où sont tes angles morts : au bip grave, ramène la manette devant toi.

## Enregistrer ses mouvements (diagnostic)

Paramètres › Diagnostic › **« Enregistrer les mouvements »** : pendant que l'interrupteur est activé, SteadyGrip note à chaque image la position du casque et des deux manettes, l'état de suivi donné par Pimax et ce que SteadyGrip a envoyé au jeu. Cela ne change rien en jeu.

- Un fichier par partie, `poses_AAAAMMJJ_HHMMSS.csv`, dans `%LOCALAPPDATA%\SteadyGrip\enregistrements` (environ 3 Mo par minute). L'enregistrement démarre avec le jeu, ou dès qu'on active l'interrupteur jeu lancé, et s'arrête quand on le désactive ou qu'on quitte le jeu.
- À quoi ça sert : rejouer hors jeu de vrais gestes pour mettre au point de nouvelles méthodes, par exemple un modèle de bras appris sur la façon de jouer de chacun, pour mieux deviner où est une manette sortie du champ des caméras.
- Un second fichier, `demandes_AAAAMMJJ_HHMMSS.csv`, note chaque demande de position faite par le jeu (en avant, au même instant, dans le passé), pour analyser la façon dont un jeu interroge les manettes.
- Les fichiers ne contiennent que des positions et des orientations, sans image ni son.

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

Pour comparer deux réglages : jouez au moins une minute avec chacun, en faisant les mêmes gestes.

## Réglages conseillés dans les jeux

**CrossFire: Sierra Squad** (testé avec un Pimax Dream Air SLAM) :
- **Épaulement automatique de l'arme : désactivé**, et **visée automatique des lunettes (sniper) : désactivée.** Elles ramènent la manette juste sous le casque, là où les caméras ne la voient presque plus (31 à 37 pertes par minute à moins de 25 cm du casque, contre moins de 2 entre 25 et 45 cm).
- Confirmé sur deux parties de 10 minutes avec les mêmes armes (fusil de précision, fusil d'assaut) : avec la visée automatique et l'arme au visage, la main droite perdait le suivi 6,9 fois par minute et passait 53 % du temps hors suivi ; sans visée automatique, manettes devant le casque, 0,6 fois par minute et 5 % du temps, sans aucune perte près du casque.
- **Tenir l'arme un peu en avant du visage**, plutôt que plaquée sous le casque : sur la même session, la main droite est passée de 37 pertes en 181 s à 6 en 145 s.
- **Impact de la seconde main sur la visée : 50 %** (80 % par défaut). L'arme est orientée surtout par la main qui tient la poignée, dont l'orientation (gyroscope) est très stable. 30 % marche aussi ; 50 % a donné le meilleur ressenti.

Avec SteadyGrip sur « Très stable » et « Armes à deux mains », ces réglages donnent la visée la plus stable mesurée : tremblement réduit de 70 à 76 %.

## Dépannage

- Journal : `%LOCALAPPDATA%\SteadyGrip\SteadyGrip.log`. Il indique si la couche s'est chargée dans le jeu et liste les espaces des manettes détectés.
- Si le jeu n'apparaît pas « EN COURS », il n'utilise peut-être pas OpenXR (jeu OpenVR / SteamVR natif), ou il utilise un autre runtime OpenXR que celui de Pimax Play.
- « Relance SteadyGrip en administrateur » : l'application n'a pas été lancée en administrateur.
- Le journal de la couche note chaque phase : « perte : dérive contenue », « réinitialisation Pimax : modèle de bras », « reprise : retour progressif ».
- « Version de Pimax non reconnue » : une mise à jour de Pimax a changé le journal. SteadyGrip continue à lisser, sans le signal de perte. Joignez `SteadyGrip.log` et le dernier `controller_*.txt` à un signalement (onglet Issues du dépôt) pour adapter la lecture.
- « Main gauche/droite : pas de données » alors que le jeu tourne : le jeu ne précise pas la main à la création de ses espaces de pose. Le filtrage s'applique quand même, mais les statistiques ne peuvent pas être attribuées à une main.
