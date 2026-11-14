# SteadyGrip: user guide

*Français : [GUIDE.md](GUIDE.md)* · [Back to home](../README.en.md)

Everything SteadyGrip does, and what to know when something goes wrong. To install it, see the [home page](../README.en.md); changes in each version are in the [CHANGELOG](../CHANGELOG.en.md). The app's interface is in French; its labels are quoted here with a translation.

## Why it doesn't get in the game's way

- The layer uses OpenXR's official mechanism (a DLL declared in the registry, inserted by the loader between the game and the runtime). It injects nothing into the game and touches neither the image nor the headset: it only changes the controller poses the game asks for (`xrLocateSpace`, `xrLocateSpaces`), and reads the grip buttons' state along the way (it only corrects it when "Grip qui lâche tout seul" is on for that game).
- It only does light math on positions: no measurable cost on frame rate.
- The loss signal comes from the app: it reads the Pimax service's log in memory, without writing anything. Nothing is injected into Pimax and no Pimax file is modified.
- Without the app, or if Pimax changes its log format, the layer keeps smoothing, just without the loss signal.
- For one game, "Stabilité de la visée : Désactivée" (aiming stability: off) passes Pimax's raw pose through; Paramètres › Désinstaller removes the layer from all games.

## My games and their settings

- **A game you launch is added to "Mes jeux" automatically.** The layer recognizes it by its executable; for a Steam game, it also finds its Steam app ID (Steam library `appmanifest_*.acf` files), even when its OpenXR name is cryptic (`CFSS0.0.1.0` for CrossFire: Sierra Squad).
- **Adding a game by hand**: "Ajouter un jeu" card. *Depuis Steam* tab: the list of games installed with Steam. *Autre jeu (.exe)* tab: pick the program that starts the game, a name, and optionally an image.
- **Cover art**: for a Steam game, the image is copied from Steam's cache on your PC, otherwise downloaded once from Steam's image server. Covers are kept in `%LOCALAPPDATA%\SteadyGrip\jaquettes`. "Changer l'image" on a game's page sets another one.
- **Settings**: a game uses the **default settings** ("Réglages par défaut") until you change something on its page; from the first change, it has its own. "Revenir aux réglages par défaut" deletes them. Settings for the running game apply immediately, without restarting the game.
- **Aiming stability**: Désactivée, Réactive, Équilibrée, Très stable (off, responsive, balanced, very stable: the presets below). The **advanced settings** sliders give a "custom" setting.

Everything is in `%LOCALAPPDATA%\SteadyGrip\settings.ini`: `[Filtre]` for default settings, `[Jeu:steam:<id>]` or `[Jeu:exe:<program>]` for a game's settings, `[Biblio:…]` and `[Bibliotheque]` for the game list, `[Interface]` for the theme. Per-game settings created before 0.6 (`[Jeu:<OpenXR name>]`) are migrated automatically the first time the game runs.

## Presets

| Preset | Smoothing at rest | Smoothing in motion | Teleport spreading | Orientation stabilization |
|---|---|---|---|---|
| Light ("Réactive") | 30 ms | 4 ms | 70 ms | 25 |
| Medium ("Équilibrée") | 60 ms | 6 ms | 100 ms | 50 |
| Strong ("Très stable") | 100 ms | 30 ms | 140 ms | 75 |

## What the filter does

TrackProbe measurements showed that between two camera frames, the controller position is extrapolated by the IMU, then snapped to each new optical measurement (about 25 times per second). These corrections cause the micro-jitter. When a controller leaves the cameras' view (weapon lowered at rest), only the IMU tracks it: when it comes back into view, the correction can reach 30–45 cm.

The filter follows the trajectory predicted by the runtime's velocities and spreads each correction over time:

- **no lag added to real motion**: the output moves exactly like the raw pose, only corrections are spread;
- controller almost still (steady aim): slow spreading, set by "smoothing at rest";
- controller moving fast: almost instant spreading, set by "smoothing in motion";
- large correction, or correction after being out of view: linear spreading, set by "teleport spreading", never faster than 3 m/s.

If the runtime does not provide velocities, the layer estimates them from recent poses (regression over about 50 ms).

## Results on TrackProbe sessions (replayed at 90 Hz, Medium preset)

