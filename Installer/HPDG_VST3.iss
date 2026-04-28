#define MyAppName "HPDG VST3"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "BoomBap Labs"
#define MyAppURL "https://github.com/Hamec01/HPDG"
#define MyPluginBundleName "HPDG.vst3"

#ifndef SourceVst3Dir
  #define SourceVst3Dir "..\build\HPDG_artefacts\Release\VST3\HPDG.vst3"
#endif

#ifndef OutputDir
  #define OutputDir "..\Releases"
#endif

[Setup]
AppId={{8D83F55D-9437-4AF5-B77D-8D3D2C1C1A5D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={commoncf64}\VST3\{#MyPluginBundleName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=HPDG_VST3_Setup_{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={commoncf64}\VST3\{#MyPluginBundleName}\Contents\x86_64-win\HPDG.vst3

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Standard installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "HPDG VST3 plugin"; Types: full; Flags: fixed

[InstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#MyPluginBundleName}"

[Files]
Source: "{#SourceVst3Dir}\*"; DestDir: "{commoncf64}\VST3\{#MyPluginBundleName}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{commoncf64}\VST3"; Description: "Open VST3 folder"; Flags: postinstall shellexec skipifsilent unchecked
