@echo off
rem SteadyGrip - installation ou mise a jour en un double-clic.
rem Deux cas :
rem   - paquet de distribution (sous-dossier "programme" deja compile) : installation directe ;
rem   - sources : compile d'abord avec build.bat (resultat dans dist\).
rem Puis, en administrateur : copie le programme dans C:\SteadyGrip (vos reglages, dans
rem %LOCALAPPDATA%\SteadyGrip, ne sont pas touches), declare la couche aupres d'OpenXR,
rem cree les raccourcis du bureau et du menu Demarrer, et lance SteadyGrip.
setlocal
cd /d "%~dp0"
if /i "%~1"=="--admin" goto :admin

echo ============================================================
echo  SteadyGrip - installation
echo ============================================================
echo.
if exist "%~dp0programme\SteadyGripApp.exe" (
  set "SG_SRC=programme"
  goto :elevate
)
if not exist "%~dp0build.bat" goto :incomplete
if not exist "%~dp0CMakeLists.txt" goto :incomplete

echo Etape 1 sur 2 : compilation  /  Step 1 of 2: build
call "%~dp0build.bat" --auto
if errorlevel 1 goto :buildfailed
set "SG_SRC=dist"
echo.
echo Etape 2 sur 2 : installation  /  Step 2 of 2: installation

:elevate
echo Windows va demander une autorisation d'administrateur : repondez Oui.
echo Windows will ask for administrator permission: answer Yes.
echo.
set "SG_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $p = Start-Process -FilePath $env:SG_SELF -ArgumentList '--admin', $env:SG_SRC -Verb RunAs -Wait -PassThru; exit $p.ExitCode } catch { exit 99 }"
set "RC=%errorlevel%"
if "%RC%"=="0" exit /b 0
if "%RC%"=="99" (
  echo L'autorisation d'administrateur a ete refusee : rien n'a ete installe.
  echo Administrator permission was refused: nothing was installed.
  pause
  exit /b 1
)
rem Les autres codes ont deja ete expliques dans la fenetre d'administrateur.
exit /b 1

rem ---------------------------------------------------------------------------------------------
:admin
set "SG_SRC=%~2"
set "SG_DIR=C:\SteadyGrip"
echo ============================================================
echo  SteadyGrip - installation dans %SG_DIR%
echo ============================================================
echo.
if not exist "%~dp0%SG_SRC%\SteadyGripApp.exe" goto :incomplete

tasklist /fi "imagename eq SteadyGripApp.exe" 2>nul | find /i "SteadyGripApp.exe" >nul
if not errorlevel 1 (
  echo Fermeture de SteadyGrip, deja ouvert...  /  Closing SteadyGrip...
  taskkill /im SteadyGripApp.exe /f >nul 2>&1
  timeout /t 1 /nobreak >nul
)

robocopy "%~dp0%SG_SRC%" "%SG_DIR%" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto :copyfailed
if exist "%~dp0LISEZ-MOI.txt" copy /y "%~dp0LISEZ-MOI.txt" "%SG_DIR%\" >nul
if exist "%~dp0README-EN.txt" copy /y "%~dp0README-EN.txt" "%SG_DIR%\" >nul

rem L'application est un programme fenetre : start /wait attend qu'elle ait fini.
start "" /wait "%SG_DIR%\SteadyGripApp.exe" --install
if errorlevel 1 goto :registerfailed

echo Termine : SteadyGrip est installe dans %SG_DIR%.
echo Raccourcis : bureau et menu Demarrer. SteadyGrip va s'ouvrir.
echo Done: SteadyGrip is installed in %SG_DIR%, with desktop and Start menu shortcuts.
echo.
start "" "%SG_DIR%\SteadyGripApp.exe"
pause
exit /b 0

:copyfailed
echo.
echo La copie dans %SG_DIR% n'a pas abouti. Un jeu VR est peut-etre ouvert et utilise encore
echo SteadyGrip : fermez-le, puis relancez Installer.bat.
echo Copying to %SG_DIR% failed. A VR game may still be using SteadyGrip: close it and run Installer.bat again.
pause
exit /b 2

:registerfailed
echo.
echo Les fichiers sont copies, mais SteadyGrip n'a pas pu se declarer aupres d'OpenXR.
echo Ouvrez SteadyGrip (%SG_DIR%\SteadyGripApp.exe) et cliquez sur Installer.
echo Files were copied, but SteadyGrip could not register with OpenXR. Open SteadyGrip and click Installer.
pause
exit /b 2

:buildfailed
echo.
echo La compilation n'a pas abouti : rien n'a ete installe ni modifie.
echo Le detail est ci-dessus et dans le fichier build.log, a cote de Installer.bat.
echo The build failed: nothing was installed or changed. Details are above and in build.log.
pause
exit /b 1

:incomplete
echo Il manque des fichiers a cote de Installer.bat.
echo Si vous l'avez lance depuis l'archive .zip, extrayez d'abord toute l'archive
echo (clic droit sur le .zip, "Extraire tout"), puis relancez Installer.bat depuis le dossier extrait.
echo.
echo Files are missing next to Installer.bat. If you ran it from the .zip archive, first extract
echo the whole archive (right-click the .zip, "Extract All"), then run Installer.bat from the extracted folder.
pause
exit /b 1
