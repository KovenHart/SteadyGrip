# SteadyGrip

Controller stabilization for headsets whose controllers are tracked by the headset's own cameras (Pimax Dream Air SLAM, etc.). SteadyGrip has two parts:

- **an implicit OpenXR API layer** (`SteadyGrip.dll`): once installed, it loads automatically in every OpenXR game;
- **a companion app** (`SteadyGripApp.exe`): it installs the layer, keeps a list of your games (with cover art), adjusts each game's stabilization live and shows what happens during play. Its interface follows Pimax Play's colors, either **Pimax Classic** or **Pimax EVO** (Settings › Appearance). The interface is in French.

Only controller poses are changed. The headset pose is never touched.

*Français : [README.md](README.md) · What's new: [CHANGELOG.en.md](CHANGELOG.en.md)*

> SteadyGrip is an independent project, not affiliated with Pimax. It does not modify any Pimax file or program: it only reads, in memory, the tracking state the Pimax service writes to its own log.

## Download

The latest build is in [Releases](../../releases): unzip `SteadyGrip-<version>-win64.zip` into a permanent folder, then follow [Installation](#installation).

## What it does

**Jitter.** Between two camera frames, the controller position is extrapolated by the IMU, then snapped to each new optical measurement (about 25 times per second). Those snaps cause the micro-jitter you see while aiming, and the stutter when moving the weapon. The layer follows the trajectory predicted from the runtime's velocities and spreads each correction over time: no lag is added to real motion. An adaptive ("One Euro") filter also calms the weapon's orientation when it is almost still. In CrossFire: Sierra Squad this measured 70–77 % less aiming jitter than Pimax alone.

**Tracking losses.** The Pimax service knows when it loses a controller, but never tells games: it only writes it to its log (`C:\ProgramData\Pimax\slam\controller_*.txt`), which reaches the disk every 30–40 s. The app reads those lines **from the log's write buffer in the Pimax service's memory** as soon as they are written (read-only; this is why the app asks for administrator rights). The layer then:

| Pimax state | Behavior |
|---|---|
| Tracked | normal (smoothing only) |
| Lost | keeps Pimax's estimate, but caps drift (30 cm by default) and keeps the controller within arm's reach |
| Reinitializing (about 0.5 s after the loss) | Pimax's position is dropped; an arm model moves the hand around an estimated elbow following the controller's orientation |
| Tracked again | smooth return to the true position instead of a teleport |

Everything is computed in a **torso frame** (neck and smoothed head heading): looking at your weapon does not move it. With **Two-handed weapons** on, a lost hand follows the other hand while both grips are squeezed.

**Loss map.** For each loss, the log notes where the hand was as seen from the headset and computes losses per minute spent in each zone; each loss is also written to `SteadyGrip_pertes.csv`. Key finding: below 25 cm from the headset (weapon pressed under it to aim), Pimax loses the controller 31–37 times per minute, versus under 2 at 25–45 cm.

## My games and their settings

- **A game you launch is added automatically.** The layer recognizes it by its executable, and for Steam games by its Steam app ID (from Steam's `appmanifest_*.acf` files).
- **Add a game by hand**: "Ajouter un jeu" card, from installed Steam games or by picking an `.exe`.
- **Cover art**: copied from Steam's local cache, otherwise downloaded once from Steam's image server; stored in `%LOCALAPPDATA%\SteadyGrip\jaquettes`.
- A game uses the **default settings** until you change something on its page; then it gets its own. Settings for the running game apply immediately.

All settings live in `%LOCALAPPDATA%\SteadyGrip\settings.ini`.

## Building

Requirements: Visual Studio 2022 ("Desktop development with C++") and Git. On first configure, CMake downloads the OpenXR headers (GitHub) and Microsoft's WebView2 SDK (NuGet package `Microsoft.Web.WebView2`): an Internet connection is needed.

```bat
build.bat
```

or by hand:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

`build.bat` builds, runs the tests and gathers everything in `dist\`: `SteadyGrip.dll`, `XR_APILAYER_NOVENDOR_steadygrip.json`, `SteadyGripApp.exe` and the `ui` folder. They must stay together. GitHub Actions builds every push and drafts a release for each `v*` tag matching `VERSION`.

## Installation

1. Copy the files and the `ui` folder to a permanent folder, e.g. `C:\Program Files\SteadyGrip`.
2. Run `SteadyGripApp.exe` (Windows asks for administrator rights) and click **Installer** in the top banner. The layer registers under `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`.
3. Before each session, run `SteadyGripApp.exe`: the banner should read "Tout est prêt : lance ton jeu" (everything is ready).
4. Launch an OpenXR game: it shows up as "EN COURS" (running) in My games.

The interface uses WebView2, already installed with Windows 10 and 11. If it is missing, install "Microsoft Edge WebView2 Runtime" from Microsoft's website.

## Recommended in-game settings (CrossFire: Sierra Squad)

- Automatic weapon shouldering **off**, and automatic scope aiming **off**: both bring the controller right under the headset, where the cameras barely see it.
- Hold the weapon slightly in front of your face rather than pressed under the headset.
- Second-hand influence on aiming: **50 %** (80 % by default).

## License

MIT, see [LICENSE](LICENSE). Third-party components: [THIRD-PARTY.md](THIRD-PARTY.md).