| Session | Frame-to-frame jumps over 1 mm (raw → filtered) | 95th percentile (raw → filtered) | Mean offset added |
|---|---|---|---|
| Controllers in hand, headset on (L / R) | 264 → 2 / 87 → 3 | 1.04 → 0.38 / 0.70 → 0.27 mm | 0.5–0.6 mm |
| Controllers held still (L / R) | 30 → 0 / 432 → 23 (> 3 mm: 165 → 0) | 0.69 → 0.17 / 3.29 → 0.70 mm | 0.3–1.0 mm |
| Aiming and rest (teleports) | 287 mm → 41 mm max per frame / 449 mm → 38 mm | — | — |

The test bench is `tests/bench.cpp`: `bench <file.csv> [hz] [preset 0-3] [t_start] [t_end]`.

## Orientation (aiming) stabilization

In many shooters, aim depends mostly on the orientation of the hand holding the grip. An adaptive "One Euro" filter smooths orientation strongly when the weapon is almost still, and barely when it turns fast. Measured on real sessions (setting 50): aiming noise reduced by 40–70 %, lag under 1° on average during fast motion. Range 0 (off) to 100; 25 / 50 / 75 in the Light / Medium / Strong presets.

## Tracking losses reported by Pimax

The Pimax service knows when it loses a controller, but does not pass it on to games: it only writes it to its log (`C:\ProgramData\Pimax\slam\controller_*.txt`), which reaches the disk only every 30–40 s. The companion app reads those lines **from the log's write buffer, in the Pimax service's memory**, as soon as they are written (under 2 ms before the matching teleport, measured with LostWatch).

- **Read-only**: nothing is written into Pimax, no code is loaded into its processes, no file is modified.
- This is why **the app asks for administrator rights**: the Pimax service runs elevated. The game itself runs normally.
- Each controller's state then goes through shared memory to the layer, inside the game.

What the layer does ("Garder l'arme en main hors du champ" switch, keep the weapon in hand out of view), in a **torso frame** (the neck and the smoothed horizontal head heading): looking at your weapon or tilting your head does not move it.

| Pimax state | Behavior |
|---|---|
| Tracked | normal (smoothing only) |
| Lost | Pimax's estimate is kept, but its movement since the loss started is capped (**max drift**, 30 cm by default), and the controller stays within arm's reach of the torso (**reach**, 85 cm) |
| Reinitializing (about 0.5 s after the loss) | Pimax's position estimate is dropped. **Arm model**: the hand rotates around an estimated elbow following the controller's orientation (gyroscope, always reliable); raising the wrist raises the weapon. Without the arm model: weapon frozen |
| Tracked again | gradual return to the true position, longer for larger gaps (60 ms + 400 ms per meter, at most 300 ms) |

During play, the app shows each hand's state (seen by the cameras, out of view, smooth return). The log records the **gap at recovery** each time: the distance between the position SteadyGrip displayed and the true position when Pimax finds the controller again.

**Linking the lost hand to the other hand**: the **Armes à deux mains** (two-handed weapons) switch selects the "both grips squeezed" mode (otherwise: never). "Automatic" and "always" modes remain available in `settings.ini` (`LierMains=0` or `1`). Off by default: on real data, wrists rotate too much (16–30 cm of variation seen from the other controller) for it to be reliable all the time.

- **Both grips squeezed**: the lost hand is linked only if both grip buttons are squeezed when the loss starts, the other hand is tracked, and the hands are 8–80 cm apart. Releasing a grip during the loss makes the hand independent again (no jump). The layer reads the grips when the game queries them (actions bound to `/input/squeeze`); the log says "Bouton grip lu pendant le jeu" for each hand.
- Measured in CrossFire (right hand): on short losses, the linked hand comes back with a 0 cm median gap, 3 cm at most (114 losses), versus 2 cm median and up to 39 cm for the independent hand (84 losses).

### Pimax updates

The app reads the version of the Pimax service and its controller-tracking module and compares it with the last version where reading was verified (`[Pimax]` section of `settings.ini`). New version: "vérification en cours" (checking) until two lines read in memory are later found in the log file (a few tracking losses are enough).

The app switches to **"incompatible"** if the log contains unknown state names, if the file records losses that memory never shows, or if the buffer is lost three times in a row with no line found. The layer then stops using the loss signal; smoothing and orientation stabilization keep working.

