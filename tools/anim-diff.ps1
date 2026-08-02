# Compare the arena's per-verb anim timelines against the vanilla goldens
# (tools\oracle\timelines.json). Verbs compared = the intersection of names.
# A verb in the timelines whose [verb] marker is MISSING from the arena log
# is a FAIL (probe died early / marker never fired), not a skip.
param(
    [Parameter(Mandatory=$true)][string]$ArenaLog,
    [string]$Timelines = "",
    [string]$Verbs = ""          # optional comma list to restrict
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Timelines) { $Timelines = Join-Path $root "tools\oracle\timelines.json" }
if (-not (Test-Path $Timelines)) { Write-Error "no timelines at $Timelines"; exit 1 }
if (-not (Test-Path $ArenaLog))  { Write-Error "no arena log at $ArenaLog"; exit 1 }
$tl = Get-Content $Timelines -Raw | ConvertFrom-Json
$lines = Get-Content $ArenaLog

# Reconstruct the arena's per-tick anim stream from [animrun] (idx held for
# len frames ENDING at t), then slice per [verb] marker and re-RLE. Building
# the flat stream first makes runs straddling verb boundaries split correctly.
$stream = @{}   # tick -> idx
foreach ($m in @($lines | Select-String '\[animrun\] idx=(\d+) len=(\d+) t(\d+)')) {
    $ix=[int]$m.Matches[0].Groups[1].Value; $ln=[int]$m.Matches[0].Groups[2].Value
    $t =[int]$m.Matches[0].Groups[3].Value
    for ($k = $t - $ln; $k -lt $t; $k++) { $stream[$k] = $ix }
}
# The producer never flushes the run that is still open at process exit, so the
# stream ends before the log does. The LAST verb has no next marker to bound its
# window and would otherwise be measured against vanilla frames that were never
# observed here (measured: jumpon's ticks 695-713 are simply absent).
$lastTick = if ($stream.Count) { ($stream.Keys | Measure-Object -Maximum).Maximum } else { -1 }
$marks = @($lines | Select-String '\[verb\] (\S+) t(\d+)' |
    ForEach-Object { [pscustomobject]@{ name = $_.Matches[0].Groups[1].Value
                                        t    = [int]$_.Matches[0].Groups[2].Value } })
function ArenaRuns([int]$t0, [int]$t1) {
    $runs = @(); $cur = $null; $len = 0
    for ($t = $t0; $t -lt $t1; $t++) {
        if (-not $stream.ContainsKey($t)) { continue }
        $ix = $stream[$t]
        if ($null -ne $cur -and $ix -eq $cur) { $len++ }
        else { if ($null -ne $cur -and $len -ge 2) { $runs += ,@($cur,$len) }
               $cur = $ix; $len = 1 }
    }
    if ($null -ne $cur -and $len -ge 2) { $runs += ,@($cur,$len) }
    ,$runs
}
# Truncate a run list to a frame budget. A run the budget cuts down to a single
# frame is a boundary sliver, not a run: STOP there. Emitting nothing while
# still spending the budget (the old behaviour) turned a 1-frame difference at
# the trailing edge into a run-count mismatch - the same failure the leading
# edge had.
function Truncate($runs, [int]$budget) {
    $out = @()
    foreach ($r in $runs) {
        $ix = [int]$r[0]; $ln = [int]$r[1]
        if ($budget -le 0) { break }
        $take = [math]::Min($ln, $budget)
        if ($take -lt 2) { break }
        $out += ,@($ix, $take)
        $budget -= $take
    }
    ,$out
}
# The clip that was already playing when the marker fires bleeds PAST it: two
# frames on the vanilla side (the game's own input->anim latency) but only one
# on the arena side (the bridge drives the pose directly). The >=2 jitter filter
# keeps the vanilla residue and drops the arena one, so a 1-FRAME phase
# difference became a run-count mismatch on every verb. Drop that carried-over
# run from whichever side still carries it. Each side knows its own pre-verb
# clip: vanilla's is the previous window's last run (timelines.json preserves
# verb order), the arena's is the stream tick before the marker. A leading run
# that is NOT the carried clip is real content and stays.
#
# LENGTH CAP: residue is short. Measured over every legitimate drop on the
# mode-13 probe the maximum is 5 ticks (the arena's stick -> locomotion-clip
# latency on carrywalk/windupwalk; the vanilla side's is 2-3). Without a cap the
# rule deletes real content the moment a verb's own clip happens to equal the
# previous one's - e.g. a kickrun marker would silently drop vanilla's [3,16],
# sixteen frames of run-up. The cap is the measured maximum with NO headroom: a
# longer residue should fail loudly, not quietly disappear.
$CARRY_MAX = 5
function DropCarried($runs, $carried) {
    if ($null -ne $carried -and $runs.Count -gt 0 -and [int]$runs[0][0] -eq [int]$carried -and
        [int]$runs[0][1] -le $CARRY_MAX) {
        ,@($runs | Select-Object -Skip 1)
    } else { ,$runs }
}
function RunSum($runs) { $s = 0; foreach ($r in $runs) { $s += [int]$r[1] }; $s }
function ObservedTicks([int]$t0, [int]$t1) {
    $n = 0
    for ($t = $t0; $t -lt $t1; $t++) { if ($stream.ContainsKey($t)) { $n++ } }
    $n
}

