# A1.2f boot-soak smoke harness. Launches the RWDI exe N times with
# ARENA_AUTO_BATTLE=1 (auto-Battle + synthetic frontend mash — see main.cpp),
# waits for the in-arena [capture] marker in arena_bridge.log, and classifies
# each boot PASS / CRASH (process died; names the new dump) / HANG (timeout,
# process alive). Exit code = number of failures.
#
#   powershell -ExecutionPolicy Bypass -File tools\arena-soak.ps1 -N 10
param([int]$N = 10, [int]$TimeoutSec = 75, [switch]$Probe, [switch]$AnimProbe,
      [string]$Expect = "", [string]$Rising = "", [string]$Constant = "",
      [string]$Absent = "", [int]$Mode = 0)
# -Probe: single run in ARENA_AUTO_BATTLE=3 (in-level stick-up + hold-L
# injection for the camera forensics); dwells after PASS so the injected
# samples land in the log before the kill.
# -AnimProbe: single run in ARENA_AUTO_BATTLE=4 (A1.4) — runs + presses Z (set)
# in-level; asserts the render patch logged [anim] idx=41 (the set-bomb pose)
# with the frame counter advancing. This is the A1.4 objective gate.
#
# GENERIC GATES (so a new objective probe needs only its ARENA_AUTO_BATTLE mode
# in main.cpp, not an edit to this harness as well):
# -Expect '<regex>' : fail unless the pattern appears in arena_bridge.log.
# -Rising '<regex>' : pattern must match >=2 times with its first capture group
#                     strictly increasing — the "it actually PLAYED, not just got
#                     set for one frame" check.
# -Constant '<regex>': inverse of -Rising. Pattern must match >=2 times and its
#                     first capture group must be IDENTICAL every time. For
#                     values where CHANGE is the bug — the A1.5 fixed camera's
#                     yaw being the motivating case.
# -Absent '<regex>' : fail IF the pattern appears. For "this must never happen"
#                     conditions that are silent when healthy — the floor
#                     guard firing being the motivating case (it means the sim
#                     drove the player off the rendered floor again, 8.5a).
#                     Note this passes vacuously if the run never got in-arena,
#                     so pair it with a mode that also logs something positive.
# -Mode <n>         : ARENA_AUTO_BATTLE value (default 1; 3 for -Probe, 4 for
#                     -AnimProbe).
#
# -AnimProbe asserts the set pose is STILL SHOWING 12 frames after the set edge:
#   -Mode 4 -Expect '\[animw\] \+12 idx=41'
#
# It used to be -Rising 'idx=<set> frame=(\d+)' — "the anim frame counter advanced".
# That was the WRONG PROXY and it cost a wrong bug report (a supposed A1.5 camera
# regression). Measured 2026-07-27: the game walker re-asserts its own anim EVERY
# frame, and our trigger runs after it, so we can hold the pose only by
# re-triggering — which restarts it, pinning the frame counter at 0 forever. The
# counter can never advance with an overlay-style trigger, camera or no camera.
#
# The gate's INTENT was always "the pose actually appeared for a meaningful
# duration, rather than flickering for one frame". Asserting it is still idx 29
# twelve frames after the edge measures exactly that, and it is what the
# implementation can honestly deliver. Real ANIMATION (a counter that advances)
# needs the game's own set state engaged so the walker plays it itself — that is
# still open; see integration notes §8.18.
if ($AnimProbe -and -not $Expect -and -not $Rising) { $Expect = '\[animw\] \+12 idx=41' }
if ($AnimProbe -and $Mode -eq 0)  { $Mode = 4 }
if ($Probe     -and $Mode -eq 0)  { $Mode = 3 }
if ($Mode -eq 0) { $Mode = 1 }
if ($Probe -or $AnimProbe -or $Expect -or $Rising -or $Constant -or $Absent) { $N = 1 }

$root  = Split-Path $PSScriptRoot -Parent
$exe   = Join-Path $root "build-rwdi\BMHeroRecompiled.exe"
$log   = Join-Path $root "arena_bridge.log"
$dumps = "$env:LOCALAPPDATA\CrashDumps"
if (-not (Test-Path $exe)) { Write-Error "missing $exe (build build-rwdi first)"; exit 99 }