### Loss map

For each loss, the layer records where the hand was as seen from the headset (camera frame): azimuth (positive to the right), elevation (negative below the gaze) and distance. Each summary in the log gives **losses per minute spent in each zone**: rates, not totals, so a zone where the hand spends little time is not favored. Each loss is also added to `%LOCALAPPDATA%\SteadyGrip\SteadyGrip_pertes.csv`.

### Ideas tested and dropped

- **Extending the hand's momentum during short losses**: during "lost", Pimax already integrates the IMU (1–2 cm median gap at recovery); during reinitialization, Pimax freezes the pose with zero velocity. The current arm model remains best (7 cm median gap versus 16–17 cm for the alternatives).
- **Predicting orientation to offset smoothing lag**: prediction reinjects the jitter; the gain is at most 0.06° at equal jitter.
- **Checking Pimax's state against optical corrections** (0.5, removed in 0.5.1): in game, the grip pose is offset from the controller's center, so wrist rotation looked like corrections and almost every reinitialization was wrongly ignored.

### "Tracked" hand out of arm's reach

Pimax sometimes shows a controller as tracked while getting its position wrong: few LEDs visible (weapon against the face while aiming), infrared reflection. The hand then slides away from the body, and jumps back when Pimax catches up. Since a hand can't be farther from the neck than the arm's reach (advanced settings, 85 cm by default), SteadyGrip holds the displayed position at that reach, plus a 5 cm margin. The error is reduced, not removed: in a real case with a sniper rifle, Pimax put the hand 1.25 m from the neck, and the largest displayed gap drops from 84 to 54 cm. These positions aren't learned by the arm model. The log says "main suivie hors de portée du bras". Always on when "Contenir les pertes" is; if your hand gets held with the arm stretched out, increase the arm's reach.

### Learned arm model

Réglages avancés › Manette hors du champ › **"Modèle de bras appris sur tes gestes"** (Advanced settings › Controller out of view › Arm model learned from your movements), on by default since 0.6.6 (it needs "Modèle de bras pendant les réinitialisations", also on). During a Pimax reinitialization, instead of rotating the hand around an assumed elbow, SteadyGrip looks up the postures it has already seen with the controller oriented the same way (and the head tilted the same way), and puts the hand where it was then, relative to the torso.

- The model learns while the controller is tracked, one frame in three, even when the option is off. It keeps the last 5000 postures per controller pose, about 3½ minutes of tracked play: it follows your recent habits rather than piling everything up, since that gives the best results when replaying your sessions. It needs about one minute of play to be ready; until then, the elbow model is used.
- It describes your body and your way of playing, not the game: one file for all games, `%LOCALAPPDATA%\SteadyGrip\modeles\bras.bin`, read when a game starts and saved when it closes. Deleting it starts over.
- No jump: the estimate starts from the displayed position and reaches the learned posture in about a second.
- Measured offline by replaying four recorded sessions, each with a model learned on the other three; gap at recovery after a reinitialization (up to 60 s: aiming through a scope can keep the hand against the headset for half a minute):

  | Session | Elbow model: median / worst 10 % | Learned model: median / worst 10 % |
  |---|---|---|
  | CrossFire (1) | 18 cm / 70 cm | 10 cm / 19 cm |
  | Gunman Contract | 27 cm / 61 cm | 14 cm / 28 cm |
  | CrossFire (2), two-handed weapon | 12 cm / 26 cm | 9 cm / 15 cm |
  | CrossFire (3), sniper rifle | 14 cm / 49 cm | 9 cm / 26 cm |

  The learned model does at least as well everywhere; the clearest gain is on large errors.
- Limit: a hand supporting a two-handed weapon mostly depends on the other hand. For that one, the "two-handed weapon" option remains the most accurate.
- The log says, at each reinitialization, "(modèle appris)" (learned model) or "(modèle du coude : pas encore assez appris)" (elbow model: not learned enough yet).

## Grip that lets go by itself

With Pimax's OpenXR runtime, the grip button sometimes releases for a fraction of a second while you squeeze it: the hand opens and closes, and the weapon may drop (Gunman Contract, for example). For these games, turn on **"Grip qui lâche tout seul"** on the game's page.

