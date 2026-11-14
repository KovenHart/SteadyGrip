# Changelog

*English: [CHANGELOG.en.md](CHANGELOG.en.md)*

Les nouveautés de chaque version de SteadyGrip, la plus récente en premier.

## 0.6.6

- **Modèle de bras appris activé par défaut** : c'est la combinaison qui a donné les meilleurs résultats sur toutes les parties mesurées. Il n'est plus marqué expérimental. Un jeu dont les réglages ont déjà été enregistrés garde son choix.
- **Bips de suivi** (Paramètres › Sons, désactivé par défaut) : bip grave quand Pimax perd une manette plus de 0,2 s, bip aigu quand il la retrouve, du côté de la manette. Joués par l'application, seulement en jeu. Bouton « Écouter ».

## 0.6.5

- **Main « suivie » hors de portée du bras** : quand Pimax affiche une manette comme suivie mais la place plus loin du cou que la portée du bras (erreur de suivi, souvent en visée arme contre le visage), la position affichée est bloquée à cette portée (+5 cm). Cas réel au fusil de précision : écart maximal affiché de 84 cm ramené à 54 cm. Le journal note chaque épisode.
- **Modèle de bras appris** : mémoire ramenée aux 5000 dernières postures (environ 3 min 30 de jeu), qui suit mieux les habitudes récentes. En rejouant quatre parties : 9 à 14 cm d'écart médian au retour, et 15 à 28 cm pour les 10 % de pertes les pires (au lieu de 24 à 30 cm). Les fichiers de la 0.6.4 sont relus (postures les plus récentes gardées).
- `replay_eval` compte les réinitialisations jusqu'à 60 s : au fusil de précision, la main reste contre le casque jusqu'à 30 s pendant la visée.

## 0.6.4

- **Modèle de bras appris** (expérimental, Réglages avancés › Manette hors du champ, désactivé par défaut) : pendant une réinitialisation de Pimax, la main est placée là où elle se trouvait dans les postures déjà vues avec la même orientation de manette, au lieu de tourner autour d'un coude supposé. Le modèle apprend en continu, est commun à tous les jeux et est gardé d'une partie à l'autre (`%LOCALAPPDATA%\SteadyGrip\modeles\bras.bin`). En rejouant quatre parties enregistrées (CrossFire trois fois, Gunman Contract) : écart à la reprise de 10 à 13 cm en médiane au lieu de 12 à 27 cm, et de 24 à 30 cm au lieu de 26 à 70 cm pour les 10 % de pertes les pires.
- `replay_eval.exe` rejoue un enregistrement de mouvements et compare les deux modèles de bras.

## 0.6.3

- **Enregistrer les mouvements** (Paramètres › Diagnostic, désactivé par défaut) : un fichier CSV par partie avec, à chaque image, la position du casque et des deux manettes, l'état de suivi Pimax et la pose envoyée au jeu. Il servira à mettre au point un modèle de bras appris sur les gestes du joueur, pour mieux deviner la position d'une manette hors du champ des caméras.

## 0.6.2

- **Lissage en mouvement réparé pour les jeux Unity** (Gunman Contract, par exemple). Ces jeux demandent chaque position de manette deux fois : une fois pour préparer l'image suivante, puis de nouveau pour l'image en cours, avec des capteurs plus frais. La seconde réponse de Pimax différait un peu de la première, et cet écart passait tel quel dans l'image : le lissage en mouvement n'agissait plus, et pouvait même ajouter des à-coups. SteadyGrip absorbe maintenant cet écart comme un recalage ordinaire. Mesuré dans Gunman Contract (« Arme en mouvement » à 30 ms) : à-coups −77 % et −76 %, tremblement de visée −82 % et −76 % (mains gauche et droite), contre 0 à −9 % avant. Les jeux qui ne demandent qu'une fois par image ne changent pas.
- Le journal indique, toutes les 10 s, à quels instants le jeu demande la pose et combien de secondes demandes ont été corrigées. Les mesures d'à-coups et de tremblement portent sur la pose réellement affichée.
- Délai par défaut de « Grip qui lâche tout seul » ramené de 120 à 60 ms : dans Gunman Contract, les relâchements parasites mesurés durent 29 ms au plus (32 masqués en deux parties, aucun plus long). Un vrai relâchement est donc pris en compte deux fois plus vite. Les jeux dont le délai a déjà été réglé gardent leur valeur.

## 0.6.1