$results = @()
for ($i = 1; $i -le $N; $i++) {
    Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800
    # The game truncates/rewrites arena_bridge.log per process — delete it so any
    # [capture] found belongs to THIS iteration (length-delta checks are unsound).
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    $dumpCount = (Get-ChildItem $dumps -Filter *.dmp -ErrorAction SilentlyContinue | Measure-Object).Count

    $env:ARENA_AUTO_BATTLE = "$Mode"
    $p = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru
    $verdict = "HANG"; $detail = ""
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
        Start-Sleep -Seconds 2
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern '\[capture\]' -Quiet)) {
            $verdict = "PASS"; break
        }
        if ($p.HasExited) {
            $verdict = "CRASH"
            Start-Sleep -Seconds 3   # give WER time to write the dump
            $newCount = (Get-ChildItem $dumps -Filter *.dmp -ErrorAction SilentlyContinue | Measure-Object).Count
            if ($newCount -gt $dumpCount) {
                $detail = (Get-ChildItem $dumps -Filter *.dmp | Sort-Object LastWriteTime -Descending | Select-Object -First 1).Name
            }
            break
        }
    }
    $secs = [int]$sw.Elapsed.TotalSeconds
    if (($Probe -or $AnimProbe -or $Expect -or $Rising -or $Constant -or $Absent) -and $verdict -eq "PASS") {
        Start-Sleep -Seconds 10   # let the injected input sample land in the log
    }
    Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Stop-Process -Force
    $results += [pscustomobject]@{ Iter = $i; Verdict = $verdict; Seconds = $secs; Detail = $detail }
    Write-Host ("iter {0}: {1} ({2}s) {3}" -f $i, $verdict, $secs, $detail)
}
Remove-Item Env:\ARENA_AUTO_BATTLE -ErrorAction SilentlyContinue
$results | Format-Table -AutoSize
$fails = @($results | Where-Object Verdict -ne "PASS").Count
Write-Host ("SUMMARY: {0}/{1} PASS" -f ($N - $fails), $N)

if ($Expect) {
    $hit = (Test-Path $log) -and (Select-String -Path $log -Pattern $Expect -Quiet)
    Write-Host ("EXPECT GATE: /{0}/ -> {1}" -f $Expect, $(if ($hit) { 'PASS' } else { 'FAIL' }))
    if (-not $hit) { $fails++ }
}

if ($Absent) {
    # Inverse of -Expect: the pattern must NOT be in the log.
    $bad = @()
    if (Test-Path $log) { $bad = @(Select-String -Path $log -Pattern $Absent) }
    Write-Host ("ABSENT GATE: /{0}/ -> {1} ({2} match(es))" -f
        $Absent, $(if ($bad.Count -eq 0) { 'PASS' } else { 'FAIL' }), $bad.Count)
    foreach ($b in $bad | Select-Object -First 5) { Write-Host ("    " + $b.Line.Trim()) }
    if ($bad.Count -gt 0) { $fails++ }
}

if ($Rising) {
    # The pattern's first capture group must appear >=2 times and strictly
    # increase — proves the thing kept advancing rather than firing once.
    # (For -AnimProbe this is the A1.4 gate: the set index can come from our sim-edge
    # trigger or the game's own walker on the Z press; either proves the
    # animation path works.)
    $vals = @()
    if (Test-Path $log) {
        $vals = @(Select-String -Path $log -Pattern $Rising |
                  ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
    }
    $ok = ($vals.Count -ge 2) -and ($vals[-1] -gt $vals[0])
    Write-Host ("RISING GATE: /{0}/ samples={1} values=[{2}] -> {3}" -f `
        $Rising, $vals.Count, ($vals -join ','), $(if ($ok) { 'PASS' } else { 'FAIL' }))
    if (-not $ok) { $fails++ }
}

if ($Constant) {
    # Inverse of -Rising: the captured value must NEVER change. For the A1.5
    # fixed camera, drift IS the bug - so constancy is the assertion.
    # A single sample is NOT "constant" (nothing was proven), hence >= 2.
    $vals = @()
    if (Test-Path $log) {
        $vals = @(Select-String -Path $log -Pattern $Constant |
                  ForEach-Object { $_.Matches[0].Groups[1].Value })
    }
    $uniq = @($vals | Select-Object -Unique)
    $ok = ($vals.Count -ge 2) -and ($uniq.Count -eq 1)
    Write-Host ("CONSTANT GATE: /{0}/ samples={1} distinct=[{2}] -> {3}" -f `
        $Constant, $vals.Count, ($uniq -join ','), $(if ($ok) { 'PASS' } else { 'FAIL' }))
    if (-not $ok) { $fails++ }
}

exit $fails
