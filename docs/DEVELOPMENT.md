# SteadyGrip: development

*Français : [DEVELOPPEMENT.md](DEVELOPPEMENT.md)* · [Back to home](../README.en.md)

Building from source, packaging and publishing, code layout. To simply use SteadyGrip, see the [home page](../README.en.md).

## The programs

| File | Role |
|---|---|
| `SteadyGrip.dll` (`src/layer/`) | The implicit OpenXR API layer, loaded in the game: correction smoothing, orientation stabilization, loss containment, loss map, log. |
| `SteadyGripApp.exe` (`src/app/`) | The companion app (administrator): installs the layer and shortcuts, settings, game library and cover art, reads the Pimax tracking state in memory. |
| `ui/` | The app's interface (HTML, CSS, JavaScript), displayed with WebView2. |
| `src/common/` | Shared by both: filter (`PoseFilter.h`), shared memory and settings (`Shared.h`), Steam files (`Steam.h`), Pimax log buffer reading (`LogBuffer.h`). |

## Requirements

- Windows 10 or 11, 64-bit.
- Visual Studio 2022 or newer with **"Desktop development with C++"** (CMake included), and Git.
- An Internet connection on first configure: CMake downloads the OpenXR headers (GitHub) and the WebView2 SDK (NuGet package `Microsoft.Web.WebView2`, headers and static loader).

## Building

`build.bat` builds, runs the tests and gathers the result in `dist\`:

```
dist\
  SteadyGrip.dll                         the layer
  XR_APILAYER_NOVENDOR_steadygrip.json   its OpenXR manifest
  SteadyGripApp.exe                      the app
  ui\                                    its interface
```

By hand:

```bat
cmake -B build -A x64
cmake --build build --config Release
```

These files must stay together. Running `SteadyGripApp.exe` and clicking **Installer** registers the layer under `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` (pointing to this folder's `.json`) and creates the shortcuts. Command line: `SteadyGripApp.exe --install` or `--uninstall` (layer and shortcuts).

The interface can be opened in a browser (`ui/index.html`): without the app, it shows sample data.

## Tests

| Program | What it checks |
|---|---|
| `test_lossguard.exe` | holding the controller during a loss, arm model, proportional return, grip linking, loss-map angles, orientation stabilization, grip debounce |
| `test_app.exe` | JSON messages between interface and app, Steam file parsing |
| `bench.exe`, `orient_eval.exe`, `motion_eval.exe` (in `tests/`) | replay a TrackProbe session (CSV) to measure smoothing |
| `unity_eval.exe s` / `u` | simulates one request per frame, or two requests per display time (Unity), and measures jerks |

## Version and distribution

- The version number is in `VERSION` (and `kVersion` in `src/common/Shared.h`).
- GitHub Actions (`.github/workflows/build.yml`) builds every push on Windows, runs the tests and attaches the `SteadyGrip-<version>-win64.zip` package to the run (Actions tab).
- To publish: `git tag v<version>` then `git push origin v<version>`. The tag must match `VERSION`. A draft release with the package is created under Releases: review it, then "Publish release". Without the command line: Actions tab › "Compilation et paquet" › **Run workflow**, tick "Créer le brouillon de version": the tag is created on publishing.
- The package contains `Installer.bat`, `Desinstaller.bat`, `LISEZ-MOI.txt`, `README-EN.txt` and the program in `programme\`. `Installer.bat` also works from the sources: it builds first with `build.bat`.

## Code layout

- `src/common/PoseFilter.h`: `PoseFilter` (corrections spread with no added lag), `OrientSmoother` (One Euro on orientation), `BodyFrame` (torso frame), `LossGuard` (loss, reinitialization, return, hand linking), `HeadAngles` and `LossZones` (loss map). No dependencies: also used by the tests on Linux.
- `src/layer/layer.cpp`: intercepted OpenXR functions, hand and grip identification, log, per-period summaries, `SteadyGrip_pertes.csv`.
- `src/app/PimaxReader.h`: finds the log buffer in `pi_server.exe` (read-only), verification against the file, Pimax version, "incompatible" state.
- `src/app/app.cpp`: WebView2 window, messages with the interface, per-game settings, automatic addition of the running game, shortcuts.
- `src/app/Library.h`, `src/common/Steam.h`: game library, installed Steam games, cover art.
- Shared memory `Local\SteadyGripShared_v8` (`Shared.h`): settings (app → layer), per-hand statistics and game identity (layer → app), Pimax tracking state (app → layer).
