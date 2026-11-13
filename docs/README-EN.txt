STEADYGRIP
==========

Steadies the controllers of camera-tracked Pimax headsets (Dream Air SLAM, Crystal Super...) in
OpenXR games: less jitter when you aim, and the weapon stays in hand when a controller leaves the
cameras' view.

SteadyGrip is an independent project, not affiliated with Pimax. It modifies no Pimax file or
program: it only reads, in memory, the tracking state the Pimax service writes to its log.

The app's interface is in French.


REQUIREMENTS
------------
- Windows 10 or 11, 64-bit.
- A camera-tracked Pimax headset with Pimax Play or Pimax EVO, and an OpenXR game.
- Nothing else: the program is already built. Its window uses Microsoft's WebView2 engine, included
  in Windows 11 and up-to-date Windows 10 (otherwise:
  https://developer.microsoft.com/microsoft-edge/webview2/).


INSTALL OR UPDATE
-----------------
1. Extract the whole archive (right-click the .zip, "Extract All").
2. In the extracted folder, double-click Installer.bat.
   - Windows may show "Windows protected your PC": the program is not signed. Click
     "More info", then "Run anyway".
   - Answer Yes to the administrator prompt: it is needed to register SteadyGrip with OpenXR and
     to read the controllers' tracking state in the Pimax service.

The program is installed in C:\SteadyGrip, with a desktop and Start menu shortcut. Updating keeps
your settings and games. Close your VR game before updating. The extracted folder can be deleted
once installation is finished.


USE
---
1. Start SteadyGrip (desktop shortcut) before playing. The top banner should say everything is
   ready.
2. Start your game: it appears in "Mes jeux" (My games), marked EN COURS (running).
3. Click the game to choose its aiming stability ("Stabilité de la visée": Désactivée, Réactive,
   Équilibrée, Très stable = off, responsive, balanced, very stable) and turn on "Armes à deux
   mains" (two-handed weapons). Changes apply immediately.

Tip: hold the weapon slightly in front of your face rather than pressed under the headset. Right
under the headset, the cameras barely see the controller.


UNINSTALL
---------
Double-click Desinstaller.bat in C:\SteadyGrip (it removes the OpenXR layer and the shortcuts),
then delete that folder. Your settings are in %LOCALAPPDATA%\SteadyGrip.


TROUBLESHOOTING
---------------
- Log: %LOCALAPPDATA%\SteadyGrip\SteadyGrip.log. It shows whether SteadyGrip loaded in the game.
- The game is not shown as running: it may not use OpenXR (native SteamVR game), or it uses
  another OpenXR runtime than Pimax's.
- "Version de Pimax non reconnue" (Pimax version not recognized): a Pimax update changed its log.
  SteadyGrip keeps smoothing, without the loss signal.
- A game behaves oddly: choose "Désactivée" for that game, or run Desinstaller.bat.
- Games protected by anti-cheat may refuse OpenXR API layers.

To report a problem: https://github.com/KovenHart/SteadyGrip/issues, with the version number, the
game, the log and the latest C:\ProgramData\Pimax\slam\controller_*.txt file.

Full guide: docs\GUIDE.en.md. Changes: CHANGELOG.en.md. License: LICENSE (MIT).
Third-party components: THIRD-PARTY.md.
