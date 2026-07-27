# RenderDoc capture of a running arena frame — GROUND TRUTH for anything visual.
#
# WHY THIS EXISTS: tools/capture-game.ps1 (PrintWindow) silently captured only the
# top-left QUARTER of the frame for weeks because of a DPI bug, and three wrong
# root causes were chased against those screenshots before a RenderDoc capture
# showed the A1.5 camera had been correct all along (integration notes §8.17).
# capture-game.ps1 is fixed and is still the cheap everyday tool — but when a
# screenshot disagrees with a measurement, capture the frame here and believe
# THIS one.
#
#   .\tools\rd-capture.ps1                     # capture at the default distance
#   .\tools\rd-capture.ps1 -Dist 2400 -Save    # ...and save the frame to PNG
#
# No keyboard is involved. renderdoccmd has no "trigger capture" verb and F12
# needs foreground focus, which a background process cannot reliably take. So the
# game is launched hooked, and qrenderdoc's EMBEDDED Python (no system Python or
# packages needed) connects over target control and triggers the capture.
param(
    [string] $Dist   = "2800",
    [string] $AtDx   = "0",
    [string] $AtDz   = "0",
    [int]    $Mode   = 1,
    [string] $Tag    = "arena",
    [string] $OutDir = "",
    [switch] $Save
)
$ErrorActionPreference = "Continue"
$fork  = Split-Path $PSScriptRoot -Parent
$rdDir = Join-Path ${env:ProgramFiles} "RenderDoc"
if (-not (Test-Path (Join-Path $rdDir "renderdoccmd.exe"))) {
    Write-Output "RenderDoc not found at $rdDir"; exit 1
}
if (-not $OutDir) { $OutDir = Join-Path $fork "tools\rdcaps" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Get-Process BMHeroRecompiled, qrenderdoc -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800

Set-Location $fork
if (Test-Path "$fork\arena_bridge.log") { Clear-Content "$fork\arena_bridge.log" }
$env:ARENA_AUTO_BATTLE = "$Mode"
$env:ARENA_CAM_DIST    = $Dist
$env:ARENA_CAM_AT_DX   = $AtDx
$env:ARENA_CAM_AT_DZ   = $AtDz

Start-Process -FilePath (Join-Path $rdDir "renderdoccmd.exe") -ArgumentList @(
    "capture", "-d", $fork, "-c", (Join-Path $OutDir $Tag),
    "$fork\build-rwdi\BMHeroRecompiled.exe") | Out-Null

$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 70) {
    Start-Sleep -Seconds 1
    if ((Test-Path "$fork\arena_bridge.log") -and
        (Select-String -Path "$fork\arena_bridge.log" -Pattern '\[capture\] level' -Quiet)) { break }
}
Write-Output "in-arena at $([int]$sw.Elapsed.TotalSeconds)s"
Start-Sleep -Seconds 5

# Paths go through the ENVIRONMENT. qrenderdoc's usage is
# `qrenderdoc [options] filename`, so an extra argument after `--python x.py` is
# taken as a capture to open and never reaches the script; and the script cannot
# derive its own location either (__file__ undefined, argv[0] is qrenderdoc.exe
# under Program Files, which is not writable - it then dies with no log at all).
$trigLog = Join-Path $OutDir "rd_trigger.log"
$env:ARENA_RD_LOG = $trigLog
$q = Start-Process -PassThru -FilePath (Join-Path $rdDir "qrenderdoc.exe") `
        -ArgumentList @("--python", (Join-Path $PSScriptRoot "rd_trigger.py"))
$q | Wait-Process -Timeout 120 -ErrorAction SilentlyContinue
if (-not $q.HasExited) { $q | Stop-Process -Force }

if (Test-Path $trigLog) { Get-Content $trigLog | Select-String -Pattern 'CAPTURED|FATAL|EXCEPTION' }

Get-Process BMHeroRecompiled, qrenderdoc -ErrorAction SilentlyContinue | Stop-Process -Force
$env:ARENA_AUTO_BATTLE = ''; $env:ARENA_CAM_DIST = ''
$env:ARENA_CAM_AT_DX = '';   $env:ARENA_CAM_AT_DZ = ''

$rdc = Get-ChildItem "$OutDir\$Tag*.rdc" -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $rdc) { Write-Output "NO CAPTURE PRODUCED"; exit 1 }
Write-Output ("CAPTURE: {0} ({1:N1} MB)" -f $rdc.FullName, ($rdc.Length / 1MB))

if ($Save) {
    # rd_saveframe.py reads the capture path and output PNG from the environment.
    $png = [IO.Path]::ChangeExtension($rdc.FullName, ".png")
    $env:ARENA_RD_CAP = $rdc.FullName
    $env:ARENA_RD_PNG = $png
    $env:ARENA_RD_LOG = ""
    $q2 = Start-Process -PassThru -FilePath (Join-Path $rdDir "qrenderdoc.exe") `
            -ArgumentList @("--python", (Join-Path $PSScriptRoot "rd_saveframe.py"))
    $q2 | Wait-Process -Timeout 180 -ErrorAction SilentlyContinue
    if (-not $q2.HasExited) { $q2 | Stop-Process -Force }
    if (Test-Path $png) { Write-Output "FRAME PNG: $png" } else { Write-Output "frame PNG not produced" }
}
