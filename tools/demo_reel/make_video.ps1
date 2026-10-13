# Builds the HPDG demo video: synthesizes the demo samples, records the script against the real
# VST3 editor (frames + engine audio), then composes the MP4 with cursor, captions and title cards.
#
#   powershell -ExecutionPolicy Bypass -File tools\demo_reel\make_video.ps1 [-Out Releases\HPDG_demo.mp4]
#
# Needs: Release build of HPDG_DemoReel (cmake --build build --config Release --target HPDG_DemoReel),
# Python 3 with numpy, pillow, imageio-ffmpeg (pip install numpy pillow imageio-ffmpeg).
param(
    [string]$Script = "tools\demo_reel\demo.txt",
    [string]$Recording = "build\demo_reel",
    [string]$Out = "build\demo_reel\HPDG_demo.mp4"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Push-Location $root
try {
    python tools\demo_reel\make_samples.py tools\demo_reel\media
    if ($LASTEXITCODE -ne 0) { throw "make_samples failed" }
    & .\build\Release\HPDG_DemoReel.exe $Script $Recording
    if ($LASTEXITCODE -ne 0) { throw "HPDG_DemoReel failed" }
    python tools\demo_reel\compose.py $Recording $Out
    if ($LASTEXITCODE -ne 0) { throw "compose failed" }
    Write-Host "Video: $Out"
}
finally {
    Pop-Location
}
