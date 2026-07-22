# A1.2f boot-soak smoke harness. Launches the RWDI exe N times with
# ARENA_AUTO_BATTLE=1 (auto-Battle + synthetic frontend mash — see main.cpp),
# waits for the in-arena [capture] marker in arena_bridge.log, and classifies
# each boot PASS / CRASH (process died; names the new dump) / HANG (timeout,
# process alive). Exit code = number of failures.
#
#   powershell -ExecutionPolicy Bypass -File tools\arena-soak.ps1 -N 10
param([int]$N = 10, [int]$TimeoutSec = 75, [switch]$Probe)
# -Probe: single run in ARENA_AUTO_BATTLE=3 (in-level stick-up + hold-L
# injection for the camera forensics); dwells 8s after PASS so the injected
# samples land in the log before the kill.
if ($Probe) { $N = 1 }

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

    if ($Probe) { $env:ARENA_AUTO_BATTLE = "3" } else { $env:ARENA_AUTO_BATTLE = "1" }
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
    if ($Probe -and $verdict -eq "PASS") { Start-Sleep -Seconds 8 }   # let the injection sample
    Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Stop-Process -Force
    $results += [pscustomobject]@{ Iter = $i; Verdict = $verdict; Seconds = $secs; Detail = $detail }
    Write-Host ("iter {0}: {1} ({2}s) {3}" -f $i, $verdict, $secs, $detail)
}
Remove-Item Env:\ARENA_AUTO_BATTLE -ErrorAction SilentlyContinue
$results | Format-Table -AutoSize
$fails = @($results | Where-Object Verdict -ne "PASS").Count
Write-Host ("SUMMARY: {0}/{1} PASS" -f ($N - $fails), $N)
exit $fails
