# Boot instrumented VANILLA single-player (ARENA_ORACLE=1), wait for the phase
# script to finish, and distill arena_bridge.log into tools\oracle\goldens.json.
# The goldens are the REFERENCE the arena is gated against (oracle-gate.ps1).
# Spec: bmhero-arena docs/superpowers/specs/2026-08-01-single-player-oracle-design.md
#
# Everything here is MEASURED from the log. The only inputs from outside are the
# phase-marker names and the set button mask, both of which the phase script in
# main.cpp drives - no observed clip index or frame count is written into this
# file, or the gate would only ever assert our own assumption.
param(
    [switch]$Force,          # overwrite goldens that differ (default: show diff, exit 1)
    [int]$TimeoutSec = 240,  # ~45-60s boot+mash, then ~35s of scripted phases
    [string]$FromLog = ""    # parse an existing log instead of booting (parser work)
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe  = Join-Path $root "build-rwdi\BMHeroRecompiled.exe"
$log  = Join-Path $root "arena_bridge.log"
$out  = Join-Path $root "tools\oracle\goldens.json"

# The set button the phase script taps (main.cpp). Not derivable from the log -
# recorded so a future run can tell whether the verb itself changed.
$SET_MASK = "0x0010"

if ($FromLog) {
    if (-not (Test-Path $FromLog)) { Write-Error "no log at $FromLog"; exit 1 }
    $lines = Get-Content $FromLog
} else {
if (-not (Test-Path $exe)) { Write-Error "missing $exe (build first)"; exit 1 }

# -- launch with a CLEAN env (play.ps1 discipline: only ARENA_ORACLE applies) --
$knobs = @("ARENA_SET_ANIM","ARENA_KICK_ANIM","ARENA_POSE_FRAMES","ARENA_POSE_MOVING",
           "ARENA_CAM_DIST","ARENA_CAM_PITCH","ARENA_CAM_YAW","ARENA_CAM_FOLLOW",
           "ARENA_CAM_OFF","ARENA_CAM_ZFAR","ARENA_AUTO_BATTLE","ARENA_ANIM_SWEEP",
           "ARENA_PROBE_AXIS","ARENA_RASTER_N","ARENA_RASTER_STEP","ARENA_ORACLE")
$saved = @{}
foreach ($k in $knobs) { $saved[$k] = [Environment]::GetEnvironmentVariable($k)
                         Remove-Item "Env:\$k" -ErrorAction SilentlyContinue }
try {
    Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800
    # The game truncates arena_bridge.log per process (fopen "w"), so a length
    # delta is unsound and a stale DONE would end the wait early: delete it.
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    $env:ARENA_ORACLE = '1'
    $p = Start-Process $exe -WorkingDirectory $root -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $done = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        if ($p.HasExited) { break }
        if ((Test-Path $log) -and
            (Select-String -Path $log -Pattern '\[oracle\] phase=DONE' -Quiet)) {
            $done = $true; break
        }
    }
    Get-Process BMHeroRecompiled -ErrorAction SilentlyContinue | Stop-Process -Force
    if (-not $done) {
        $last = ""
        if (Test-Path $log) {
            $last = (Select-String -Path $log -Pattern '\[oracle' |
                     Select-Object -Last 3 | ForEach-Object { $_.Line }) -join "`n"
        }
        Write-Error "oracle run did NOT reach DONE. Last oracle lines:`n$last`n(fallback trigger - see spec 5.3)"
        exit 1
    }
    $lines = Get-Content $log
} finally {
    foreach ($k in $knobs) {
        if ($null -ne $saved[$k]) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
        else { Remove-Item "Env:\$k" -ErrorAction SilentlyContinue }
    }
}
}

# ------------------------------ parse ---------------------------------------
function PhaseN([string]$name) {
    $m = $lines | Select-String "\[oracle\] phase=$name n=(\d+)" | Select-Object -First 1
    if (-not $m) { Write-Error "phase marker '$name' missing from log"; exit 1 }
    [int]$m.Matches[0].Groups[1].Value
}
$nWalk = PhaseN 'walk';     $nStand  = PhaseN 'stand'
$nDrop = PhaseN 'dropB';    $nHold   = PhaseN 'holdB'
$nRel  = PhaseN 'releaseB'; $nSet    = PhaseN 'setR'
$nWoff = PhaseN 'walkoff';  $nKick   = PhaseN 'kickrun'
$nJump = PhaseN 'jumpA';    $nAirset = PhaseN 'airsetR'

