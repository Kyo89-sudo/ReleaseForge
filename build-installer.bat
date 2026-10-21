@echo off
setlocal EnableExtensions
cd /d "%~dp0"
call build.bat nopause
if errorlevel 1 goto :error
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo ERRORE: Inno Setup 6 non trovato ^(https://jrsoftware.org/isinfo.php^).
  goto :error
)
"%ISCC%" installer\ReleaseForge.iss
if errorlevel 1 goto :error
echo OK: installer in dist
pause
exit /b 0
:error
pause
exit /b 1
