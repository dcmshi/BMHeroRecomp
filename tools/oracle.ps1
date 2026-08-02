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
           "ARENA_KICK_POSE_FRAMES","ARENA_HIT_ANIM","ARENA_HIT_POSE_FRAMES",
           "ARENA_AIRSET_ANIM","ARENA_AIRSET_POSE_FRAMES","ARENA_CARRY_IDLE_ANIM",
           "ARENA_CARRY_WALK_ANIM","ARENA_WINDUP_ANIM","ARENA_WINDUP_START",
           "ARENA_JUMP_ANIM","ARENA_THROW_ANIM","ARENA_THROW_POSE_FRAMES",
           "ARENA_WINDUP_WALK_ANIM","ARENA_CARRY_JUMP_ANIM",
           "ARENA_AIR_THROW_ANIM","ARENA_AIR_THROW_POSE_FRAMES",
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
# round 9 segments
$nCarryB = PhaseN 'carryB';   $nCarryW   = PhaseN 'carrywalk'
$nCarryR = PhaseN 'carryrel'; $nHoldLong = PhaseN 'holdlong'
$nWupWalk = PhaseN 'windupwalk'
$nSpread = PhaseN 'spreadrel'
$nSet2   = PhaseN 'setR2';    $nJumpOn   = PhaseN 'jumpon'
# round 11 segments
$nCarryJ = PhaseN 'carryjump'; $nJumpB = PhaseN 'jumpB'
$nRelAir = PhaseN 'relairB'

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
$frameLn = $lines | Select-String '\[oracle\] frame n=(\d+) level=(\d+) player=\d+ floorY=([-\d.]+) playerY=([-\d.]+)' |
    ForEach-Object { [pscustomobject]@{ n = [int]$_.Matches[0].Groups[1].Value
                                        level = [int]$_.Matches[0].Groups[2].Value
                                        floor = [double]$_.Matches[0].Groups[3].Value
                                        py    = [double]$_.Matches[0].Groups[4].Value } }

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

# HIT/tumble clip (round 7): the air-set bomb fuses out at the setter's feet
# (~106 frames after airsetR; the player just stands there) and the blast's
# knockback plays the game's own hit reaction. Key on the first blast after
# airsetR - the kicked bomb's blast came earlier.
$nBlastA = $blast | Where-Object { $_ -gt $nAirset } | Select-Object -First 1
$hit = $null
if ($nBlastA) { $hit = ClipAfter $nBlastA @($idleIdx, $jumpIdx) }

# kick: does the bomb SLIDE off (arena behaviour) or detonate on contact?
# And how FAST (round 8: the arena's kick was slower than running, so the
# kicker caught up and pushed the bomb) - Hero units per frame over the slide.
$nBlastK = $blast | Where-Object { $_ -gt $nKick } | Select-Object -First 1
$kickSlide = $false; $kickSpeed = $null
$kicked = @($bomb | Where-Object { $_.n -ge $nKick -and ($null -eq $nBlastK -or $_.n -lt $nBlastK) })
if ($kicked.Count -ge 2) {
    $dx = $kicked[-1].x - $kicked[0].x; $dz = $kicked[-1].z - $kicked[0].z
    $dist = [math]::Sqrt($dx*$dx + $dz*$dz)
    $kickSlide = ($dist -gt 100.0)
    $dn = $kicked[-1].n - $kicked[0].n
    if ($dn -gt 0) { $kickSpeed = [math]::Round($dist / $dn, 2) }
}

$player = $lines | Select-String '\[oracle-player\] n=(\d+) slot=\d+ state=(\d+) pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\)' |
    ForEach-Object { [pscustomobject]@{ n = [int]$_.Matches[0].Groups[1].Value
                                        st = [int]$_.Matches[0].Groups[2].Value
                                        x = [double]$_.Matches[0].Groups[3].Value
                                        y = [double]$_.Matches[0].Groups[4].Value
                                        z = [double]$_.Matches[0].Groups[5].Value } }

