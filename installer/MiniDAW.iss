; MiniDAW installer script for Inno Setup 6 (the native, Qt-free version).
; Build:  ISCC.exe /DMyAppVersion=0.11 installer\MiniDAW.iss
; Expects the deployed application in dist-native\MiniDAW (build.sh native-deploy).
; The AppId is the one of the old Qt installer on purpose: installing this version over a Qt installation upgrades it.

#define MyAppName "MiniDAW"
#ifndef MyAppVersion
  #define MyAppVersion "0.11"
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
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "fileassoc"; Description: "Associate .mdaw project files with {#MyAppName}"
Name: "plugindirs"; Description: "Create the folder for your own MiniDAW plug-ins (%LOCALAPPDATA%\MiniDAW\plugins)"

[InstallDelete]
; leftovers of the old Qt version when this installer upgrades it
Type: files; Name: "{app}\Qt6*.dll"
Type: files; Name: "{app}\MiniDAWNative.exe"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\tls"

[Files]
Source: "..\dist-native\MiniDAW\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Dirs]
Name: "{localappdata}\MiniDAW\plugins"; Tasks: plugindirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{group}\User Guide"; Filename: "{app}\docs\user"
Name: "{group}\Plug-in SDK"; Filename: "{app}\plugin-sdk"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.mdaw"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName}.Project"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#MyAppName}.Project"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName} Project"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#MyAppName}.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#MyAppName}.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExe}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

; Settings, autosave files and your own plug-ins live in %LOCALAPPDATA%\MiniDAW; they stay on uninstall.
