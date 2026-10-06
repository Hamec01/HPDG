param([string]$SamplesRoot = (Join-Path $PSScriptRoot '../Samples'))
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($SamplesRoot).TrimEnd('\')
$prefixes = @{ BoomBap='B'; Trap='T'; Techno='T'; DnB='D'; Rap='R'; Drill='DR' }
$laneCodes = @{ Kick='Kk'; ClapGhost='CG'; HiHat='HH'; Perc='PC'; Snare='SN'; GhostKick='GK'; HatFX='HF'; OpenHat='OH'; Ride='RD'; Cymbal='CY'; Sub808='SB' }
$rawCodes = @{ KC='Kk'; CP='CG'; CH='HH'; SN='SN'; RS='SN'; SH='PC'; TM='PC'; CB='PC'; WB='PC'; DJ='HF'; FX='HF'; CM='CY' }
$total = 0
foreach ($genre in Get-ChildItem -LiteralPath $root -Directory) {
    if (-not $prefixes.ContainsKey($genre.Name)) { throw "Unknown genre: $($genre.Name)" }
    foreach ($group in (Get-ChildItem -LiteralPath $genre.FullName -Recurse -File | Where-Object { $_.Extension -match '^\.(wav|aif|aiff|flac|mp3)$' } | Group-Object DirectoryName)) {
        $folder = $group.Name
        $manifestPath = Join-Path $folder 'sample-names.json'
        if (Test-Path -LiteralPath $manifestPath) {
            $existing = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            foreach ($file in $group.Group) {
                if (-not $existing.PSObject.Properties[$file.Name]) { throw "Unmapped sample: $($file.FullName)" }
            }
            continue
        }
        [string[]]$names = @($group.Group | ForEach-Object Name)
        [Array]::Sort($names, [StringComparer]::OrdinalIgnoreCase)
        $counters = @{}
        $aliases = [ordered]@{}
        $plan = @()
        foreach ($name in $names) {
            $file = Get-Item -LiteralPath (Join-Path $folder $name)
            $lane = $file.Directory.Name
            if ($laneCodes.ContainsKey($lane)) { $code = $laneCodes[$lane] }
            elseif ($lane -match '_SYN') { $code = 'SY' }
            elseif ($lane -match '_EFX') { $code = 'HF' }
            elseif ($file.BaseName -match '^\d+D\d([A-Z]{2})' -and $rawCodes.ContainsKey($Matches[1])) { $code = $rawCodes[$Matches[1]] }
            else { $code = 'PC' }
            $counters[$code] = 1 + [int]$counters[$code]
            $newName = $prefixes[$genre.Name] + $code + $counters[$code] + $file.Extension.ToLowerInvariant()
            $target = [IO.Path]::GetFullPath((Join-Path $folder $newName))
            if (-not $target.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Target outside Samples' }
            $aliases[$newName] = $file.BaseName
            $plan += [PSCustomObject]@{ Source=$file.FullName; Target=$target; Temp=(Join-Path $folder ([Guid]::NewGuid().ToString() + '.rename-tmp')); Hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
        }
        foreach ($item in $plan) {
            if ((Test-Path -LiteralPath $item.Target) -and $item.Target -notin $plan.Source) { throw "Target already exists: $($item.Target)" }
        }
        # Write recovery metadata before any file is moved. Two passes allow name swaps.
        $aliases | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        foreach ($item in $plan) { Move-Item -LiteralPath $item.Source -Destination $item.Temp }
        foreach ($item in $plan) {
            Move-Item -LiteralPath $item.Temp -Destination $item.Target
            if ((Get-FileHash -LiteralPath $item.Target -Algorithm SHA256).Hash -ne $item.Hash) { throw "Audio changed: $($item.Target)" }
        }
        $total += $plan.Count
        Write-Output "$($genre.Name)/$($folder.Substring($genre.FullName.Length + 1)): $($plan.Count)"
    }
}
Write-Output "Renamed and SHA256 verified: $total samples"
