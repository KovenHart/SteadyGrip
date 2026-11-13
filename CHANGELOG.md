# Changelog

*English: [CHANGELOG.en.md](CHANGELOG.en.md)*

Les nouveautés de chaque version de SteadyGrip, la plus récente en premier.

## 0.6.2

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
