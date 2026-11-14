# Changelog

*Français : [CHANGELOG.md](CHANGELOG.md)*

What's new in each SteadyGrip version, most recent first.

## 0.6.4

- **Learned arm model** (experimental, "Modèle de bras appris" in Réglages avancés › Manette hors du champ, off by default): during a Pimax reinitialization, the hand is placed where it was in the postures already seen with the same controller orientation, instead of rotating around an assumed elbow. The model keeps learning, is shared by all games and kept between sessions (`%LOCALAPPDATA%\SteadyGrip\modeles\bras.bin`). Replaying four recorded sessions (CrossFire three times, Gunman Contract): gap at recovery of 10 to 13 cm median instead of 12 to 27 cm, and 24 to 30 cm instead of 26 to 70 cm for the worst 10 % of losses.
- `replay_eval.exe` replays a movement recording and compares both arm models.

## 0.6.3

- **Record movements** ("Enregistrer les mouvements" in Paramètres › Diagnostic, off by default): one CSV file per session with, every frame, the headset and both controllers' poses, the Pimax tracking state and the pose sent to the game. It will be used to develop an arm model learned from the player's own movements, to better guess where a controller is when it leaves the cameras' view.

## 0.6.2

- **Motion smoothing fixed for Unity games** (Gunman Contract, for example). These games request each controller pose twice: once to prepare the next frame, then again for the current frame, with fresher sensor data. Pimax's second answer differed slightly from the first, and that difference went straight into the image: motion smoothing no longer worked, and could even add jerks. SteadyGrip now absorbs that difference like an ordinary correction. Measured in Gunman Contract ("Arme en mouvement" at 30 ms): jerks −77 % and −76 %, aiming jitter −82 % and −76 % (left and right hands), versus 0 to −9 % before. Games that request once per frame are unchanged.
- Every 10 s, the log shows when the game requests the pose and how many second requests were corrected. Jerk and jitter measurements now use the pose actually displayed.
- Default delay of "Grip qui lâche tout seul" lowered from 120 to 60 ms: in Gunman Contract, the measured spurious releases last 29 ms at most (32 masked over two sessions, none longer). A real release now takes effect twice as fast. Games whose delay was already set keep their value.

## 0.6.1

- **Grip that lets go by itself** (new switch on a game's page, off by default): with Pimax's OpenXR runtime, the grip button sometimes releases for a fraction of a second. The hand opens and closes, and in Gunman Contract, for example, the weapon drops. SteadyGrip now ignores releases shorter than 120 ms (adjustable from 30 to 400 ms in the advanced settings). A real release takes effect after that delay.
- At each summary, the log counts the masked spurious releases and their longest duration, to tune the delay.

## 0.6.0

- **New interface**, designed for people who know nothing about the subject. A "My games" page with cover art, and a banner that says in one sentence whether everything is ready, or what to do. On a game's page: one main choice, "Aiming stability" (Off, Responsive, Balanced, Very stable), and two explained switches. During play, it shows the state of each hand and how much jitter was removed. Fine-tuning sliders stay in "Advanced settings".
- **Pimax Classic and Pimax EVO themes** (Settings › Appearance): the colors of Pimax's two clients.
- **Game library.** A game you launch is added automatically, recognized by its executable and its Steam app ID (CrossFire: Sierra Squad no longer shows up as "CFSS0.0.1.0"). Games can also be added by hand, from the list of installed Steam games or by picking an `.exe`.
- **Cover art**: copied from Steam's cache on the PC, otherwise downloaded once from Steam's image server. You can pick your own image.
- **Per-game settings** stored by Steam app ID or by executable. Profiles created before 0.6 are migrated automatically.
- "Very stable" now uses 30 ms of motion smoothing, the value from the best measured session.
- **SteadyGrip icon**, and **desktop and Start menu shortcuts** created at install time. They can be removed in Settings › Raccourcis.
- **One double-click install**: `Installer.bat` copies the program to `C:\SteadyGrip`, registers the layer and creates the shortcuts; `Desinstaller.bat` removes the layer and the shortcuts.
- The interface uses WebView2, Windows' web engine. The SDK is downloaded at build time.

## 0.5.2

- The end-of-game summary is no longer empty: the game destroys its spaces (including the base space) before the session, and the summary is now written before that.

## 0.5.1

- **Removed the check of Pimax's state against corrections, added in 0.5.** In game, the weapon grip is offset from the controller's center: turning the wrist moves it while Pimax has frozen the position. Those moves were mistaken for corrections, so almost every reinitialization was ignored and then resumed. The hand alternated between Pimax's frozen pose and the arm model, causing visible drift.

## 0.5.0

- **Loss map**: for each loss, where the hand was as seen from the headset (angle, distance), and losses per minute spent in each zone. Everything is also logged to `SteadyGrip_pertes.csv`. This is what revealed the blind spot just below the headset.
- **Two-handed weapons**: when both grips are squeezed, a lost hand follows the other hand.
- **Pimax updates**: the app checks Pimax's version and that the log is still being read correctly. If the format changes, it switches to "incompatible" and smoothing keeps working on its own.
- Ideas tested and dropped: extending the hand's momentum during losses, predicting orientation to offset smoothing lag.

## 0.4

- **Real-time Pimax loss signal.** The app reads, in memory and without modifying anything, the state of each controller as logged by the Pimax service. During a loss, drift is capped and the weapon stays within arm's reach. During a reinitialization, an arm model takes over. When the controller is back in view, the weapon rejoins its true position smoothly.
- Orientation (aiming) stabilization, per-game profiles, per-settings summaries in the log.

## 0.1 to 0.3

- OpenXR layer smoothing optical corrections (micro-jitter, teleports when the controller comes back into view), and a settings app.
