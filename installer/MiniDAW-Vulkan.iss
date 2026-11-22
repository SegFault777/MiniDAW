; MiniDAW installer script for Inno Setup 6 (the Vulkan version, MiniDAWVulkan.exe).
; Build:  ISCC.exe /DMyAppVersion=0.12 installer\MiniDAW-Vulkan.iss
; Expects the deployed application in dist-vulkan\MiniDAW (build.sh vulkan-deploy).
; It has its own AppId and folder, so it installs next to the Native version instead of replacing it.

#define MyAppName "MiniDAW Vulkan"
#ifndef MyAppVersion
  #define MyAppVersion "0.12"
#endif
#define MyAppExe "MiniDAWVulkan.exe"

[Setup]
AppId={{B3F6A1D2-5C47-4E8B-9A21-6D0C7E4F18A9}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=MiniDAW contributors
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExe}
OutputDir=..\installer-out
OutputBaseFilename=MiniDAW-Vulkan-{#MyAppVersion}-setup
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
Name: "fileassoc"; Description: "Associate .mdaw project files with {#MyAppName} (the Native version may already use them)"; Flags: unchecked
Name: "openwith"; Description: "Add {#MyAppName} to the ""Open with"" list of audio and MIDI files (mp3, wav, aiff, flac, m4a, ogg, mid)"
Name: "plugindirs"; Description: "Create the folder for your own MiniDAW plug-ins (%LOCALAPPDATA%\MiniDAW\plugins)"

[Files]
Source: "..\dist-vulkan\MiniDAW\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Dirs]
Name: "{localappdata}\MiniDAW\plugins"; Tasks: plugindirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{group}\User Guide"; Filename: "{app}\docs\user"
Name: "{group}\Plug-in SDK"; Filename: "{app}\plugin-sdk"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.mdaw"; ValueType: string; ValueName: ""; ValueData: "MiniDAWVulkan.Project"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\MiniDAWVulkan.Project"; ValueType: string; ValueName: ""; ValueData: "{#MyAppName} Project"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\MiniDAWVulkan.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\MiniDAWVulkan.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExe}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#MyAppName}"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExe}"" ""%1"""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".mp3"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.mp3\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".wav"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.wav\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".aif"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.aif\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".aiff"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.aiff\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".flac"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.flac\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".m4a"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.m4a\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".ogg"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.ogg\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".mid"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.mid\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExe}\SupportedTypes"; ValueType: string; ValueName: ".midi"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.midi\OpenWithList\{#MyAppExe}"; ValueType: none; Flags: uninsdeletekey; Tasks: openwith

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

; Settings, autosave files and your own plug-ins live in %LOCALAPPDATA%\MiniDAW; they stay on uninstall.
