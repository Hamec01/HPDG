[CmdletBinding()]
param([switch]$SkipBuild, [string]$CompilerPath, [switch]$TestInstaller)
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path $PSScriptRoot -Parent
Push-Location $repoDir
try {
    $releaseDir = Join-Path $repoDir 'Releases'
    $toolDir = Join-Path $repoDir 'build\tools'
    $runtimeFile = Join-Path $repoDir 'build\prerequisites\vc_redist.x64.exe'
    New-Item -ItemType Directory -Force $releaseDir, $toolDir, (Split-Path $runtimeFile) | Out-Null
    if (!(Test-Path -LiteralPath $runtimeFile)) {
        Invoke-WebRequest 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile $runtimeFile
    }
    $runtimeSignature = Get-AuthenticodeSignature -LiteralPath $runtimeFile
    if ($runtimeSignature.Status -ne 'Valid' -or $runtimeSignature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
        throw 'Visual C++ Runtime must have a valid Microsoft signature.'
    }
    if (!$CompilerPath) {
        $CompilerPath = Join-Path $toolDir 'InnoSetup\ISCC.exe'
        if (!(Test-Path -LiteralPath $CompilerPath)) {
            $compilerSetup = Join-Path $toolDir 'innosetup-6.7.3.exe'
            Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile $compilerSetup
            $signature = Get-AuthenticodeSignature -LiteralPath $compilerSetup
            if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Pyrsys') { throw 'Invalid Inno Setup signature.' }
            $portableDir = Split-Path $CompilerPath
            $process = Start-Process -FilePath $compilerSetup -ArgumentList ('/PORTABLE=1 /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="' + $portableDir + '"') -WindowStyle Hidden -PassThru -Wait
            if ($process.ExitCode -ne 0) { throw "Inno Setup extraction failed: $($process.ExitCode)" }
        }
    }
    if (!$SkipBuild) {
        & cmake -S $repoDir -B build
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
        & cmake --build build --config Release --target HPDG_VST3 --parallel 4
        if ($LASTEXITCODE -ne 0) { throw 'VST3 build failed.' }
    }
    $bundleDir = Join-Path $repoDir 'build\HPDG_artefacts\Release\VST3\HPDG.vst3'
    $sampleRoot = Join-Path $repoDir 'Samples'
    $bundleSampleRoot = Join-Path $bundleDir 'Contents\x86_64-win\Samples'
    if (!(Test-Path -LiteralPath (Join-Path $bundleDir 'Contents\x86_64-win\HPDG.vst3'))) { throw 'VST3 binary missing.' }
    $manifest = @(Get-ChildItem -LiteralPath $sampleRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relativePath = $_.FullName.Substring($sampleRoot.Length + 1)
        $installedSource = Join-Path $bundleSampleRoot $relativePath
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        if (!(Test-Path -LiteralPath $installedSource) -or (Get-FileHash -LiteralPath $installedSource -Algorithm SHA256).Hash -ne $hash) {
            throw "Bundle sample missing or outdated: $relativePath. Rebuild VST3."
        }
        [pscustomobject]@{ path = $relativePath; bytes = $_.Length; sha256 = $hash }
    })
    if (@(Get-ChildItem -LiteralPath $bundleSampleRoot -Recurse -File).Count -ne $manifest.Count) { throw 'Bundle contains stale sample files.' }
    $audioCount = @($manifest | Where-Object { [IO.Path]::GetExtension($_.path) -in '.wav', '.aif', '.aiff', '.flac', '.ogg' }).Count
    if ($audioCount -ne 749) { throw "Expected 749 beta audio files; found $audioCount. Update release notes before publishing." }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $releaseDir 'HPDG_0.1.0-beta.1_samples.sha256.json') -Encoding utf8

    # Build a standalone, offline HTML FAQ from the authoritative Markdown text.
    $htmlLines = [Collections.Generic.List[string]]::new()
    $htmlLines.Add('<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>HPDG &#8212; FAQ</title><style>body{font:17px/1.65 system-ui,sans-serif;max-width:900px;margin:40px auto;padding:0 24px;color:#282522;background:#f8f3e7}h1,h2{line-height:1.25}h2{margin-top:36px}p{white-space:pre-line}strong{color:#6b420d}</style><body>')
    $paragraph = [Collections.Generic.List[string]]::new()
    foreach ($line in (Get-Content -LiteralPath (Join-Path $repoDir 'FAQ.md') -Encoding utf8)) {
        $encoded = [System.Net.WebUtility]::HtmlEncode($line)
        $encoded = [regex]::Replace($encoded, '\*\*(.*?)\*\*', '<strong>$1</strong>')
        if ($line.StartsWith('# ') -or $line.StartsWith('## ') -or !$line.Trim()) {
            if ($paragraph.Count) { $htmlLines.Add('<p>' + ($paragraph -join "`n") + '</p>'); $paragraph.Clear() }
            if ($line.StartsWith('## ')) { $htmlLines.Add('<h2>' + $encoded.Substring(3) + '</h2>') }
            elseif ($line.StartsWith('# ')) { $htmlLines.Add('<h1>' + $encoded.Substring(2) + '</h1>') }
        } else { $paragraph.Add($encoded) }
    }
    if ($paragraph.Count) { $htmlLines.Add('<p>' + ($paragraph -join "`n") + '</p>') }
    $htmlLines.Add('</body></html>')
    $htmlLines | Set-Content (Join-Path $repoDir 'docs\FAQ.html') -Encoding utf8
    $compilerArgs = @('/Qp', "/DSourceVst3Dir=$bundleDir", "/DVCRedistFile=$runtimeFile")
    if ($TestInstaller) {
        $testOutput = Join-Path $repoDir 'build\installer-test-output'
        New-Item -ItemType Directory -Force $testOutput | Out-Null
        $compilerArgs += '/DInstallerTest=1', "/DOutputDir=$testOutput"
    }
    & $CompilerPath @compilerArgs (Join-Path $PSScriptRoot 'HPDG_VST3.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
    if (!$TestInstaller) {
        $installer = Join-Path $releaseDir 'HPDG_VST3_Setup_0.1.0-beta.1.exe'
        $installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
        "$installerHash  $([IO.Path]::GetFileName($installer))" | Set-Content "$installer.sha256" -Encoding ascii
        Copy-Item FAQ.md, RIGHTS.txt, docs\FAQ.html, docs\BETA-RELEASE.txt -Destination $releaseDir -Force
        Write-Output "Beta ready: $installer ($audioCount audio samples)"
    }
} finally { Pop-Location }