- **Grip qui lâche tout seul** (nouvel interrupteur sur la page d'un jeu, désactivé par défaut) : avec le runtime OpenXR de Pimax, le bouton grip se relâche parfois une fraction de seconde. La main s'ouvre et se referme, et dans Gunman Contract, par exemple, l'arme tombe. SteadyGrip ignore maintenant les relâchements de moins de 120 ms (réglable de 30 à 400 ms dans les réglages avancés). Un vrai relâchement est pris en compte avec ce délai.
- Le journal compte, à chaque bilan, les relâchements parasites masqués et leur durée maximale, pour ajuster le délai.

## 0.6.0

- **Nouvelle interface**, pensée pour quelqu'un qui n'y connaît rien. Une page « Mes jeux » avec les jaquettes, et un bandeau qui dit en une phrase si tout est prêt, ou ce qu'il faut faire. Sur la page d'un jeu : un seul choix, « Stabilité de la visée » (Désactivée, Réactive, Équilibrée, Très stable), et deux interrupteurs expliqués. Pendant la partie, elle montre l'état de chaque main et le tremblement évité. Les curseurs fins restent dans « Réglages avancés ».
- **Thèmes Pimax Classic et Pimax EVO** (Paramètres › Apparence) : les couleurs des deux clients de Pimax.
- **Bibliothèque de jeux.** Un jeu lancé s'ajoute tout seul, reconnu par son programme et par son numéro Steam (CrossFire: Sierra Squad n'apparaît plus sous « CFSS0.0.1.0 »). On peut aussi ajouter un jeu à la main : depuis la liste des jeux Steam installés, ou en choisissant un `.exe`.
- **Jaquettes** : copiées depuis le cache de Steam du PC, sinon téléchargées une fois sur le serveur d'images de Steam. On peut choisir sa propre image.
- **Réglages par jeu** rangés par numéro Steam ou par programme. Les profils créés avant la 0.6 sont repris automatiquement.
- « Très stable » passe à 30 ms de lissage en mouvement : la valeur de la meilleure session mesurée.
- **Icône SteadyGrip**, et **raccourcis sur le bureau et dans le menu Démarrer**, créés à l'installation. On peut les retirer dans Paramètres › Raccourcis.
- **Installation en un double-clic** : `Installer.bat` copie le programme dans `C:\SteadyGrip`, déclare la couche et crée les raccourcis ; `Desinstaller.bat` retire la couche et les raccourcis.
- L'interface utilise WebView2, le moteur web de Windows. Le SDK est téléchargé à la compilation.

## 0.5.2

- Le bilan de fin de partie n'est plus vide : le jeu détruit ses espaces (dont l'espace de base) avant la session, et le bilan est maintenant fait avant.

## 0.5.1

- **Retrait du contrôle de l'état Pimax par les recalages, ajouté en 0.5.** En jeu, la poignée de l'arme est décalée du centre de la manette : quand on tourne le poignet, elle bouge alors que Pimax a figé la position. Ces mouvements passaient pour des recalages, si bien que presque toutes les réinitialisations étaient ignorées puis reprises. La main alternait entre la pose figée de Pimax et le modèle de bras, d'où des dérives visibles.

## 0.5.0

- **Carte des pertes** : à chaque perte, la position de la main vue du casque (angle, distance), et les pertes par minute passée dans chaque zone. Le tout est aussi enregistré dans `SteadyGrip_pertes.csv`. C'est elle qui a mis en évidence l'angle mort juste sous le casque.
- **Armes à deux mains** : quand les deux grips sont serrés, la main perdue suit l'autre main.
- **Mises à jour de Pimax** : l'application vérifie la version de Pimax et la lecture du journal. Si le format change, elle passe en « incompatible » et le lissage continue seul.
- Pistes testées et écartées : prolonger l'élan de la main pendant les pertes, prédire l'orientation pour compenser le retard du lissage.

## 0.4

- **Signal de perte Pimax en temps réel.** L'application lit, en mémoire et sans rien modifier, l'état de chaque manette noté par le service Pimax. Pendant une perte, la dérive est limitée et l'arme reste à portée de bras. Pendant une réinitialisation, un modèle de bras prend le relais. Au retour dans le champ, l'arme rejoint la vraie position en douceur.
- Stabilisation de l'orientation (visée), profils par jeu, bilans par période de réglages dans le journal.

## 0.1 à 0.3

- Couche OpenXR de lissage des recalages optiques (micro-tremblements, téléportations au retour dans le champ) et application de réglage.
