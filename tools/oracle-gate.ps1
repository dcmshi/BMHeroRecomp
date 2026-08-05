# Assert the ARENA build matches the single-player goldens (tools\oracle\goldens.json).
# Eighteen FAIL-able assertions; every expected value comes from the GAME, not a
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

# --- check 17: full anim-timeline diff (oracle 2.0) ---------------------------
# Every shared verb's ENTIRE animation timeline must match vanilla's within
# +/-3 frames per run - the blanket net the sixteen bespoke checks are not: any
# wrong clip, duration or transition in a covered verb goes red here. Mode 13 is
# the LAST boot above, so arena_bridge.log still holds its log.
#
# The verbs vanilla and the arena genuinely disagree on live in
# tools\oracle\known-divergences.json, one line of reason each. The register is
# read BOTH ways: an unregistered FAIL fails the gate (a new divergence), and a
# registered verb that PASSES also fails it, asking for the entry to be removed.
# Without that second direction the register would rot into a permanent mute -
# the vacuous green this whole instrument exists to prevent.
#
# COVERAGE FLOOR: the differ skips a verb whose [verb] marker never fired, so
# without a count assertion six of the ten shared verbs could quietly leave the
# comparison and this check would still print PASS. The floor is DERIVED from
# the verb tables themselves (task #33) - the hand-kept constant it replaces
# decayed by construction: a verb added past it was silently un-floored. The
# intersection of kOracleScript and kBattleScript names IS the compared set
# (battle-only and oracle-only names never meet in the differ), so parsing the
# header keeps the floor honest without a rebuild. Fails closed: an unparseable
# header yields 0 shared verbs, which no differ run can satisfy... except by
# comparing 0 - hence the explicit parse check below. The floor also catches
# the wrong log being parsed (this check reads arena_bridge.log, which only
# holds the mode-13 boot because mode 13 is the LAST RunSoak above).
$vsPath = Join-Path $root "src\main\verb_script.h"
function Get-VerbNames([string]$src, [string]$table) {
    $m = [regex]::Match($src, "$table\[\]\s*=\s*\{(.*?)\n\};", 'Singleline')
    if (-not $m.Success) { return @() }
    @([regex]::Matches($m.Groups[1].Value, '\{\s*"([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
}
$vsSrc = if (Test-Path $vsPath) { Get-Content $vsPath -Raw } else { "" }
$vsOracle = Get-VerbNames $vsSrc 'kOracleScript'
$vsBattle = Get-VerbNames $vsSrc 'kBattleScript'
$AD_MIN_VERBS = @($vsOracle | Where-Object { $vsBattle -contains $_ }).Count

# DEPTH FLOOR (task #32): the verb count floors breadth but not depth - a
# battle window shortened enough truncates a verb to a passing PREFIX and the
# verb count never moves. The differ already prints its passing-run subtotal;
# assert it. Update the constant from the differ's own "(N in passing verbs)"
# line whenever a verb is added or a registered divergence is fixed - the gate
# goes red in that direction too (a RISE above the floor is fine, >=), so the
# constant cannot silently overstate coverage, only understate it.
$AD_MIN_PASS_RUNS = 18
$adScript = Join-Path $root "tools\anim-diff.ps1"
$kdPath   = Join-Path $root "tools\oracle\known-divergences.json"
# $ErrorActionPreference is Stop for this script; 2>&1 on a child process turns
# its stderr into error records, which would throw instead of being reported.
$prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
$adOut  = & powershell -ExecutionPolicy Bypass -File $adScript -ArenaLog $log 2>&1
$adExit = $LASTEXITCODE
$ErrorActionPreference = $prevEA
$adOut | Out-Host
if (-not (Test-Path $kdPath)) {
    Check "anim timelines match vanilla" $false "no known-divergence register at $kdPath"
} else {
    $kd      = Get-Content $kdPath -Raw | ConvertFrom-Json
    # an EMPTY register is the success state of the stale-exception rule, and
    # @() around an empty property set yields @($null) - Count 1, element null -
    # which used to fail this check with "never compared: " naming nothing.
    $kdNames = @($kd.PSObject.Properties.Name | Where-Object { $_ })
    $adPass  = @($adOut | Select-String '^\[anim-diff\] (\S+) +PASS' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    $adFail  = @($adOut | Select-String '^\[anim-diff\] (\S+) +FAIL' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    $adSum   = $adOut | Select-String '^\[anim-diff\] (\d+) verbs compared' | Select-Object -First 1
    $adCount = if ($adSum) { [int]$adSum.Matches[0].Groups[1].Value } else { -1 }
    $adRuns     = $adOut | Select-String '\((\d+) in passing verbs\)' | Select-Object -First 1
    $adPassRuns = if ($adRuns) { [int]$adRuns.Matches[0].Groups[1].Value } else { -1 }
    $adKnown   = @($adFail | Where-Object { $kdNames -contains $_ })
    $adUnknown = @($adFail | Where-Object { $kdNames -notcontains $_ })
    $adStale   = @($adPass | Where-Object { $kdNames -contains $_ })
    $adAbsent  = @($kdNames | Where-Object { ($adPass -notcontains $_) -and ($adFail -notcontains $_) })
    foreach ($v in $adKnown) { Write-Host ("[oracle-gate] KNOWN-DIVERGENT {0}: {1}" -f $v, $kd.$v) }
    $why = @()
    if ($adUnknown.Count) { $why += "UNREGISTERED anim-diff FAIL: $($adUnknown -join ', ')" }
    if ($adStale.Count)   { $why += "stale exception - $($adStale -join ', ') now PASSES; REMOVE the entry from known-divergences.json" }
    if ($adAbsent.Count)  { $why += "registered verb never compared (marker missing?): $($adAbsent -join ', ')" }
    if ($adPass.Count -lt 1) { $why += "no verb passed at all" }
    if ($AD_MIN_VERBS -lt 1) { $why += "derived 0 shared verbs from verb_script.h - the header parse is broken, fix Get-VerbNames" }
    if ($adCount -lt $AD_MIN_VERBS) { $why += "only $adCount verbs compared, want >= $AD_MIN_VERBS (marker stopped firing, or the wrong log)" }
    if ($adPassRuns -lt $AD_MIN_PASS_RUNS) { $why += "passing verbs cover only $adPassRuns runs, want >= $AD_MIN_PASS_RUNS (a shortened window can pass on a prefix)" }
    if ($adCount -ne ($adPass.Count + $adFail.Count)) { $why += "differ says $adCount verbs, $($adPass.Count + $adFail.Count) PASS/FAIL lines parsed" }
    if ($adExit -ne $adKnown.Count) { $why += "differ exit $adExit != $($adKnown.Count) registered failures" }
    Check "anim timelines match vanilla" ($why.Count -eq 0) `
          $(if ($why.Count) { $why -join '; ' }
            else { "$adCount verbs compared: $($adPass.Count) passed, $($adKnown.Count) known-divergent, 0 unexpected" })
}

# --- check 18 (task #18): the air-set FALL ARC matches the vanilla goldens ----
# The sim's falling bomb, logged per tick by [fallarc] (post-tick), from the
# same mode-12 boot as checks 7-10. First it RIDES the setter's hands - the
# leading rows sit at the golden hand offset (airset_attach_dy) and there are
# exactly airset_attach_samples of them (the release row logs attach=0 while
# still AT the offset, so offset rows are the unit, not attach>0 rows). Then
# it free-falls: the per-tick delta sequence is an arithmetic series whose
# step is the golden gravity (Hero units = sim x120) starting from REST (the
# first delta is exactly one gravity step - airset_release_vy 0). The golden
# fall_frames is NOT asserted: it is scenario-bound (vanilla released from a
# different height); the LAW pins the arc, the duration follows from height.
$fa = @($m12 | Select-String '\[fallarc\] t(\d+) bi=(\d+) y=([-\d.]+) py=([-\d.]+) attach=(\d+)' |
       ForEach-Object { [pscustomobject]@{
           t  = [int]$_.Matches[0].Groups[1].Value
           bi = [int]$_.Matches[0].Groups[2].Value
           y  = [double]$_.Matches[0].Groups[3].Value
           py = [double]$_.Matches[0].Groups[4].Value } })
if ($fa.Count -ge 8) {
    # first episode: contiguous ticks on one bomb index
    $ep = [System.Collections.Generic.List[object]]::new(); $ep.Add($fa[0])
    for ($i = 1; $i -lt $fa.Count; $i++) {
        if ($fa[$i].bi -eq $ep[0].bi -and $fa[$i].t -eq $ep[$ep.Count-1].t + 1) { $ep.Add($fa[$i]) } else { break }
    }
    # riding prefix: rows at the golden hand offset (+-1 Hero)
    $att = 0
    foreach ($r in $ep) {
        if ([math]::Abs(($r.y - $r.py) * 120.0 - $g.airset_attach_dy) -le 1.0) { $att++ } else { break }
    }
    # fall: consecutive y deltas after the riding prefix, in Hero units
    $fd = @(); for ($i = $att; $i -lt $ep.Count; $i++) { $fd += (($ep[$i].y - $ep[$i-1].y) * 120.0) }
    $gravOK = $false; $vyOK = $false; $gFit = $null; $v0 = $null
    if ($fd.Count -ge 4) {
        $g2 = @(); for ($i = 1; $i -lt $fd.Count; $i++) { $g2 += ($fd[$i-1] - $fd[$i]) }
        $gFit = [math]::Round(($g2 | Measure-Object -Average).Average, 3)
        $v0   = [math]::Round($fd[0] + $gFit, 3)
        $gravOK = [math]::Abs($gFit - $g.airset_fall_gravity) -le 0.05
        $vyOK   = [math]::Abs($v0   - $g.airset_release_vy)   -le 0.05
    }
    $attOK = ($att -eq $g.airset_attach_samples)
    Check "airset fall arc == goldens" ($attOK -and $gravOK -and $vyOK) `
          ("attach {0} rows (golden {1}); g={2} (golden {3}); v0={4} (golden {5}); {6} fall rows" -f `
           $att, $g.airset_attach_samples, $gFit, $g.airset_fall_gravity, $v0, $g.airset_release_vy, $fd.Count)
} else { Check "airset fall arc == goldens" $false "no [fallarc] episode in mode-12 log ($($fa.Count) lines)" }

if ($fails) { Write-Host "`n[oracle-gate] FAILED: $($fails -join ', ')"; exit 1 }
Write-Host "`n[oracle-gate] ALL GREEN"; exit 0
