# Launch build-rwdi with EXPLICIT arena knobs - and only those.
#
#   .\tools\play.ps1                          # defaults (no overrides at all)
#   .\tools\play.ps1 -SetAnim 42              # try another set clip
#   .\tools\play.ps1 -KickAnim 33 -PoseFrames 16
#   .\tools\play.ps1 -CamYaw 35 -CamDist 2400 # framing experiments
#   .\tools\play.ps1 -Mode 4                  # ARENA_AUTO_BATTLE probe modes
#   .\tools\play.ps1 -NoPuppetMesh            # puppets back to bomb placeholders
#   .\tools\play.ps1 -NoPuppetAnim            # puppets hold the spawn idle bind
#
# Every ARENA_* knob NOT passed is CLEARED for the launch, then your shell's
# environment is restored - so a value you exported for an A/B last week can
# never silently ride along (the 2026-07-31 "poses still wrong" trap: stale
# ARENA_SET_ANIM overriding a new default). Add new knobs to $knobs.
#
# Corollary discovered 2026-08-05: a knob in $knobs with no switch of its own is
# UNSETTABLE through this script. ARENA_PUPPET_MESH was in that state, so the
# 08-04 handoff's own "ARENA_PUPPET_MESH=0 A/Bs the bombers" instruction quietly
# did nothing when run via play.ps1. Both puppet A/B knobs now have switches.
# ARENA_PUPPET_BOT is deliberately NOT in $knobs: its effect (a puppet running a
# canned cycle) is unmissable on screen, so a stale value cannot mislead anyone
# the way a stale pose index did - and leaving it out keeps the documented
# `$env:ARENA_PUPPET_BOT='1'; .\tools\play.ps1` form working.
param(
    [int]$SetAnim     = [int]::MinValue,   # -1..63; -1 = no pose
    [int]$KickAnim    = [int]::MinValue,   # -1..63; -1 = no pose (default)
    [int]$PoseFrames  = [int]::MinValue,   # pose window length (default 10)
    [switch]$PoseMoving,                   # play the set pose even while running
    [float]$CamDist   = [float]::NaN,
    [float]$CamPitch  = [float]::NaN,
    [float]$CamYaw    = [float]::NaN,
    [switch]$CamFollow,
    [switch]$CamOff,
    [int]$Mode        = [int]::MinValue,   # ARENA_AUTO_BATTLE
    [int]$AnimSweep   = [int]::MinValue,   # ticks per index; enables the sweep
    [switch]$NoPuppetMesh,                 # ARENA_PUPPET_MESH=0: bomb placeholders
    [switch]$NoPuppetAnim                  # ARENA_PUPPET_ANIM=0: puppets hold idle
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe  = Join-Path $root "build-rwdi\BMHeroRecompiled.exe"
if (-not (Test-Path $exe)) { Write-Error "missing $exe (build first: .\build.ps1 -Config rwdi)"; exit 1 }

$knobs = @("ARENA_SET_ANIM","ARENA_KICK_ANIM","ARENA_POSE_FRAMES","ARENA_POSE_MOVING",
           "ARENA_CAM_DIST","ARENA_CAM_PITCH","ARENA_CAM_YAW","ARENA_CAM_FOLLOW",
           "ARENA_CAM_OFF","ARENA_CAM_ZFAR","ARENA_AUTO_BATTLE","ARENA_ANIM_SWEEP",
           "ARENA_PROBE_AXIS","ARENA_RASTER_N","ARENA_RASTER_STEP","ARENA_ORACLE",
           "ARENA_KICK_POSE_FRAMES","ARENA_HIT_ANIM","ARENA_HIT_POSE_FRAMES",
           "ARENA_AIRSET_ANIM","ARENA_AIRSET_POSE_FRAMES","ARENA_CARRY_IDLE_ANIM",
           "ARENA_CARRY_WALK_ANIM","ARENA_WINDUP_ANIM","ARENA_WINDUP_START",
           "ARENA_JUMP_ANIM","ARENA_THROW_ANIM","ARENA_THROW_POSE_FRAMES",
           "ARENA_WINDUP_WALK_ANIM","ARENA_CARRY_JUMP_ANIM",
           "ARENA_AIR_THROW_ANIM","ARENA_AIR_THROW_POSE_FRAMES",
           "ARENA_PUSH_ENTRY","ARENA_PUPPET_ANIM","ARENA_IDLE_ANIM","ARENA_FALL_ANIM",
           "ARENA_JUMP_DRIVER","ARENA_AIRTOSS_RECOVER_ANIM",
           "ARENA_AIRTOSS_RECOVER_FRAMES","ARENA_LAND_ANIM","ARENA_LAND_POSE_FRAMES",
           "ARENA_WINDUP_TRANS_ANIM","ARENA_WINDUP_TRANS_FRAMES",
           "ARENA_PUPPET_MESH")

# snapshot + clear, so ONLY the flags below apply to the child
$saved = @{}
foreach ($k in $knobs) {
    $saved[$k] = [Environment]::GetEnvironmentVariable($k)
    Remove-Item "Env:\$k" -ErrorAction SilentlyContinue
}
try {
    if ($SetAnim    -ne [int]::MinValue) { $env:ARENA_SET_ANIM    = "$SetAnim" }
    if ($KickAnim   -ne [int]::MinValue) { $env:ARENA_KICK_ANIM   = "$KickAnim" }
    if ($PoseFrames -ne [int]::MinValue) { $env:ARENA_POSE_FRAMES = "$PoseFrames" }
    if ($PoseMoving)                     { $env:ARENA_POSE_MOVING = "1" }
    if (-not [float]::IsNaN($CamDist))   { $env:ARENA_CAM_DIST    = "$CamDist" }
    if (-not [float]::IsNaN($CamPitch))  { $env:ARENA_CAM_PITCH   = "$CamPitch" }
    if (-not [float]::IsNaN($CamYaw))    { $env:ARENA_CAM_YAW     = "$CamYaw" }
    if ($CamFollow)                      { $env:ARENA_CAM_FOLLOW  = "1" }
    if ($CamOff)                         { $env:ARENA_CAM_OFF     = "1" }
    if ($Mode       -ne [int]::MinValue) { $env:ARENA_AUTO_BATTLE = "$Mode" }
    if ($AnimSweep  -ne [int]::MinValue) { $env:ARENA_ANIM_SWEEP  = "$AnimSweep" }
    if ($NoPuppetMesh)                   { $env:ARENA_PUPPET_MESH = "0" }
    if ($NoPuppetAnim)                   { $env:ARENA_PUPPET_ANIM = "0" }

    $shown = $knobs | Where-Object { Test-Path "Env:\$_" } |
             ForEach-Object { "$_=$([Environment]::GetEnvironmentVariable($_))" }
    Write-Host ("launching {0}" -f $(if ($shown) { $shown -join "  " } else { "with pure defaults (all knobs cleared)" }))
    # ...and say so when an ARENA_* var outside $knobs is riding along. Without
    # this the line above can honestly report "pure defaults" while e.g.
    # ARENA_PUPPET_BOT=1 drives a puppet around the arena (2026-08-05).
    $ride = Get-ChildItem Env: | Where-Object { $_.Name -like 'ARENA_*' -and $knobs -notcontains $_.Name } |
            ForEach-Object { "$($_.Name)=$($_.Value)" }
    if ($ride) { Write-Host ("  NOTE riding along from your shell (not a play.ps1 switch): {0}" -f ($ride -join "  ")) }
    Start-Process -FilePath $exe -WorkingDirectory $root | Out-Null
}
finally {
    # restore the caller's shell exactly as it was
    foreach ($k in $knobs) {
        if ($null -ne $saved[$k]) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
        else { Remove-Item "Env:\$k" -ErrorAction SilentlyContinue }
    }
}
