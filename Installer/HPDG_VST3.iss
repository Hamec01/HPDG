; Windows x64 VST3-only beta. The complete library is already inside the bundle.
#define MyAppVersion "0.1.0 Beta 1"
#ifndef SourceVst3Dir
#define SourceVst3Dir "..\build\HPDG_artefacts\Release\VST3\HPDG.vst3"
#endif
#ifndef OutputDir
#define OutputDir "..\Releases"
#endif
#ifndef VCRedistFile
#define VCRedistFile "..\build\prerequisites\vc_redist.x64.exe"
#endif
#define RuntimeRequiredVersion GetFileVersion(VCRedistFile)
[Setup]
#ifdef InstallerTest
AppId=HPDG-Beta-Isolated-Installer-Test
PrivilegesRequired=lowest
CreateUninstallRegKey=no
OutputBaseFilename=HPDG_Beta_InstallTest
#else
AppId={{8D83F55D-9437-4AF5-B77D-8D3D2C1C1A5D}
PrivilegesRequired=admin
OutputBaseFilename=HPDG_VST3_Setup_0.1.0-beta.1
#endif
AppName=HPDG VST3
AppVersion={#MyAppVersion}
AppVerName=HPDG VST3 0.1.0 Beta 1
AppPublisher=HamloProd
VersionInfoVersion=0.1.0.1
VersionInfoCompany=HamloProd
VersionInfoCopyright=Copyright (C) 2026 HamloProd
DefaultDirName={commoncf64}\VST3\HPDG.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
#ifdef InstallerTest
Compression=lzma2/fast
#else
Compression=lzma2/ultra64
#endif
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no
UninstallDisplayIcon={app}\Contents\x86_64-win\HPDG.vst3
InfoBeforeFile=..\docs\BETA-RELEASE.txt
InfoAfterFile=..\docs\INSTALL-FINISH.txt
[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[InstallDelete]
; Replace the factory tree on updates, so old long names cannot create duplicates.
Type: filesandordirs; Name: "{app}\Contents\x86_64-win\Samples"
[Files]
Source: "{#SourceVst3Dir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\FAQ.md"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\docs\FAQ.html"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\RIGHTS.txt"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\docs\BETA-RELEASE.txt"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\docs\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "..\JUCE\LICENSE.md"; DestDir: "{app}\Documentation\ThirdParty\JUCE"; Flags: ignoreversion
Source: "..\Assets\Fonts\OFL.txt"; DestDir: "{app}\Documentation\ThirdParty\Neucha"; Flags: ignoreversion
Source: "{#VCRedistFile}"; Flags: dontcopy
#ifndef InstallerTest
[Icons]
Name: "{commonprograms}\HamloProd\HPDG FAQ"; Filename: "{app}\Documentation\FAQ.html"
Name: "{commonprograms}\HamloProd\Uninstall HPDG VST3"; Filename: "{uninstallexe}"
#endif
[Run]
Filename: "{app}\Documentation\FAQ.html"; Description: "Open HPDG FAQ / Открыть руководство"; Flags: postinstall shellexec skipifsilent unchecked
[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
  RuntimeVersion: String;
  Installed: Cardinal;
  InstalledVersion, RequiredVersion: Int64;
begin
  Result := '';
#ifdef InstallerTest
  { Payload test runs on the development machine without changing its runtime. }
  Exit;
#endif
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1) then
    if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', RuntimeVersion) then
      if StrToVersion(Copy(RuntimeVersion, 2, Length(RuntimeVersion)), InstalledVersion) and StrToVersion('{#RuntimeRequiredVersion}', RequiredVersion) then
        if ComparePackedVersion(InstalledVersion, RequiredVersion) >= 0 then Exit;
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
  begin
    Result := 'Could not start Microsoft Visual C++ Runtime setup.';
    Exit;
  end;
  if ExitCode = 3010 then NeedsRestart := True
  else if (ExitCode <> 0) and (ExitCode <> 1638) then
    Result := Format('Microsoft Visual C++ Runtime setup failed (code %d).', [ExitCode]);
end;
