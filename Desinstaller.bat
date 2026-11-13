@echo off
rem SteadyGrip - desinstallation : retire la couche OpenXR et les raccourcis.
rem Vos reglages (%LOCALAPPDATA%\SteadyGrip) sont conserves ; supprimez ce dossier a la main si besoin.
setlocal
cd /d "%~dp0"
if /i "%~1"=="--admin" goto :admin
echo Windows va demander une autorisation d'administrateur : repondez Oui.
echo Windows will ask for administrator permission: answer Yes.
set "SG_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $p = Start-Process -FilePath $env:SG_SELF -ArgumentList '--admin' -Verb RunAs -Wait -PassThru; exit $p.ExitCode } catch { exit 99 }"
if "%errorlevel%"=="99" (
  echo L'autorisation a ete refusee : rien n'a ete modifie.  /  Permission refused: nothing was changed.
  pause
)
exit /b 0

:admin
if not exist "%~dp0SteadyGripApp.exe" (
  echo SteadyGripApp.exe est introuvable a cote de Desinstaller.bat.
  pause
  exit /b 1
)
taskkill /im SteadyGripApp.exe /f >nul 2>&1
start "" /wait "%~dp0SteadyGripApp.exe" --uninstall
if errorlevel 1 (
  echo La couche OpenXR n'a pas pu etre retiree.  /  The OpenXR layer could not be removed.
  pause
  exit /b 1
)
echo SteadyGrip est desinstalle : la couche OpenXR et les raccourcis sont retires.
echo Vous pouvez maintenant supprimer le dossier %~dp0
echo SteadyGrip is uninstalled (OpenXR layer and shortcuts removed). You can now delete this folder.
pause
exit /b 0
