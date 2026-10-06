[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path $PSScriptRoot -Parent
$testExe = Join-Path $repoDir 'build\installer-test-output\HPDG_Beta_InstallTest.exe'
$testDir = Join-Path $repoDir ('build\installer-smoke-' + [Guid]::NewGuid().ToString('N'))
$pluginDir = Join-Path $testDir 'HPDG.vst3'
New-Item -ItemType Directory -Path $testDir | Out-Null
$obsoleteDir = Join-Path $pluginDir 'Contents\x86_64-win\Samples\Techno\Kick'
New-Item -ItemType Directory -Path $obsoleteDir -Force | Out-Null
'obsolete factory sample' | Set-Content (Join-Path $obsoleteDir 'old-long-name.wav')
$setupArgs = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="' + $pluginDir + '" /LOG="' + (Join-Path $testDir 'setup.log') + '"'
$process = Start-Process -FilePath $testExe -ArgumentList $setupArgs -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "Isolated installation failed: $($process.ExitCode). See $testDir\setup.log" }
$manifest = Get-Content (Join-Path $repoDir 'Releases\HPDG_0.1.0-beta.1_samples.sha256.json') -Raw | ConvertFrom-Json
$sampleDir = Join-Path $pluginDir 'Contents\x86_64-win\Samples'
foreach ($entry in $manifest) {
    $samplePath = Join-Path $sampleDir $entry.path
    if (!(Test-Path -LiteralPath $samplePath) -or (Get-FileHash -LiteralPath $samplePath).Hash -ne $entry.sha256) {
        throw "Installed sample mismatch: $($entry.path)"
    }
}
if (@(Get-ChildItem -LiteralPath $sampleDir -Recurse -File).Count -ne $manifest.Count) { throw 'Unexpected installed sample files.' }
$dll = Join-Path $pluginDir 'Contents\x86_64-win\HPDG.vst3'
$sourceDll = Join-Path $repoDir 'build\HPDG_artefacts\Release\VST3\HPDG.vst3\Contents\x86_64-win\HPDG.vst3'
if ((Get-FileHash $dll).Hash -ne (Get-FileHash $sourceDll).Hash) { throw 'Installed binary mismatch.' }
foreach ($document in @('FAQ.html', 'FAQ.md', 'RIGHTS.txt', 'BETA-RELEASE.txt', 'THIRD-PARTY-NOTICES.txt')) {
    if (!(Test-Path -LiteralPath (Join-Path $pluginDir "Documentation\$document"))) { throw "Missing document: $document" }
}
if (Select-String -LiteralPath (Join-Path $pluginDir 'Documentation\FAQ.html') -Pattern 'â€' -Quiet) { throw 'FAQ encoding error.' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HpdgBetaNative {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
}
'@
$module = [HpdgBetaNative]::LoadLibrary($dll)
if ($module -eq [IntPtr]::Zero) { throw "Installed VST3 failed to load: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
try {
    if ([HpdgBetaNative]::GetProcAddress($module, 'GetPluginFactory') -eq [IntPtr]::Zero) { throw 'VST3 factory export missing.' }
} finally { [HpdgBetaNative]::FreeLibrary($module) | Out-Null }

# Use only the uninstaller generated in this verified workspace test directory.
$resolvedTestDir = [IO.Path]::GetFullPath($testDir)
$workspaceBuild = [IO.Path]::GetFullPath((Join-Path $repoDir 'build')) + [IO.Path]::DirectorySeparatorChar
if (!$resolvedTestDir.StartsWith($workspaceBuild, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe uninstall test path.' }
$uninstaller = Join-Path $pluginDir 'unins000.exe'
$process = Start-Process -FilePath $uninstaller -ArgumentList ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="' + (Join-Path $testDir 'uninstall.log') + '"') -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "Isolated uninstall failed: $($process.ExitCode)" }
if (Test-Path -LiteralPath $dll) { throw 'Plugin binary remained after uninstall.' }
if (Test-Path -LiteralPath $sampleDir) { throw 'Factory library remained after uninstall.' }
Write-Output "PASS: installation, $($manifest.Count) library file hashes, documentation, VST3 DLL loading and uninstall. Logs: $testDir"