# -Verbs is a gate's contract, so a name that matches no timeline is a usage
# ERROR, not a silent skip (it would quietly compare fewer verbs than asked).
# Tokens are trimmed: "a, b" and a trailing space used to vanish.
$restrict = $null
if ($Verbs) {
    $restrict = @($Verbs -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
    if ($restrict.Count -eq 0) {
        Write-Host "[anim-diff] -Verbs '$Verbs' is empty after trimming"; exit 1
    }
    $known = @($tl.PSObject.Properties.Name)
    $unknown = @($restrict | Where-Object { $known -notcontains $_ })
    if ($unknown.Count -gt 0) {
        Write-Host ("[anim-diff] -Verbs names no timeline in {0}: {1}" -f $Timelines, ($unknown -join ', '))
        Write-Host ("[anim-diff] known verbs: {0}" -f ($known -join ', ')); exit 1
    }
}
$fails = 0; $compared = 0; $totalRuns = 0; $passedRuns = 0
$prevVanillaIdx = $null
foreach ($vp in $tl.PSObject.Properties) {
    $name = $vp.Name
    # tracked for EVERY verb, compared or not - the carried clip comes from the
    # window that physically precedes this one, which may not be in scope.
    $vAll = @($vp.Value.runs | ForEach-Object { ,@([int]$_[0], [int]$_[1]) })
    $vCarried = $prevVanillaIdx
    if ($vAll.Count -gt 0) { $prevVanillaIdx = [int]$vAll[-1][0] }
    if ($restrict -and $name -notin $restrict) { continue }
    $mk = @($marks | Where-Object { $_.name -eq $name })
    if ($mk.Count -eq 0) {
        if ($restrict) { Write-Host ("[anim-diff] {0,-12} FAIL  no [verb] marker in arena log" -f $name); $fails++; $compared++ }
        continue   # unrestricted: verbs the battle script doesn't carry are skipped by design
    }
    $compared++
    $t0 = $mk[0].t
    $next = @($marks | Where-Object { $_.t -gt $t0 } | Sort-Object t | Select-Object -First 1)
    $t1 = if ($next) { $next[0].t } else { [math]::Min($t0 + [int]$vp.Value.frames, $lastTick + 1) }
    $aAll = ArenaRuns $t0 $t1
    $aCarried = if ($stream.ContainsKey($t0 - 1)) { $stream[$t0 - 1] } else { $null }
    # If the vanilla carried clip is unknown (the first window in the file has
    # no predecessor) the arena must not drop either - a one-sided drop is the
    # exact asymmetry this rule exists to remove.
    if ($null -eq $vCarried) { $aCarried = $null }
    $vKeep = DropCarried $vAll $vCarried
    $aKeep = DropCarried $aAll $aCarried
    # compare as much of the two streams as BOTH actually carry
    $window = [math]::Min((RunSum $vKeep), (RunSum $aKeep))
    $vRuns = Truncate $vKeep $window
    $aRuns = Truncate $aKeep $window
    $totalRuns += $vRuns.Count
    $bad = $null
    # A SHORT arena stream must FAIL, not silently shrink the comparison: the
    # window budget comes from the observed runs, so a probe that died 20 ticks
    # into a 100-tick verb would otherwise "pass" over those 20. The header
    # promises a dead probe is a failure. (For the last verb the window is
    # already bounded by the last observed tick, so this costs nothing there.)
    $observed = ObservedTicks $t0 $t1
    if ($observed -lt ($t1 - $t0)) {
        $bad = "arena stream incomplete - $observed of $($t1 - $t0) ticks observed in t$t0..t$t1"
    } elseif ($vRuns.Count -eq 0 -or $aRuns.Count -eq 0) {
        # a window that truncates to nothing must not print PASS: "0 runs over
        # 0f" is the vacuous green this whole tool exists to prevent. A window
        # that is merely SHORT but has runs on both sides compares normally.
        $bad = "collapsed window ${window}f - nothing to compare (vanilla $($vRuns.Count) runs, arena $($aRuns.Count) runs)"
    } elseif ($vRuns.Count -ne $aRuns.Count) {
        $bad = "run count vanilla=$($vRuns.Count) arena=$($aRuns.Count)"
    } else {
        for ($k = 0; $k -lt $vRuns.Count; $k++) {
            if ($vRuns[$k][0] -ne $aRuns[$k][0] -or
                [math]::Abs($vRuns[$k][1] - $aRuns[$k][1]) -gt 3) {
                $bad = "run $k vanilla ($($vRuns[$k] -join ',')) vs arena ($($aRuns[$k] -join ','))"
                break
            }
        }
    }
    if ($bad) { Write-Host ("[anim-diff] {0,-12} FAIL  {1}" -f $name, $bad); $fails++ }
    else      { Write-Host ("[anim-diff] {0,-12} PASS  {1} runs over {2}f" -f $name, $vRuns.Count, $window)
                $passedRuns += $vRuns.Count }
}
if ($compared -eq 0) { Write-Host "[anim-diff] compared ZERO verbs - that is a failure, not a pass"; exit 1 }
# both totals: $totalRuns counts every verb reached, $passedRuns only the ones
# that actually agreed - a gate wanting "N runs really matched" needs the latter.
Write-Host ("[anim-diff] {0} verbs compared, {1} runs ({2} in passing verbs), {3} failed" -f $compared, $totalRuns, $passedRuns, $fails)
exit $fails