$anim = $lines | Select-String '\[oracle-anim\] n=(\d+) idx=(\d+) frame=([\d.]+)' |
    ForEach-Object { [pscustomobject]@{ n = [int]$_.Matches[0].Groups[1].Value
                                        idx = [int]$_.Matches[0].Groups[2].Value
                                        fr  = [double]$_.Matches[0].Groups[3].Value } }
$bomb = $lines | Select-String '\[oracle-bomb\] n=(\d+) slot=(\d+) state=(\d+) pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\)' |
    ForEach-Object { [pscustomobject]@{ n = [int]$_.Matches[0].Groups[1].Value
                                        x = [double]$_.Matches[0].Groups[4].Value
                                        y = [double]$_.Matches[0].Groups[5].Value
                                        z = [double]$_.Matches[0].Groups[6].Value } }
$blast = $lines | Select-String '\[oracle-blast\] n=(\d+)' |
    ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
$frameLn = $lines | Select-String '\[oracle\] frame n=(\d+) level=(\d+) player=\d+ floorY=([-\d.]+)' |
    ForEach-Object { [pscustomobject]@{ n = [int]$_.Matches[0].Groups[1].Value
                                        level = [int]$_.Matches[0].Groups[2].Value
                                        floor = [double]$_.Matches[0].Groups[3].Value } }

# BASELINES. Every clip lookup is "the first clip that is not what the player was
# already doing", so each verb needs the idx it interrupts measured from its own
# phase window - the kick lands while RUNNING and the throw while HOLDING, so the
# idle baseline alone would return the locomotion/hold clip instead of the verb.
function DominantIdx([int]$a, [int]$b) {
    $g = $anim | Where-Object { $_.n -ge $a -and $_.n -lt $b } |
         Group-Object idx | Sort-Object Count -Descending | Select-Object -First 1
    if ($g) { [int]$g.Name } else { -1 }
}
$idleIdx = DominantIdx $nStand  $nDrop
$walkIdx = DominantIdx $nWalk   $nStand
$holdIdx = DominantIdx $nHold   $nRel
$jumpIdx = DominantIdx $nJump   $nAirset

# clip after a marker: the first contiguous run of a non-baseline idx within 150 frames
function ClipAfter([int]$n0, [int[]]$excl) {
    $win = @($anim | Where-Object { $_.n -ge $n0 -and $_.n -lt ($n0 + 150) -and
                                    $excl -notcontains $_.idx })
    if (-not $win) { return $null }
    $idx = $win[0].idx; $prev = $win[0].n - 1; $run = 0
    foreach ($a in $win) {
        if ($a.idx -eq $idx -and $a.n -eq $prev + 1) { $run++; $prev = $a.n } else { break }
    }
    [pscustomobject]@{ idx = $idx; frames = $run }
}
$drop   = ClipAfter $nDrop   @($idleIdx)
$throw  = ClipAfter $nRel    @($idleIdx, $holdIdx)
$set    = ClipAfter $nSet    @($idleIdx)
$kick   = ClipAfter $nKick   @($idleIdx, $walkIdx)
$airset = ClipAfter $nAirset @($idleIdx, $jumpIdx)

# bomb rest: the SET bomb's stable Y (>=10 consecutive samples within 0.5), which
# is a true stationary placement; tap-B is a short THROW here, so it only serves
# as the fallback.
function RestY([int]$a, [int]$b) {
    $s = @($bomb | Where-Object { $_.n -ge $a -and $_.n -lt $b })
    for ($i = 0; $i -le $s.Count - 10; $i++) {
        $w = $s[$i..($i+9)].y
        if ((($w | Measure-Object -Maximum).Maximum - ($w | Measure-Object -Minimum).Minimum) -lt 0.5) {
            return ($w | Measure-Object -Average).Average
        }
    }
    return $null
}
$restY = RestY $nSet $nKick
$restFrom = "setR"
if ($null -eq $restY) { $restY = RestY $nDrop $nHold; $restFrom = "dropB" }
$floorY = ($frameLn | Where-Object { $_.n -ge $nSet } | Select-Object -First 1).floor
$restLift = if ($null -ne $restY) { [math]::Round($restY - $floorY, 1) } else { $null }

