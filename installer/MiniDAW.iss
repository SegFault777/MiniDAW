; MiniDAW installer script for Inno Setup 6.
; Build:  ISCC.exe /DMyAppVersion=0.5_1 installer\MiniDAW.iss
; Expects the deployed application (exe + Qt runtime) in dist\MiniDAW.

#define MyAppName "MiniDAW"
#ifndef MyAppVersion
  #define MyAppVersion "0.5_1"
#endif
#define MyAppExe "MiniDAW.exe"

[Setup]
AppId={{7B1E2C54-93A6-4F0B-8C1D-5E2A6D90F3B7}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=MiniDAW contributors
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExe}
OutputDir=..\installer-out
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-setup
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "fileassoc"; Description: "Associate .mdaw project files with {#MyAppName}"

[Files]
Source: "..\dist\MiniDAW\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.mdaw"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName}.Project"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#MyAppName}.Project"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName} Project"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#MyAppName}.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#MyAppName}.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExe}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
