# Summarise / filter arena_bridge.log so fork debugging isn't eyeballing 30k of
# text. The log is rewritten per process, so it always describes the LAST boot.
#
#   tools\arena-log.ps1                 # summary: marker counts + key values
#   tools\arena-log.ps1 -Marker anim    # just [anim] lines
#   tools\arena-log.ps1 -Tail 40        # last 40 lines
#   tools\arena-log.ps1 -Follow         # live tail while the game runs
#   tools\arena-log.ps1 -Grep 'idx=29'  # regex over the whole log
#
# Markers currently emitted by the bridge/render patch:
#   [capture] origin + reference sim pos (the draw-gate handshake)
#   [simpos]  sim -> Hero coordinate samples
#   [bombs]   live bomb bookkeeping
#   [anim]    player animation index + frame (the A1.4 gate reads this)
#   [dbg]     arena_dbg_u32 markers
#   [arena]   bridge lifecycle
param(
    [string] $Marker = "",
    [string] $Grep   = "",
    [int]    $Tail   = 0,
    [switch] $Follow,
    [string] $Path   = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$log  = if ($Path) { $Path } else { Join-Path $root "arena_bridge.log" }

if (-not (Test-Path $log)) {
    Write-Host "no log at $log - has the game run yet?" -ForegroundColor Yellow
    exit 1
}

$age = (Get-Date) - (Get-Item $log).LastWriteTime
Write-Host ("log: {0}  ({1:N0} lines, last written {2:N0}s ago)" -f `
    $log, (Get-Content $log | Measure-Object -Line).Lines, $age.TotalSeconds)

if ($Follow) { Write-Host "(following - Ctrl+C to stop)`n"; Get-Content $log -Wait -Tail 20; exit 0 }
if ($Grep)   { Write-Host ""; Select-String -Path $log -Pattern $Grep | ForEach-Object { $_.Line }; exit 0 }
if ($Marker) { Write-Host ""; Select-String -Path $log -Pattern "^\[$Marker\]" | ForEach-Object { $_.Line }; exit 0 }
if ($Tail -gt 0) { Write-Host ""; Get-Content $log -Tail $Tail; exit 0 }

# --- default: summary ---
$lines = Get-Content $log
Write-Host "`nmarkers:"
$lines | Select-String -Pattern '^\[([a-z]+)\]' | ForEach-Object { $_.Matches[0].Groups[1].Value } |
    Group-Object | Sort-Object Count -Descending |
    ForEach-Object { Write-Host ("  {0,-10} {1,5}" -f $_.Name, $_.Count) }

# The [capture] line is the draw-gate handshake: without it the render bridge
# never armed, which is what arena-soak.ps1 treats as the PASS condition.
$cap = $lines | Select-String -Pattern '^\[capture\]' | Select-Object -First 1
Write-Host "`ndraw gate:"
if ($cap) { Write-Host "  $($cap.Line)" -ForegroundColor Green }
else      { Write-Host "  NO [capture] - the bridge never armed (soak would call this HANG)" -ForegroundColor Red }

# Animation: which indices played, and did the frame counter advance? A pose that
# gets set but never advances means the game's walker stomped it (the A1.4
# hold-while-moving problem).
$anim = $lines | Select-String -Pattern '\[anim\] idx=(\d+) frame=(\d+)'
if ($anim) {
    Write-Host "`nanimation:"
    $anim | ForEach-Object { [pscustomobject]@{
            idx   = [int]$_.Matches[0].Groups[1].Value
            frame = [int]$_.Matches[0].Groups[2].Value } } |
        Group-Object idx | ForEach-Object {
            $f = @($_.Group | ForEach-Object { $_.frame })
            $advanced = ($f.Count -ge 2) -and ($f[-1] -gt $f[0])
            $note = if ($advanced) { "advanced (played)" } else { "STATIC (set but never advanced)" }
            $col  = if ($advanced) { "Green" } else { "Yellow" }
            Write-Host ("  idx {0,-4} samples={1,-4} frames=[{2}]  {3}" -f `
                $_.Name, $f.Count, ($f -join ','), $note) -ForegroundColor $col
        }
}

foreach ($m in @('simpos','bombs')) {
    $rows = $lines | Select-String -Pattern "^\[$m\]"
    if ($rows) {
        Write-Host "`n$($m):"
        $rows | Select-Object -Last 3 | ForEach-Object { Write-Host "  $($_.Line)" }
        if ($rows.Count -gt 3) { Write-Host "  ... $($rows.Count) total (-Marker $m for all)" }
    }
}

# Select-String is case-insensitive by default; this is a regex alternation, so
# it must NOT be -SimpleMatch.
$errs = $lines | Select-String -Pattern 'error|fail|assert|abort'
if ($errs) {
    Write-Host "`npossible errors:" -ForegroundColor Red
    $errs | Select-Object -First 10 | ForEach-Object { Write-Host "  $($_.Line)" }
}