# ---- round 9 extractions ----------------------------------------------------
# CARRY-WALK: the clip while holding B AND moving, and whether its frame counter
# ADVANCES (round 9: the arena froze the feet). Baseline = the carry idle from
# the original stationary hold window (holdIdx).
$cwWin = @($anim | Where-Object { $_.n -ge $nCarryW -and $_.n -lt $nCarryR })
$carryWalkIdx = DominantIdx $nCarryW $nCarryR
$carryWalkAdv = $false
$cwSame = @($cwWin | Where-Object { $_.idx -eq $carryWalkIdx })
if ($cwSame.Count -ge 2) {
    $carryWalkAdv = (($cwSame.fr | Measure-Object -Maximum).Maximum -gt $cwSame[0].fr)
}

# WINDUP: the windmill loop dominates the TAIL of the long stationary hold; its
# first appearance minus the hold start is the start offset (the arena's
# charge-hide must key on the clip, not a timer - overlap frames otherwise).
# Tail = the last third of the ACTUAL window (the n clock ticks once per FRAME,
# half the poll rate - a fixed +240 poll offset overshot the window entirely
# on the first run; measure the instrument). The stationary window ends where
# the round-10 windupwalk segment begins.
$windupIdx = DominantIdx ($nHoldLong + [int](2 * ($nWupWalk - $nHoldLong) / 3)) $nWupWalk
$windupStart = $null
if ($windupIdx -ge 0 -and $windupIdx -ne $holdIdx -and $windupIdx -ne $idleIdx) {
    $first = $anim | Where-Object { $_.n -ge $nHoldLong -and $_.n -lt $nWupWalk -and
                                    $_.idx -eq $windupIdx } | Select-Object -First 1
    if ($first) { $windupStart = $first.n - $nHoldLong }
} else { $windupIdx = $null }

# WINDUP+WALK (round 10): the clip while charged AND stick held - and whether
# vanilla lets the player MOVE at all in that state ([oracle-player] XZ
# displacement over the window). Three possible verdicts: a distinct
# charge-run clip; the windmill with frozen feet (idx == windup); or no
# movement at all (moves=false - the arena, which DOES allow charged movement,
# then has no vanilla clip to copy and needs a design stand-in).
$windupWalkIdx = DominantIdx ($nWupWalk + 10) $nSpread
$wupP = @($player | Where-Object { $_.n -ge ($nWupWalk + 10) -and $_.n -lt $nSpread })
$windupWalkMoves = $false
if ($wupP.Count -ge 2) {
    $dx = $wupP[-1].x - $wupP[0].x; $dz = $wupP[-1].z - $wupP[0].z
    $windupWalkMoves = ([math]::Sqrt($dx*$dx + $dz*$dz) -gt 50.0)
}

# STAND-ON-BOMB: set at the feet, jump straight up, land back on it. The lift is
# the playerY plateau (>=8 consecutive samples within 0.5) between the landing
# and the fuse-out blast, relative to the ground-standing baseline. The XZ gap
# between the player and the bomb over the same window says whether the player
# actually came down ON it ("landed beside it" and "clipped through it" have
# the same playerY).
function XZGap([int]$n0, [int]$n1) {
    # mean player<->bomb XZ distance over [n0,n1) using per-n pairing
    $b = @{}; $bomb | Where-Object { $_.n -ge $n0 -and $_.n -lt $n1 } |
        ForEach-Object { $b[$_.n] = $_ }
    $gaps = @($player | Where-Object { $_.n -ge $n0 -and $_.n -lt $n1 -and $b.ContainsKey($_.n) } |
        ForEach-Object { $p = $_; $q = $b[$_.n]
                         [math]::Sqrt(($p.x-$q.x)*($p.x-$q.x) + ($p.z-$q.z)*($p.z-$q.z)) })
    if ($gaps.Count -gt 0) { [math]::Round(($gaps | Measure-Object -Average).Average, 1) } else { $null }
}
# where does the R-set PLACE the bomb, relative to the setter? (the arena sets
# at the feet - a mismatch here would make every stand-on choreography miss)
$setPlaceOffset = XZGap ($nSet2 + 15) ($nSet2 + 30)
$standBase = ($frameLn | Where-Object { $_.n -ge $nStand -and $_.n -lt $nDrop } |
              Measure-Object -Property py -Average).Average
