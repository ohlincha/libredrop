; Inno Setup script for LibreDrop (CI: iscc /DAppVersion=x.y.z packaging\windows\libredrop.iss)
; Installs the deployed folder dist\LibreDrop (program, Qt, ffmpeg.exe/ffprobe.exe) per user by default.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{9B3E2C71-4D8A-4F65-A1C2-7E5D0B8F3A42}
AppName=LibreDrop
AppVersion={#AppVersion}
AppPublisher=LibreDrop contributors
AppPublisherURL=https://github.com/ohlincha/libredrop
DefaultDirName={autopf}\LibreDrop
DefaultGroupName=LibreDrop
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\..\dist
OutputBaseFilename=LibreDrop-{#AppVersion}-Windows-x64-Setup
SetupIconFile=..\..\resources\icon.ico
UninstallDisplayIcon={app}\LibreDrop.exe
LicenseFile=..\..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\..\dist\LibreDrop\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\LibreDrop"; Filename: "{app}\LibreDrop.exe"
Name: "{group}\LibreDrop (example recording)"; Filename: "{app}\LibreDrop.exe"; Parameters: "--example"
Name: "{autodesktop}\LibreDrop"; Filename: "{app}\LibreDrop.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LibreDrop.exe"; Parameters: "--example"; Description: "Start LibreDrop with the example recording"; Flags: nowait postinstall skipifsilent