# throw: impact detonation + flight envelope
$nBlastT = $blast | Where-Object { $_ -gt $nRel } | Select-Object -First 1
$impact = $false; $flight = $null; $arc = $null
if ($nBlastT) {
    $air = @($bomb | Where-Object { $_.n -gt $nRel -and $_.n -lt $nBlastT })
    if ($air.Count -gt 0) {
        $lastAir = $air[-1].n
        $flight  = $nBlastT - $nRel
        $impact  = (($nBlastT - $lastAir) -le 5) -and ($flight -lt 60)
        $arc     = [math]::Round((($air | Measure-Object -Property y -Maximum).Maximum - $air[0].y), 1)
    }
}

# kick: does the bomb SLIDE off (arena behaviour) or detonate on contact?
$nBlastK = $blast | Where-Object { $_ -gt $nKick } | Select-Object -First 1
$kickSlide = $false
$kicked = @($bomb | Where-Object { $_.n -ge $nKick -and ($null -eq $nBlastK -or $_.n -lt $nBlastK) })
if ($kicked.Count -ge 2) {
    $dx = $kicked[-1].x - $kicked[0].x; $dz = $kicked[-1].z - $kicked[0].z
    $kickSlide = ([math]::Sqrt($dx*$dx + $dz*$dz) -gt 100.0)
}

$goldens = [ordered]@{
    set_anim_idx           = if ($set)    { $set.idx }       else { $null }
    set_anim_frames        = if ($set)    { $set.frames }    else { $null }
    set_button_mask        = $SET_MASK
    kick_anim_idx          = if ($kick)   { $kick.idx }      else { $null }
    kick_anim_frames       = if ($kick)   { $kick.frames }   else { $null }
    kick_slide             = $kickSlide
    airset_anim_idx        = if ($airset) { $airset.idx }    else { $null }
    airset_anim_frames     = if ($airset) { $airset.frames } else { $null }
    drop_anim_idx          = if ($drop)   { $drop.idx }      else { $null }
    drop_anim_frames       = if ($drop)   { $drop.frames }   else { $null }
    throw_anim_idx         = if ($throw)  { $throw.idx }     else { $null }
    throw_anim_frames      = if ($throw)  { $throw.frames }  else { $null }
    bomb_rest_lift         = $restLift
    bomb_rest_from         = $restFrom
    throw_impact_detonates = $impact
    throw_flight_frames    = $flight
    throw_arc_peak         = $arc
    no_oracle              = @("camera framing", "explosion look", "fun / overall feel")
    provenance             = [ordered]@{
        fork_commit = (git -C $root rev-parse --short HEAD)
        date        = (Get-Date -Format 'yyyy-MM-dd')
        level       = ($frameLn | Select-Object -First 1).level
    }
}
# refuse silently-degraded goldens: every gated field must have extracted
$core = @('set_anim_idx','kick_anim_idx','bomb_rest_lift','throw_flight_frames')
$missing = $core | Where-Object { $null -eq $goldens[$_] }
if ($missing) { Write-Error "extraction incomplete - null: $($missing -join ', '). NOT writing goldens."; exit 1 }

Write-Host ("baselines: idle={0} walk={1} hold={2} jump={3}" -f $idleIdx, $walkIdx, $holdIdx, $jumpIdx)
$new = $goldens | ConvertTo-Json -Depth 4
if ((Test-Path $out) -and -not $Force) {
    $old = Get-Content $out -Raw
    if ($old.Trim() -ne $new.Trim()) {
        Write-Host "=== goldens DIFFER from checked-in (rerun with -Force to overwrite) ==="
        Write-Host "--- old ---`n$old`n--- new ---`n$new"
        exit 1
    }
    Write-Host "goldens unchanged."; exit 0
}
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
Set-Content -Path $out -Value $new
Write-Host "goldens written to $out`n$new"
