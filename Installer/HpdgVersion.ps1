# Reads the HPDG release version from CMakeLists.txt (the only place it is written):
# project(HPDG VERSION x.y.z) + set(HPDG_VERSION_LABEL "beta.N"). Dot-source this file.
function Get-HpdgVersion([string]$RepoDir) {
    $cmakeText = Get-Content -LiteralPath (Join-Path $RepoDir 'CMakeLists.txt') -Raw
    if ($cmakeText -notmatch 'project\(HPDG VERSION (\d+)\.(\d+)\.(\d+)\)') { throw 'project(HPDG VERSION x.y.z) not found in CMakeLists.txt.' }
    $number = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
    if ($cmakeText -notmatch 'set\(HPDG_VERSION_LABEL "([a-z]*)\.?(\d*)"\)') { throw 'HPDG_VERSION_LABEL not found in CMakeLists.txt.' }
    $labelName = $Matches[1]; $labelNumber = $Matches[2]
    $tag = $number; $display = $number
    if ($labelName) {
        $tag = "$number-$labelName" + $(if ($labelNumber) { ".$labelNumber" } else { '' })
        $display = "$number " + $labelName.Substring(0, 1).ToUpper() + $labelName.Substring(1) + $(if ($labelNumber) { " $labelNumber" } else { '' })
    }
    [pscustomobject]@{
        Tag = $tag                # 0.1.0-beta.2: file names and latest.json
        Display = $display        # 0.1.0 Beta 2: installer and plugin header
        FileVersion = "$number." + $(if ($labelNumber) { $labelNumber } else { '0' })
    }
}
