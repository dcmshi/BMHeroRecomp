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

if ($fails) { Write-Host "`n[oracle-gate] FAILED: $($fails -join ', ')"; exit 1 }
Write-Host "`n[oracle-gate] ALL GREEN"; exit 0
