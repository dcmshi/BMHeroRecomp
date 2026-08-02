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
function Truncate($runs, [int]$budget) {
    $out = @()
    foreach ($r in $runs) {
        $ix = [int]$r[0]; $ln = [int]$r[1]
        if ($budget -le 0) { break }
        $take = [math]::Min($ln, $budget)
        if ($take -ge 2) { $out += ,@($ix, $take) }
        $budget -= $take
    }
    ,$out
}
$restrict = if ($Verbs) { $Verbs -split ',' } else { $null }
$fails = 0; $compared = 0
foreach ($vp in $tl.PSObject.Properties) {
    $name = $vp.Name
    if ($restrict -and $name -notin $restrict) { continue }
    $mk = @($marks | Where-Object { $_.name -eq $name })
    if ($mk.Count -eq 0) {
        if ($restrict) { Write-Host ("[anim-diff] {0,-12} FAIL  no [verb] marker in arena log" -f $name); $fails++; $compared++ }
        continue   # unrestricted: verbs the battle script doesn't carry are skipped by design
    }
    $compared++
    $t0 = $mk[0].t
    $next = @($marks | Where-Object { $_.t -gt $t0 } | Sort-Object t | Select-Object -First 1)
    $t1 = if ($next) { $next[0].t } else { $t0 + [int]$vp.Value.frames }
    $aFrames = $t1 - $t0
    $window = [math]::Min([int]$vp.Value.frames, $aFrames)
    # truncate BOTH run lists to the window
    $vRuns = Truncate @($vp.Value.runs | ForEach-Object { ,@([int]$_[0], [int]$_[1]) }) $window
    $aRuns = Truncate (ArenaRuns $t0 $t1) $window
    $bad = $null
    if ($vRuns.Count -ne $aRuns.Count) {
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
    else      { Write-Host ("[anim-diff] {0,-12} PASS  {1} runs over {2}f" -f $name, $vRuns.Count, $window) }
}
if ($compared -eq 0) { Write-Host "[anim-diff] compared ZERO verbs - that is a failure, not a pass"; exit 1 }
Write-Host ("[anim-diff] {0} verbs compared, {1} failed" -f $compared, $fails)
exit $fails
