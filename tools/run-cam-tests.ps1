# Host-side camera pose tests (A1.5). The patch runs as MIPS inside the game and
# can't be unit-tested, but the pose constants and math are pure - so the
# assumptions baked into the precomputed trig literals are machine-checked here.
# Wired into build.ps1 as a pre-build gate.
param([string]$Cc = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

# MSYS2 gcc fails SILENTLY (exit 1, no diagnostic) when invoked by absolute path
# without its own bin dir on PATH - it can't load libisl/libmpc. Prepend it.
function Resolve-Cc([string]$explicit) {
    $found = $null
    if ($explicit)          { $found = $explicit }
    elseif ($env:BMHERO_CC) { $found = $env:BMHERO_CC }
    else {
        $onPath = Get-Command gcc -ErrorAction SilentlyContinue
        if ($onPath) { return $onPath.Source }
        foreach ($c in @("C:\msys64\ucrt64\bin\gcc.exe", "C:\msys64\mingw64\bin\gcc.exe")) {
            if (Test-Path $c) { $found = $c; break }
        }
    }
    if (-not $found) { throw "no C compiler found. Pass -Cc <path> or set `$env:BMHERO_CC." }
    $bin = Split-Path $found -Parent
    if ($env:PATH -notlike "*$bin*") { $env:PATH = "$bin;" + $env:PATH }
    return $found
}
$CC  = Resolve-Cc $Cc
$exe = Join-Path ([IO.Path]::GetTempPath()) "test_arena_cam.exe"

& $CC -std=c11 -Wall -Wextra -Werror -O2 -o $exe (Join-Path $root "tools\test_arena_cam.c") -lm
if ($LASTEXITCODE -ne 0) { Write-Host "[cam-tests] BUILD FAILED" -ForegroundColor Red; exit 1 }
& $exe
if ($LASTEXITCODE -ne 0) { Write-Host "[cam-tests] FAILED" -ForegroundColor Red; exit 1 }
Write-Host "[cam-tests] OK" -ForegroundColor Green
exit 0
