# Changelog

*Français : [CHANGELOG.md](CHANGELOG.md)*

What's new in each SteadyGrip version, most recent first.

## 0.6.10

- **Sudden reversals in Zero Caliber 2**: when the second request for the same instant brought a large gap (hand reversing at 2–3 m/s), the raw pose went through as is for that request, then the next request took it back as a spread-out teleport: the hand alternated by 4 to 5 cm from one request to the next. That large gap is now spread out right away, as a teleport. Replayed on both Zero Caliber 2 recordings: sweep jerk −65 % → −75 % (left hand) and −65 % → −82 % (right hand). CrossFire, Gunman Contract and the Unity simulation: identical results.

## 0.6.9

- **Frozen Pimax poses**: in Zero Caliber 2, the first request for each instant gets a frozen pose from Pimax (the previous instant's), the second a fresh one. During fast sweeps the gap (~3 cm) exceeded the teleport threshold and the output jumped between the two every frame. SteadyGrip now replaces a frozen pose with its own prediction. Replayed on the Zero Caliber 2 recording: left-hand jerk while sweeping −2 % → −64 % (right hand unchanged, −59 %). Replayed on CrossFire and Gunman Contract: smoothing slightly better (+2 to +4 points), no regression.

## 0.6.8

- **Instant requested again later**: some games (Zero Caliber 2) request each instant twice, one frame ahead, then again on the next frame. SteadyGrip now returns the same answer for that instant, instead of applying an offset computed for another (sometimes frozen) Pimax pose. Measured in Zero Caliber 2 before the fix: the left hand was only smoothed by −15 % during sweeps (−71 % for the right).
- **Diagnostics**: the log counts instants requested again later and Pimax's frozen poses; movement recording adds a `demandes_….csv` file with every game request.

## 0.6.7

- **Hand out of arm's reach**: margin raised from 5 to 10 cm. In Zero Caliber 2, with the arm hanging, the hand's aim pose (a few centimetres in front of the grip) reaches 94 to 99 cm from the neck and was wrongly held. Measured Pimax errors (1.03 to 1.52 m) are still held; real sniper-rifle case: largest displayed gap from 84 to 58 cm (54 cm with the old margin).

## 0.6.6

- **Learned arm model on by default**: it is the combination that gave the best results on every measured session. No longer marked experimental. A game whose settings were already saved keeps its choice.
- **Tracking beeps** ("Sons" in Paramètres, off by default): a low beep when Pimax loses a controller for more than 0.2 s, a high beep when it finds it again, from the controller's side. Played by the app, only in game. "Écouter" (Listen) button.

## 0.6.5

- **"Tracked" hand out of arm's reach**: when Pimax shows a controller as tracked but puts it farther from the neck than the arm's reach (tracking error, often while aiming with the weapon against the face), the displayed position is held at that reach (+5 cm). Real case with a sniper rifle: largest displayed gap down from 84 to 54 cm. The log records each episode.
- **Learned arm model**: memory reduced to the last 5000 postures (about 3½ minutes of play), which follows recent habits better. Replaying four sessions: 9 to 14 cm median gap at recovery, and 15 to 28 cm for the worst 10 % of losses (instead of 24 to 30 cm). 0.6.4 files are read back (most recent postures kept).
- `replay_eval` counts reinitializations up to 60 s: with a sniper rifle, the hand stays against the headset for up to 30 s while aiming.

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