- SteadyGrip then corrects the game's actions bound to the grip (`/input/squeeze`): a release shorter than the delay (60 ms by default, 30 to 400 ms in Réglages avancés › Bouton grip) is ignored, and the value returned to the game stays that of the squeezed grip.
- A real release takes effect after that delay.
- At each summary, the log shows how many spurious releases were masked and the longest one. If it reports "relâchements suivis d'un nouvel appui en moins de 0,4 s" (releases followed by a new press within 0.4 s), increase the delay a little.
- Off by default: in games where the grip works fine, there is nothing to correct.

## Beeps when a controller leaves the cameras' view

Paramètres › Sons › **"Bip quand une manette sort du champ des caméras"** (Settings › Sounds › Beep when a controller leaves the cameras' view), off by default. A low beep when Pimax loses a controller, a high beep when it finds it again. The sound comes from the controller's side (left or right), so it isn't mistaken for the game's vibrations or sounds. The "Écouter" (Listen) button plays the four beeps.

- Only while a game runs with SteadyGrip, and for losses longer than 0.2 s: Pimax's very short flickers don't beep. The high beep waits for 0.15 s of regained tracking, and only follows a low beep.
- Played by the SteadyGrip app, not the game, on Windows' default audio output: if the headset audio isn't the default output, the beeps come out elsewhere.
- Useful to learn where your blind spots are: at the low beep, bring the controller back in front of you.

## Recording your movements (diagnostics)

Paramètres › Diagnostic › **"Enregistrer les mouvements"** (Settings › Diagnostics › Record movements): while it's on, SteadyGrip writes down, every frame, the headset and both controllers' poses, the tracking state reported by Pimax and what SteadyGrip sent to the game. It changes nothing in game.

- One file per session, `poses_YYYYMMDD_HHMMSS.csv`, in `%LOCALAPPDATA%\SteadyGrip\enregistrements` (about 3 MB per minute). Recording starts with the game, or as soon as the switch is turned on while the game runs, and stops when the switch is turned off or the game quits.
- What it's for: replaying real movements outside the game to develop new methods, such as an arm model learned from each player's own way of playing, to better guess where a controller is when it leaves the cameras' view.
- The file only holds positions and orientations, no images or sound.

## Comparing settings during play

The layer writes every settings change to the log (`%LOCALAPPDATA%\SteadyGrip\SteadyGrip.log`), once the slider has been still for 1 s, with a summary of the previous period (if it lasted at least 5 s): stutter in motion and aiming jitter, with and without SteadyGrip ("Pimax seul"), losses, reinitializations and mean gap at recovery. To compare two settings, play at least one minute with each, doing the same moves.

## Recommended in-game settings

**CrossFire: Sierra Squad** (tested with a Pimax Dream Air SLAM):
- **Automatic weapon shouldering: off**, and **automatic scope aiming: off**. They bring the controller right under the headset, where the cameras barely see it (31–37 losses per minute closer than 25 cm, versus under 2 at 25–45 cm).
- Confirmed over two 10-minute sessions with the same weapons (sniper rifle, assault rifle): with automatic scope aiming and the weapon against the face, the right hand lost tracking 6.9 times per minute and spent 53 % of the time untracked; without automatic aiming, controllers in front of the headset, 0.6 times per minute and 5 % of the time, with no losses near the headset.
- **Hold the weapon slightly in front of your face** rather than pressed under the headset: in the same session, the right hand went from 37 losses in 181 s to 6 in 145 s.
- **Second-hand influence on aiming: 50 %** (80 % by default).

With SteadyGrip on "Très stable" and "Armes à deux mains", these settings gave the steadiest aim measured: 70–76 % less jitter.

## Troubleshooting

- Log: `%LOCALAPPDATA%\SteadyGrip\SteadyGrip.log`. It shows whether the layer loaded in the game and lists the controller spaces found.
- If the game does not show up as "EN COURS", it may not use OpenXR (native OpenVR / SteamVR game), or it uses another OpenXR runtime than Pimax's.
- "Relance SteadyGrip en administrateur": the app was not started as administrator.
- "Version de Pimax non reconnue" (Pimax version not recognized): a Pimax update changed the log. SteadyGrip keeps smoothing, without the loss signal. Attach `SteadyGrip.log` and the latest `controller_*.txt` to an issue on the repository.
