#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#ifndef SourceDir
  #error SourceDir preprocessor define is required
#endif

#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{A4A1F3D8-0A6F-45CB-97DE-053D8E7E3E3D}
AppName=Acheron
AppVersion={#AppVersion}
AppPublisher=trebor048
AppPublisherURL=https://github.com/trebor048/discord-client
AppSupportURL=https://github.com/trebor048/discord-client/issues
AppUpdatesURL=https://github.com/trebor048/discord-client/releases
DefaultDirName={autopf}\Acheron
DefaultGroupName=Acheron
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=Acheron-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\acheron.exe
SetupIconFile=..\..\acheron.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Acheron"; Filename: "{app}\acheron.exe"
Name: "{autodesktop}\Acheron"; Filename: "{app}\acheron.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\acheron.exe"; Description: "{cm:LaunchProgram,Acheron}"; Flags: nowait postinstall skipifsilent