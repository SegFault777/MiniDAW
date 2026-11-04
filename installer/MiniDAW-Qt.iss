; Installer of the old Qt version (legacy; the native version is installed by MiniDAW.iss).
; Build:  ISCC.exe /DMyAppVersion=0.11 installer\MiniDAW-Qt.iss
; Expects the deployed application (exe + Qt runtime) in dist\MiniDAW.

#define MyAppName "MiniDAW"
#ifndef MyAppVersion
  #define MyAppVersion "0.10_1"
#endif
#define MyAppExe "MiniDAW.exe"

[Setup]
AppId={{3D4F1A70-5B2E-4C8A-9E61-0A7B8C2D5E19}
AppName={#MyAppName} (Qt)
AppVersion={#MyAppVersion}
AppPublisher=MiniDAW contributors
DefaultDirName={autopf}\{#MyAppName}-Qt
DefaultGroupName={#MyAppName} (Qt)
UninstallDisplayIcon={app}\{#MyAppExe}
OutputDir=..\installer-out
OutputBaseFilename={#MyAppName}-Qt-{#MyAppVersion}-setup
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=no

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked

[Files]
Source: "..\dist\MiniDAW\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
