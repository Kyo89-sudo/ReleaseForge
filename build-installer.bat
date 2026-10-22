@echo off
setlocal EnableExtensions
cd /d "%~dp0"
rem Installer nativo (installer\setup.cpp): non serve Inno Setup.
call build.bat nopause
if errorlevel 1 goto :error

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"

"%CMAKE%" -S installer -B build-installer -A x64 -DPAYLOAD_DIR="%CD%\dist"
if errorlevel 1 goto :error
rem --clean-first: il setup incorpora dist\ReleaseForge.exe, va sempre ricompilato
"%CMAKE%" --build build-installer --config Release --clean-first
if errorlevel 1 goto :error
copy /y build-installer\Release\ReleaseForge-*-win64-setup.exe dist\ >nul
if errorlevel 1 goto :error
echo OK: installer in dist
pause
exit /b 0
:error
echo Installer non creato: leggi il primo errore qui sopra.
pause
exit /b 1
