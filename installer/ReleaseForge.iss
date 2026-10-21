; ReleaseForge - installer Inno Setup 6.2+ (per utente, senza diritti di amministratore).
; L'app è un unico eseguibile Win32 nativo: nessun runtime .NET / Windows App SDK / VC++.
; FFmpeg si scarica dall'app (verifica SHA-256); MediaInfo completo si installa con winget.
#ifndef AppVersion
  #define AppVersion "0.1.3"
#endif
#ifndef ExeDir
  #define ExeDir "..\dist"
#endif

[Setup]
AppId={{D435F294-B487-42C4-AD02-C1B837976034}
AppName=ReleaseForge
AppVersion={#AppVersion}
VersionInfoVersion=0.1.3.0
AppPublisher=Kyos
AppPublisherURL=https://github.com/Kyo89-sudo
DefaultDirName={localappdata}\Programs\ReleaseForge
DefaultGroupName=ReleaseForge
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\dist
OutputBaseFilename=ReleaseForge-0.1.3-win64-setup
SetupIconFile=..\src\ReleaseForge.ico
UninstallDisplayIcon={app}\ReleaseForge.exe
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
SetupLogging=yes
CloseApplications=yes

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "Crea un'icona sul desktop"; Flags: unchecked
Name: "mediainfo"; Description: "Installa MediaInfo completo (versione con interfaccia) tramite winget"; Check: NeedsMediaInfo

[Files]
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ExeDir}\ReleaseForge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\ReleaseForge"; Filename: "{app}\ReleaseForge.exe"
Name: "{autodesktop}\ReleaseForge"; Filename: "{app}\ReleaseForge.exe"; Tasks: desktopicon

[Run]
Filename: "{code:WingetPath}"; Parameters: "install --id MediaArea.MediaInfo.GUI -e --silent --accept-package-agreements --accept-source-agreements"; \
  StatusMsg: "Installazione di MediaInfo completo (winget)..."; Tasks: mediainfo; Flags: waituntilterminated runhidden skipifdoesntexist
Filename: "{app}\ReleaseForge.exe"; Description: "Avvia ReleaseForge"; Flags: nowait postinstall skipifsilent

[Code]
function MediaInfoInstalled: Boolean;
var Exe: String;
begin
  Result := FileExists(ExpandConstant('{commonpf64}\MediaInfo\MediaInfo.dll'));
  if not Result and RegQueryStringValue(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\App Paths\MediaInfo.exe', '', Exe) then
    Result := FileExists(ExtractFilePath(RemoveQuotes(Exe)) + 'MediaInfo.dll');
end;

function WingetPath(Param: String): String;
begin
  Result := ExpandConstant('{localappdata}\Microsoft\WindowsApps\winget.exe');
end;

function NeedsMediaInfo: Boolean;
begin
  Result := not MediaInfoInstalled;
end;
