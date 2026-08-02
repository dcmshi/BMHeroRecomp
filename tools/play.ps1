# Launch build-rwdi with EXPLICIT arena knobs - and only those.
#
#   .\tools\play.ps1                          # defaults (no overrides at all)
#   .\tools\play.ps1 -SetAnim 42              # try another set clip
#   .\tools\play.ps1 -KickAnim 33 -PoseFrames 16
#   .\tools\play.ps1 -CamYaw 35 -CamDist 2400 # framing experiments
#   .\tools\play.ps1 -Mode 4                  # ARENA_AUTO_BATTLE probe modes
#
# Every ARENA_* knob NOT passed is CLEARED for the launch, then your shell's
# environment is restored - so a value you exported for an A/B last week can
# never silently ride along (the 2026-07-31 "poses still wrong" trap: stale
# ARENA_SET_ANIM overriding a new default). Add new knobs to $knobs.
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
    [int]$AnimSweep   = [int]::MinValue    # ticks per index; enables the sweep
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
           "ARENA_AIR_THROW_ANIM","ARENA_AIR_THROW_POSE_FRAMES")

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

    $shown = $knobs | Where-Object { Test-Path "Env:\$_" } |
             ForEach-Object { "$_=$([Environment]::GetEnvironmentVariable($_))" }
    Write-Host ("launching {0}" -f $(if ($shown) { $shown -join "  " } else { "with pure defaults (all knobs cleared)" }))
    Start-Process -FilePath $exe -WorkingDirectory $root | Out-Null
}
finally {
    # restore the caller's shell exactly as it was
    foreach ($k in $knobs) {
        if ($null -ne $saved[$k]) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
        else { Remove-Item "Env:\$k" -ErrorAction SilentlyContinue }
    }
}
