@echo off
rem Compile SteadyGrip (couche OpenXR, application, tests), lance les tests, et rassemble le resultat dans dist\.
rem Tout ce qui s'affiche est aussi enregistre dans build.log, et la fenetre reste ouverte a la fin.
rem   build.bat --auto : sans pause finale.
setlocal
cd /d "%~dp0"
if "%~1"=="--inner" goto :main
set "AUTO="
if /i "%~1"=="--auto" set "AUTO=1"

echo Compilation en cours, cela peut prendre une a deux minutes...
call "%~f0" --inner > "%~dp0build.log" 2>&1
set "RC=%errorlevel%"
type "%~dp0build.log"
echo.
echo ------------------------------------------------------------
if "%RC%"=="0" (
  echo Termine : le resultat est dans le dossier dist\.
  echo Pour l'installer dans C:\SteadyGrip avec les raccourcis : Installer.bat.
) else (
  echo ECHEC, code %RC%. Le detail est ci-dessus et dans build.log.
)
if not defined AUTO pause
exit /b %RC%

:main
where cmake >nul 2>&1
if not errorlevel 1 goto :hascmake
rem CMake fourni avec Visual Studio (le plus recent installe)
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VSDIR=%%i"
if defined VSDIR set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
:hascmake
cmake -S . -B build -A x64 || exit /b 1
cmake --build build --config Release || exit /b 2
build\bin\Release\test_lossguard.exe || exit /b 3
build\bin\Release\test_app.exe || exit /b 4
if exist dist rmdir /s /q dist
mkdir dist
copy /y build\bin\Release\SteadyGrip.dll dist\ >nul || exit /b 5
copy /y build\bin\Release\XR_APILAYER_NOVENDOR_steadygrip.json dist\ >nul || exit /b 5
copy /y build\bin\Release\SteadyGripApp.exe dist\ >nul || exit /b 5
xcopy /e /i /y /q build\bin\Release\ui dist\ui >nul || exit /b 5
copy /y Desinstaller.bat dist\ >nul || exit /b 5
copy /y README.md dist\ >nul
copy /y LICENSE dist\ >nul
exit /b 0
