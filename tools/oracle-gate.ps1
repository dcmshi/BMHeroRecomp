# Assert the ARENA build matches the single-player goldens (tools\oracle\goldens.json).
# Five FAIL-able assertions; every expected value comes from the GAME, not from a
# constant we chose (trap #1: a gate that asserts your own assumption cannot fail).
# Falsify it with `$env:ARENA_SET_ANIM = '3'` - the soak launches inherit this
# shell's env, so the gate must go red.
param([string]$GoldensPath = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$log  = Join-Path $root "arena_bridge.log"
$soak = Join-Path $root "tools\arena-soak.ps1"
if (-not $GoldensPath) { $GoldensPath = Join-Path $root "tools\oracle\goldens.json" }
if (-not (Test-Path $GoldensPath)) { Write-Error "no goldens at $GoldensPath - run tools\oracle.ps1 first"; exit 1 }
$g = Get-Content $GoldensPath -Raw | ConvertFrom-Json
$fails = @()
function Check([string]$name, [bool]$ok, [string]$detail) {
    $mark = if ($ok) { "PASS" } else { $script:fails += $name; "FAIL" }
    Write-Host ("[oracle-gate] {0,-28} {1}  {2}" -f $name, $mark, $detail)
}
# arena-soak DELETES arena_bridge.log per iteration, so the whole file belongs to
# the run we just made - a length-delta scan would be unsound (and silently empty).
function RunSoak([string[]]$soakArgs) {
    & powershell -ExecutionPolicy Bypass -File $soak @soakArgs | Out-Host
    if (Test-Path $log) { ,(Get-Content $log) } else { ,@() }
}
# "the clip PLAYED", not "the index was set for one frame". Compared against the
# MAX rather than the last sample because a clip that runs to its end wraps its
# frame counter back to 0 (measured 2026-07-30, §8.23).
function Advanced($m) {
    $v = @($m | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
    ($v.Count -ge 2) -and (($v | Measure-Object -Maximum).Maximum -gt $v[0])
}

# --- checks 1-3: set pose identity/length + bomb rest lift (one mode-4 boot) ---
$m4 = RunSoak @("-N","1","-Mode","4","-Rising","\[animw\] \+\d+ idx=$($g.set_anim_idx) frame=(\d+)")
$poseLines = @($m4 | Select-String "\[animw\] \+\d+ idx=$($g.set_anim_idx) frame=(\d+)")
Check "set-pose idx==golden" ((Advanced $poseLines)) `
      "golden idx=$($g.set_anim_idx), saw $($poseLines.Count) frames"
$lenOK = [math]::Abs($poseLines.Count - $g.set_anim_frames) -le 1
Check "set-pose plays once, full clip" $lenOK "golden $($g.set_anim_frames) frames, saw $($poseLines.Count)"
$setdbg = $m4 | Select-String '\[setdbg\] .* wy=([-\d.]+) originY=([-\d.]+)' | Select-Object -First 1
if ($setdbg) {
    $lift = [double]$setdbg.Matches[0].Groups[1].Value - [double]$setdbg.Matches[0].Groups[2].Value
    Check "bomb rest lift" ([math]::Abs($lift - $g.bomb_rest_lift) -le 2.0) `
          ("golden {0}, arena {1:N1}" -f $g.bomb_rest_lift, $lift)
} else { Check "bomb rest lift" $false "no [setdbg] line in mode-4 log" }

# --- check 4: the walk-in kick plays the game's own kick clip (mode-10 boot) ---
$m10 = RunSoak @("-N","1","-Mode","10","-Rising","\[anim\] idx=$($g.kick_anim_idx) frame=(\d+)")
$kickLines = @($m10 | Select-String "\[anim\] idx=$($g.kick_anim_idx) frame=(\d+)")
Check "kick-pose idx==golden" ((Advanced $kickLines)) `
      "golden idx=$($g.kick_anim_idx), saw $($kickLines.Count) frames"
# feel round 4: the shared 10-frame window cut the 18-frame kick clip mid-play
# and the walker stomped the rest - assert the FULL golden length now.
$kickLenOK = [math]::Abs($kickLines.Count - $g.kick_anim_frames) -le 2
Check "kick-pose full clip" $kickLenOK "golden $($g.kick_anim_frames) frames, saw $($kickLines.Count)"

# --- check 5: throw impact-detonation inside the golden flight envelope -------
$m11 = RunSoak @("-N","1","-Mode","11","-Expect","\[blastvis\]")
$tThrow = $m11 | Select-String '\[throw\] t(\d+)'       | Select-Object -First 1
$tBlast = $m11 | Select-String '\[blastvis\] .* t(\d+)' | Select-Object -First 1
if ($tThrow -and $tBlast) {
    $dt  = [int]$tBlast.Matches[0].Groups[1].Value - [int]$tThrow.Matches[0].Groups[1].Value
    $cap = [math]::Ceiling($g.throw_flight_frames * 1.25)
    $ok  = $g.throw_impact_detonates -and ($dt -gt 0) -and ($dt -le $cap) -and ($dt -lt 100)
    Check "throw impact-detonates" $ok "golden flight $($g.throw_flight_frames) (cap $cap), arena dt=$dt"
} else { Check "throw impact-detonates" $false "missing [throw]/[blastvis] in mode-11 log" }

# --- check 7: air-set leaves the walker clean (feel round 4 regression) -------
# The walker's solid-object reaction to a bomb actor (PUSH 42 / CARRY-SQUASH 52)
# suspended the fall when entered mid-air ("setting midair keeps flying off
# screen") and pinned the kick clip. Containment resets it same-frame; this
# gate proves the reaction never SURVIVES a frame - it must find the [airset]
# channel alive and zero 42/52 samples across a jump+air-set+moving-set script.
$m12 = RunSoak @("-N","1","-Mode","12","-Expect","\[airset\]")
$asAll = @($m12 | Select-String '\[airset\] ')
$asBad = @($m12 | Select-String '\[airset\] .* state=(42|52) ')
Check "air-set: no push/carry lock" (($asAll.Count -gt 0) -and ($asBad.Count -eq 0)) `
      "airset samples=$($asAll.Count), 42/52 samples=$($asBad.Count)"

# --- check 8: the VISIBLE jump flies the sim's arc (round 6 Y-drive) ----------
# The walker's own jump is a fixed mini-hop (~87 units); player 0's Pos.y is
# driven from the sim while airborne. [ydrive] logs the driven value - its
# peak must reach the sim's apex region (sim jump ~2.1 su = ~494 world; the
# floor is 240), or the drive has regressed to the mini-hop.
$yd = @($m12 | Select-String '\[ydrive\] .* drivenY=([\d.]+)' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
$peak = if ($yd.Count) { ($yd | Measure-Object -Maximum).Maximum } else { 0 }
Check "jump: visible Y flies the sim arc" ($peak -ge 420) "peak drivenY=$peak (floor 240, sim apex ~494)"

# --- check 9: the stun plays the game's own HIT clip (round 7) ----------------
# Mode 12's second air-set bomb settles at the probe's feet and fuses out; the
# blast tumbles the player, whose pose must be the golden hit clip - free, from
# the same mode-12 boot as checks 7-8.
$hitLines = @($m12 | Select-String "\[anim\] idx=$($g.hit_anim_idx) frame=(\d+)")
Check "stun plays the hit clip" ((Advanced $hitLines) -and ([math]::Abs($hitLines.Count - $g.hit_anim_frames) -le 3)) `
      "golden idx=$($g.hit_anim_idx) x$($g.hit_anim_frames), saw $($hitLines.Count) frames"

# --- check 10: airset plays the legs-up clip (round 9; same mode-12 boot) -----
# A midair set goes FREE->FALLING, which round 9 found was never latched - the
# jump clip kept playing. The golden airset clip must play with frames rising,
# then hand back to the jump clip. Mode 12 air-sets TWICE, so measure the FIRST
# contiguous run of the clip, not the whole-log count (14 total = 2 episodes -
# the first version of this check conflated them).
$runFrames = @()
foreach ($l in @($m12 | Select-String "\[anim\] idx=(\d+) frame=(\d+)")) {
    $ix = [int]$l.Matches[0].Groups[1].Value
    if ($ix -eq $g.airset_anim_idx) { $runFrames += [int]$l.Matches[0].Groups[2].Value }
    elseif ($runFrames.Count -gt 0) { break }
}
$asOK = ($runFrames.Count -ge 2) -and
        (($runFrames | Measure-Object -Maximum).Maximum -gt $runFrames[0]) -and
        ([math]::Abs($runFrames.Count - $g.airset_anim_frames) -le 3)
Check "airset plays the golden clip" $asOK `
      "golden idx=$($g.airset_anim_idx) x$($g.airset_anim_frames), first run $($runFrames.Count) frames"

# --- checks 11-13: carry clips + charge-hide + stand-on-bomb (mode-13 boot) ---
# Round 9. The walker's own carry is broken in battle (its pool bomb dies to
# the per-frame sweep, clip 41 re-triggered forever - feet frozen); the bridge
# drives the golden carry clips through the pose window instead.
$m13 = RunSoak @("-N","1","-Mode","13","-TimeoutSec","130","-Expect","\[hitpose\]")
$cwLines = @($m13 | Select-String "\[carryw\] idx=$($g.carry_walk_anim_idx) frame=(\d+)")
Check "carry-walk clip plays" ((Advanced $cwLines)) `
      "golden idx=$($g.carry_walk_anim_idx), saw $($cwLines.Count) moving-carry frames"
# the hand bomb hides the moment the charge window opens (round-9 overlap
# frames; round 11 keys it on the golden TIMER, which also covers the
# charge-run clip - the clip-26 check missed charged MOVEMENT)
$ch = @($m13 | Select-String "\[chargehide\] tm=$($g.windup_start_frames) ")
Check "charge hides the hand bomb" ($ch.Count -ge 1) `
      "golden windup start $($g.windup_start_frames)f, chargehide-at-threshold lines=$($ch.Count)"
# landing on a set bomb: the old airborne-only Y handback let the walker ground
# on the bomb ACTOR's box (plateau at floor+210). Any sustained plateau must be
# at a vanilla-legal height: the floor, or (only if vanilla supports standing
# on bombs) the golden stand lift. Plateau-based so the jump arc and the
# end-of-run tumble can't fake it.
$psY = @($m13 | Select-String '\[pstand\] f\d+ gameY=([-\d.]+)' |
         ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
$orig = $m13 | Select-String '\[setdbg\] .* originY=([-\d.]+)' | Select-Object -First 1
if ($psY.Count -ge 30 -and $orig) {
    $o = [double]$orig.Matches[0].Groups[1].Value
    $legal = @(0.0); if ($g.bomb_stand_supported) { $legal += [double]$g.bomb_stand_lift }
    $badPlateau = $null
    for ($i = 0; $i -le $psY.Count - 8; $i++) {
        $w = $psY[$i..($i+7)]
        $span = ($w | Measure-Object -Maximum).Maximum - ($w | Measure-Object -Minimum).Minimum
        if ($span -lt 1.0) {
            $lift = (($w | Measure-Object -Average).Average) - $o
            $ok = $false
            foreach ($L in $legal) { if ([math]::Abs($lift - $L) -le 8) { $ok = $true } }
            if (-not $ok -and $lift -gt 20) { $badPlateau = [math]::Round($lift,1); break }
        }
    }
    Check "no invisible box on bombs" ($null -eq $badPlateau) `
          ("legal lifts: {0}; {1}" -f ($legal -join '/'), $(if ($null -ne $badPlateau) { "ILLEGAL plateau at +$badPlateau" } else { "all plateaus legal" }))
} else { Check "no invisible box on bombs" $false "pstand samples=$($psY.Count), setdbg=$([bool]$orig)" }

# --- check 14: the release plays the throw clip (round 10; same mode-13 boot) -
# The walker's own throw trigger is a ONE-SHOT that can land on the carry
# window's closing frame and get dropped - then nothing re-asserts and the hold
# clip stayed up while the bomb arced ("sometimes the throw animation doesn't
# play"). The bridge drives the golden clip on the HELD->AIRBORNE edge; first
# contiguous run, same rationale as the airset check.
$thFrames = @()
foreach ($l in @($m13 | Select-String "\[anim\] idx=(\d+) frame=(\d+)")) {
    $ix = [int]$l.Matches[0].Groups[1].Value
    if ($ix -eq $g.throw_anim_idx) { $thFrames += [int]$l.Matches[0].Groups[2].Value }
    elseif ($thFrames.Count -gt 0) { break }
}
$thOK = ($thFrames.Count -ge 2) -and
        (($thFrames | Measure-Object -Maximum).Maximum -gt $thFrames[0]) -and
        ([math]::Abs($thFrames.Count - $g.throw_anim_frames) -le 3)
Check "release plays the throw clip" $thOK `
      "golden idx=$($g.throw_anim_idx) x$($g.throw_anim_frames), first run $($thFrames.Count) frames"

# --- check 15: charged movement moves the feet (round 10) ---------------------
# The windmill (windup_anim_idx) has static legs; vanilla switches to a distinct
# charge-run clip (windup_walk_anim_idx) while moving. [carryw] logs only while
# carrying AND moving, so the windmill must never appear there, and the golden
# charge-run clip must play with frames advancing.
$cwWind = @($m13 | Select-String "\[carryw\] idx=$($g.windup_anim_idx) ")
$cwRun  = @($m13 | Select-String "\[carryw\] idx=$($g.windup_walk_anim_idx) frame=(\d+)")
Check "charged movement moves the feet" (($cwWind.Count -eq 0) -and (Advanced $cwRun)) `
      "windmill($($g.windup_anim_idx)) while moving: $($cwWind.Count) (want 0); charge-run($($g.windup_walk_anim_idx)): $($cwRun.Count) frames"

# --- check 16: the MIDAIR release plays the air-throw clip (round 11) ---------
# A midair release is its own verb: vanilla plays a quick toss (air_throw_*),
# not the grounded lean - which mid-flight read as "pushing bomberman up".
# Mode 13's second carry releases ~6 ticks into the jump's descent.
$atFrames = @()
foreach ($l in @($m13 | Select-String "\[anim\] idx=(\d+) frame=(\d+)")) {
    $ix = [int]$l.Matches[0].Groups[1].Value
    if ($ix -eq $g.air_throw_anim_idx) { $atFrames += [int]$l.Matches[0].Groups[2].Value }
    elseif ($atFrames.Count -gt 0) { break }
}
$atOK = ($atFrames.Count -ge 2) -and
        (($atFrames | Measure-Object -Maximum).Maximum -gt $atFrames[0]) -and
        ([math]::Abs($atFrames.Count - $g.air_throw_anim_frames) -le 2)
Check "midair release plays the air toss" $atOK `
      "golden idx=$($g.air_throw_anim_idx) x$($g.air_throw_anim_frames), first run $($atFrames.Count) frames"

if ($fails) { Write-Host "`n[oracle-gate] FAILED: $($fails -join ', ')"; exit 1 }
Write-Host "`n[oracle-gate] ALL GREEN"; exit 0