$nBlastS = $blast | Where-Object { $_ -gt $nSet2 } | Select-Object -First 1
$standLift = $null; $standSupported = $false; $standGap = $null
if ($null -ne $standBase) {
    $sWin = @($frameLn | Where-Object { $_.n -ge ($nJumpOn + 10) -and
                                        ($null -eq $nBlastS -or $_.n -lt $nBlastS) })
    for ($i = 0; $i -le $sWin.Count - 8; $i++) {
        $w = $sWin[$i..($i+7)].py
        if ((($w | Measure-Object -Maximum).Maximum - ($w | Measure-Object -Minimum).Minimum) -lt 0.5) {
            $standLift = [math]::Round((($w | Measure-Object -Average).Average - $standBase), 1)
            $standSupported = ($standLift -gt 10.0)
            $standGap = XZGap $sWin[$i].n ($sWin[$i].n + 16)
            break
        }
    }
}

# ---- round 11 extractions ---------------------------------------------------
# CARRY-JUMP: does vanilla jump at all with B held (playerY rises after jumpB)?
# Which clip rides the carried ascent? And what does the MIDAIR RELEASE do -
# clip, and any vertical impulse (playerY must keep FALLING through a release
# on the descent; a rise is a kick).
$cjWin = @($frameLn | Where-Object { $_.n -gt $nJumpB -and $_.n -lt $nRelAir })
$cjPeak = if ($cjWin.Count) { ($cjWin.py | Measure-Object -Maximum).Maximum } else { 0 }
$carryJumpAllowed = ($null -ne $standBase) -and ($cjPeak - $standBase -gt 100)
$carryJumpIdx = DominantIdx ($nJumpB + 4) $nRelAir
$airThrow = ClipAfter $nRelAir @($idleIdx, $holdIdx, $jumpIdx, $carryJumpIdx)
$relWin = @($frameLn | Where-Object { $_.n -ge $nRelAir -and $_.n -lt ($nRelAir + 12) })
$airThrowYRise = $null
if ($relWin.Count -ge 6) {
    $airThrowYRise = [math]::Round((($relWin.py | Measure-Object -Maximum).Maximum - $relWin[0].py), 1)
}

