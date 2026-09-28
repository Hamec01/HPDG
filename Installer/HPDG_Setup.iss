; Inno Setup script for HPDG (HamloProd Drum Generator)
; Build with: "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" HPDG_Setup.iss

#define MyAppName "HPDG"
#define MyAppFullName "HamloProd Drum Generator"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "BoomBap Labs"
#define MyAppExeName "HPDG.exe"
#define RepoRoot "..\"
#define ReleaseArtefacts RepoRoot + "build\HPDG_artefacts\Release"

[Setup]
AppId={{6C6B6A0B-9B7B-4C7C-9E7B-3C8B9A0B6B7A}
AppName={#MyAppFullName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppFullName} {#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppFullName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#RepoRoot}Assets\Images\AppIcon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=Output
OutputBaseFilename=HPDG_Setup
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "vst3"; Description: "Install VST3 plug-in (Common Files\VST3, all DAWs)"; GroupDescription: "Components:"; Flags: checkedonce
Name: "standalone"; Description: "Install Standalone application"; GroupDescription: "Components:"; Flags: checkedonce
Name: "samples"; Description: "Install factory drum sample library (required for sound out of the box)"; GroupDescription: "Components:"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a desktop shortcut for the Standalone app"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; VST3 plug-in bundle -> shared Common Files\VST3 folder used by all VST3 hosts
Source: "{#ReleaseArtefacts}\VST3\HPDG.vst3\*"; DestDir: "{commoncf64}\VST3\HPDG.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3

; Standalone application
Source: "{#ReleaseArtefacts}\Standalone\HPDG.exe"; DestDir: "{app}"; Flags: ignoreversion; Tasks: standalone

; Documentation
Source: "{#RepoRoot}FAQ.md"; DestDir: "{app}"; Flags: ignoreversion; Tasks: standalone

; Factory drum sample library -- HPDG looks for this under the current user's
; Documents\DRUMENGINE\Samples regardless of which DAW hosts the VST3, so every
; lane has a sound assigned immediately after install instead of showing "(none)".
Source: "{#RepoRoot}Samples\*"; DestDir: "{userdocs}\DRUMENGINE\Samples"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: samples

[Icons]
Name: "{group}\{#MyAppFullName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: standalone
Name: "{group}\FAQ"; Filename: "{app}\FAQ.md"; Tasks: standalone
Name: "{group}\Uninstall {#MyAppFullName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppFullName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: standalone and desktopicon

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\HPDG.vst3"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppFullName}"; Flags: nowait postinstall skipifsilent; Tasks: standalone
