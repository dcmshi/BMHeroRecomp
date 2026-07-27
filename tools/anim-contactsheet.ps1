# Capture one labelled screenshot per player ANIMATION INDEX, so the right pose
# can be identified by flipping through images instead of watching a live sweep
# and trying to catch the moment.
#
#   .\tools\anim-contactsheet.ps1                       # defaults
#   .\tools\anim-contactsheet.ps1 -Pitch 25 -Dist 400   # closer / more side-on
#
# How it works: the game runs with ARENA_ANIM_SWEEP, which cycles every index and
# logs each one as it starts. This polls that log and screenshots shortly after
# each change, naming the file for the index that is actually on screen - so the
# label comes from the game's own report, not from counting elapsed time.
#
# Two shots per index (early and late in the hold) because a single frame of a
# motion can be ambiguous.
param(
    [int]    $HoldTicks = 120,     # ticks each index is held (~2s at 60fps)
    [double] $Pitch     = 30,      # camera pitch; play uses 60, which is near top-down
    [int]    $Dist      = 450,     # camera distance; play uses 2800
    [int]    $MaxIndex  = 52,      # the RE puts player anims at <= ~52
    [string] $OutDir    = ""
)
$ErrorActionPreference = "Continue"
$fork = Split-Path $PSScriptRoot -Parent
if (-not $OutDir) { $OutDir = Join-Path $fork "tools\anims" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem "$OutDir\anim_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force

$log = Join-Path $fork "arena_bridge.log"
Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
if (Test-Path $log) { Clear-Content $log }

$env:ARENA_AUTO_BATTLE = '1'          # idle in the arena, no injected movement
$env:ARENA_CAM_FOLLOW  = '1'          # keep the bomber framed
$env:ARENA_CAM_DIST    = "$Dist"
$env:ARENA_CAM_PITCH   = "$Pitch"
$env:ARENA_ANIM_SWEEP  = "$HoldTicks"

Set-Location $fork
$p = Start-Process -PassThru -FilePath "$fork\build-rwdi\BMHeroRecompiled.exe" -WorkingDirectory $fork
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 70) {
    Start-Sleep -Seconds 1
    if ((Test-Path $log) -and (Select-String -Path $log -Pattern '\[capture\] level' -Quiet)) { break }
}
Write-Host "in-arena at $([int]$sw.Elapsed.TotalSeconds)s - capturing $($MaxIndex+1) animations"

$seen = @{}
$deadline = (Get-Date).AddSeconds(($MaxIndex + 2) * ($HoldTicks / 60.0) + 40)
while ((Get-Date) -lt $deadline) {
    if ($p.HasExited) { Write-Host "game exited early"; break }
    $line = Get-Content $log -ErrorAction SilentlyContinue |
            Select-String -Pattern '\[animsweep\] now playing idx=(\d+)' |
            Select-Object -Last 1
    if ($line) {
        $idx = [int]$line.Matches[0].Groups[1].Value
        if (-not $seen.ContainsKey($idx)) {
            $seen[$idx] = $true
            Start-Sleep -Milliseconds 500          # let the pose develop
            & "$fork\tools\capture-game.ps1" | Out-Null
            Copy-Item "$fork\tools\game.png" (Join-Path $OutDir ("anim_{0:d2}_a.png" -f $idx)) -Force
            Start-Sleep -Milliseconds 900          # a later frame of the same clip
            & "$fork\tools\capture-game.ps1" | Out-Null
            Copy-Item "$fork\tools\game.png" (Join-Path $OutDir ("anim_{0:d2}_b.png" -f $idx)) -Force
            Write-Host ("  captured idx {0,2}" -f $idx)
            if ($seen.Count -gt $MaxIndex) { break }
        }
    }
    Start-Sleep -Milliseconds 150
}

Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Stop-Process -Force
$env:ARENA_AUTO_BATTLE=''; $env:ARENA_CAM_FOLLOW=''; $env:ARENA_CAM_DIST=''
$env:ARENA_CAM_PITCH='';   $env:ARENA_ANIM_SWEEP=''
Write-Host "`n$($seen.Count) animations captured to $OutDir"