$goldens = [ordered]@{
    set_anim_idx           = if ($set)    { $set.idx }       else { $null }
    set_anim_frames        = if ($set)    { $set.frames }    else { $null }
    set_button_mask        = $SET_MASK
    kick_anim_idx          = if ($kick)   { $kick.idx }      else { $null }
    kick_anim_frames       = if ($kick)   { $kick.frames }   else { $null }
    kick_slide             = $kickSlide
    kick_slide_speed       = $kickSpeed
    airset_anim_idx        = if ($airset) { $airset.idx }    else { $null }
    airset_anim_frames     = if ($airset) { $airset.frames } else { $null }
    hit_anim_idx           = if ($hit)    { $hit.idx }       else { $null }
    hit_anim_frames        = if ($hit)    { $hit.frames }    else { $null }
    drop_anim_idx          = if ($drop)   { $drop.idx }      else { $null }
    drop_anim_frames       = if ($drop)   { $drop.frames }   else { $null }
    throw_anim_idx         = if ($throw)  { $throw.idx }     else { $null }
    throw_anim_frames      = if ($throw)  { $throw.frames }  else { $null }
    jump_anim_idx          = $jumpIdx
    carry_idle_idx         = $holdIdx
    carry_walk_anim_idx    = $carryWalkIdx
    carry_walk_advances    = $carryWalkAdv
    windup_anim_idx        = $windupIdx
    windup_start_frames    = $windupStart
    windup_walk_anim_idx   = $windupWalkIdx
    windup_walk_moves      = $windupWalkMoves
    carry_jump_allowed     = $carryJumpAllowed
    carry_jump_anim_idx    = $carryJumpIdx
    air_throw_anim_idx     = if ($airThrow) { $airThrow.idx }    else { $null }
    air_throw_anim_frames  = if ($airThrow) { $airThrow.frames } else { $null }
    air_throw_y_rise       = $airThrowYRise
    bomb_stand_lift        = $standLift
    bomb_stand_supported   = $standSupported
    bomb_stand_xz_gap      = $standGap
    set_place_offset       = $setPlaceOffset
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
# The COMPARE ignores `provenance` - fork_commit/date re-stamp on every run, so
# including them makes "unchanged" unreachable and the check red by
# construction. Every MEASURED field is still compared (name AND value, so an
# added/dropped field still fails); the write still carries provenance, so
# -Force refreshes it as before. Values, not bytes: the two PowerShell editions
# serialise the same numbers differently (30 vs 30.0, indent width), which would
# make the check red whenever it ran under a different shell than the writer.
function GoldenFields([string]$json) {
    $o = $json | ConvertFrom-Json
    $o.PSObject.Properties.Remove('provenance')
    ($o.PSObject.Properties | ForEach-Object {
        $v = $_.Value
        if     ($null -eq $v)       { $v = '<null>' }
        elseif ($v -is [bool])      { $v = if ($v) { 'true' } else { 'false' } }
        elseif ($v -is [array])     { $v = ($v -join '|') }
        elseif ($v -is [ValueType]) { $v = ([double]$v).ToString([cultureinfo]::InvariantCulture) }
        "$($_.Name)=$v"
    }) -join "`n"
}
if ((Test-Path $out) -and -not $Force) {
    $old = Get-Content $out -Raw
    if ((GoldenFields $old) -ne (GoldenFields $new)) {
        Write-Host "=== goldens DIFFER from checked-in (rerun with -Force to overwrite) ==="
        Write-Host "--- old ---`n$old`n--- new ---`n$new"
        exit 1
    }
    Write-Host "goldens unchanged."
} else {
    New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
    Set-Content -Path $out -Value $new
    Write-Host "goldens written to $out`n$new"
}

# ---- oracle 2.0: per-verb anim timelines (RLE) -------------------------------
# Verb window = its marker's n to the NEXT marker's n. Runs of 1 frame are
# transition jitter and are dropped. Refuse verbs with zero samples (the
# round-10 vacuous-green lesson: an empty window must fail loudly, not write).
$markers = @($lines | Select-String '\[oracle\] phase=(\S+) n=(\d+)' |
    ForEach-Object { [pscustomobject]@{ name = $_.Matches[0].Groups[1].Value
                                        n    = [int]$_.Matches[0].Groups[2].Value } })
$timelines = [ordered]@{}
for ($i = 0; $i -lt $markers.Count - 1; $i++) {
    $m = $markers[$i]
    if ($m.name -in @('in-level','DONE')) { continue }
    $n0 = $m.n; $n1 = $markers[$i+1].n
    $win = @($anim | Where-Object { $_.n -ge $n0 -and $_.n -lt $n1 })
    if ($win.Count -eq 0) { Write-Error "timeline '$($m.name)' has ZERO anim samples - refusing to write"; exit 1 }
    $runs = @(); $cur = $win[0].idx; $len = 0
    foreach ($a in $win) {
        if ($a.idx -eq $cur) { $len++ }
        else { if ($len -ge 2) { $runs += ,@($cur, $len) }; $cur = $a.idx; $len = 1 }
    }
    if ($len -ge 2) { $runs += ,@($cur, $len) }
    $timelines[$m.name] = [ordered]@{ frames = ($n1 - $n0); runs = $runs }
}
$tlOut = Join-Path $root "tools\oracle\timelines.json"
$tlNew = $timelines | ConvertTo-Json -Depth 5
# Compare VALUES, not bytes: Windows PowerShell and pwsh indent JSON differently
# (and print 30.0 vs 30), so a byte compare goes red whenever the gate runs under
# a different shell than the writer did - red by construction, same trap as the
# provenance re-stamp. -Compress on both sides normalises that away.
function NormJson([string]$json) { ($json | ConvertFrom-Json | ConvertTo-Json -Depth 5 -Compress) }
if ((Test-Path $tlOut) -and -not $Force) {
    $tlOld = Get-Content $tlOut -Raw
    if ((NormJson $tlOld) -ne (NormJson $tlNew)) {
        Write-Host "=== timelines DIFFER from checked-in (rerun with -Force to overwrite) ==="
        Write-Host "--- old ---`n$tlOld`n--- new ---`n$tlNew"; exit 1
    }
    Write-Host "timelines unchanged."
} else { Set-Content -Path $tlOut -Value $tlNew; Write-Host "timelines written to $tlOut" }
