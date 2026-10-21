@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo ReleaseForge - compilazione Win32 nativa (Visual Studio + CMake)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERRORE: Visual Studio non trovato. Installa "Sviluppo di applicazioni desktop con C++".
  goto :error
)
set "VSDIR="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
if not defined VSDIR (
  echo ERRORE: componente C++ di Visual Studio mancante ^(MSVC x64/x86^).
  goto :error
)
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"
echo Visual Studio: %VSDIR%

"%CMAKE%" -S . -B build -A x64
if errorlevel 1 goto :error
"%CMAKE%" --build build --config Release --parallel
if errorlevel 1 goto :error
build\Release\core_tests.exe
if errorlevel 1 (
  echo ERRORE: test del core falliti.
  goto :error
)
if not exist dist mkdir dist
copy /y build\Release\ReleaseForge.exe dist\ReleaseForge.exe >nul
echo.
echo OK: dist\ReleaseForge.exe
if /i "%~1"=="nopause" exit /b 0
pause
exit /b 0
:error
echo Compilazione non riuscita: leggi il primo errore qui sopra.
if /i "%~1"=="nopause" exit /b 1
pause
exit /b 1
