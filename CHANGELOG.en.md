# Changelog

*Français : [CHANGELOG.md](CHANGELOG.md)*

What's new in each SteadyGrip version, most recent first.

## 0.6.0

- **New interface**, designed for people who know nothing about the subject. A "My games" page with cover art, and a banner that says in one sentence whether everything is ready, or what to do. On a game's page: one main choice, "Aiming stability" (Off, Responsive, Balanced, Very stable), and two explained switches. During play, it shows the state of each hand and how much jitter was removed. Fine-tuning sliders stay in "Advanced settings".
- **Pimax Classic and Pimax EVO themes** (Settings › Appearance): the colors of Pimax's two clients.
- **Game library.** A game you launch is added automatically, recognized by its executable and its Steam app ID (CrossFire: Sierra Squad no longer shows up as "CFSS0.0.1.0"). Games can also be added by hand, from the list of installed Steam games or by picking an `.exe`.
- **Cover art**: copied from Steam's cache on the PC, otherwise downloaded once from Steam's image server. You can pick your own image.
- **Per-game settings** stored by Steam app ID or by executable. Profiles created before 0.6 are migrated automatically.
- "Very stable" now uses 30 ms of motion smoothing, the value from the best measured session.
- **SteadyGrip icon**, and **desktop and Start menu shortcuts** created at install time. They can be removed in Settings › Raccourcis.
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
