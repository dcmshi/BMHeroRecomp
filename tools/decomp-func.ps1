# Readable, TYPED C for any undecompiled game function.
#
#   tools\decomp-func.ps1 func_80281E50          # the walker turn fn
#   tools\decomp-func.ps1 func_80024744          # the per-frame update
#   tools\decomp-func.ps1 func_80281E50 -Raw     # show the raw asm instead
#   tools\decomp-func.ps1 -Search camera         # find candidate symbols
#
# WHY THIS EXISTS
# The recomp's RecompiledFuncs/ holds machine-C for the un-migrated overlays, and
# reading it by hand was the A1.3/A1.4 methodology (CLAUDE.md). It works, but it
# is deliberately unidiomatic - no types, no struct fields, everything is
# *(f32*)(a0 + 0x54). m2c does cross-function type inference against the decomp's
# OWN headers, so you get gPlayerObject->moveAngle instead.
#
# Validated against ground truth: func_80281E50 comes back with `sp1C = 4.0f`,
# matching the 0x40800000 turn step that was previously derived by hand.
#
# PIPELINE (all of it cached; first run is slow, later runs are seconds)
#   splat (lib/bmhero/splat.yaml)  -> asm/nonmatchings/**/func_*.s
#   tools/m2ctx                    -> ctx.c   (types from the decomp's headers)
#   m2c --context                  -> typed C
param(
    [Parameter(Position = 0)] [string] $Func = "",
    [string] $Search = "",
    [switch] $Raw,
    [switch] $Refresh          # force asm + context regeneration
)
$ErrorActionPreference = "Stop"

$root    = Split-Path $PSScriptRoot -Parent
$decomp  = Join-Path $root "lib\bmhero"
$tools   = "C:\Users\dshi\GitRepos\.tools"
$venvPy  = Join-Path $tools "decomp-venv\Scripts\python.exe"
$m2c     = Join-Path $tools "m2c\m2c.py"
$cache   = Join-Path $tools "decomp-cache"
$ctxSafe = Join-Path $cache "ctx_m2c.c"

function Fail($m) { Write-Host "`nERROR: $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $venvPy)) {
    Fail @"
decomp venv missing at $venvPy

Set it up once:
  python -m venv $tools\decomp-venv
  & $tools\decomp-venv\Scripts\python.exe -m pip install -r $decomp\requirements.txt
  & $tools\decomp-venv\Scripts\python.exe -m pip install crunch64 pygfxd colorama pylibyaml
"@
}
if (-not (Test-Path $m2c)) {
    Fail "m2c missing at $m2c. Clone it: git clone https://github.com/matt-kempster/m2c.git $tools\m2c"
}
New-Item -ItemType Directory -Force -Path $cache | Out-Null

# --- 1. asm (splat) ----------------------------------------------------------
$asmDir = Join-Path $decomp "asm\nonmatchings"
if ($Refresh -or -not (Test-Path $asmDir)) {
    $baserom = Join-Path $decomp "baserom.z64"
    if (-not (Test-Path $baserom)) {
        $src = Join-Path $root "bmhero.z64"
        if (-not (Test-Path $src)) { Fail "no ROM at $src to stage as baserom.z64" }
        Copy-Item $src $baserom
        Write-Host "staged baserom.z64"
    }
    Write-Host "running splat (one-off, ~1-2 min)..." -ForegroundColor Cyan
    Push-Location $decomp
    try {
        & $venvPy tools/n64splat/split.py splat.yaml 2>&1 | Select-Object -Last 2
        if ($LASTEXITCODE -ne 0) { Fail "splat failed" }
    } finally { Pop-Location }
}

# --- 2. search mode ----------------------------------------------------------
if ($Search) {
    Write-Host "symbols matching '$Search':`n"
    Get-ChildItem $asmDir -Recurse -Filter "*.s" |
        Where-Object { $_.Name -match $Search } |
        Select-Object -First 40 |
        ForEach-Object { "  {0}" -f ($_.Name -replace '\.s$','') }
    Write-Host "`n(also try: grep the decomp's tools/symbol_addrs.txt for named symbols)"
    exit 0
}
if (-not $Func) { Fail "give a function name, or -Search <pattern>. See the header for examples." }

# --- 3. context (m2ctx) ------------------------------------------------------
$ctxRaw = Join-Path $decomp "ctx.c"
if ($Refresh -or -not (Test-Path $ctxSafe)) {
    if ($Refresh -or -not (Test-Path $ctxRaw)) {
        Write-Host "generating m2c context..." -ForegroundColor Cyan
        Push-Location $decomp
        try {
            # m2ctx shells out to cpp - MSYS2 must be on PATH
            $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH
            & $venvPy tools/m2ctx src/overlays/128D20/128D20.c | Out-Null
            if (-not (Test-Path $ctxRaw)) { Fail "m2ctx produced no ctx.c" }
        } finally { Pop-Location }
    }
    # m2c's C parser can't constant-fold these three; values folded by hand.
    # OS_CLOCK_RATE*3/4 = 46875000 ; 0x24-sizeof(f32)*3 = 0x18 ; 0xA4-0x24-sizeof(s32)*3 = 0x74
    (Get-Content $ctxRaw -Raw).
        Replace('#define OS_CPU_COUNTER (OS_CLOCK_RATE*3/4)', '#define OS_CPU_COUNTER 46875000').
        Replace('char padding_3[0x24 - (sizeof(f32) * 3)];', 'char padding_3[0x18];').
        Replace('char padding_1[0xA4 - 0x24 - (sizeof(s32) * 3)];', 'char padding_1[0x74];') |
        Set-Content $ctxSafe -NoNewline
    Write-Host "context ready ($((Get-Content $ctxSafe).Count) lines)"
}

# --- 4. locate the function's asm -------------------------------------------
# splat suffixes overlay functions with their segment (func_80281E50_code_extra_0),
# so match on the bare name as a prefix.
$hits = @(Get-ChildItem $asmDir -Recurse -Filter "$Func*.s")
if ($hits.Count -eq 0) { Fail "no asm found for '$Func'. Try: tools\decomp-func.ps1 -Search $Func" }
if ($hits.Count -gt 1) {
    Write-Host "multiple matches - pick one:" -ForegroundColor Yellow
    $hits | ForEach-Object { "  {0}" -f ($_.Name -replace '\.s$','') }
    exit 1
}
$asm  = $hits[0].FullName
$name = $hits[0].Name -replace '\.s$',''

if ($Raw) { Write-Host "--- $name (raw asm) ---`n"; Get-Content $asm; exit 0 }

# --- 5. decompile ------------------------------------------------------------
Write-Host "--- $name ---`n" -ForegroundColor Cyan
& $venvPy $m2c --context $ctxSafe -f $name $asm
exit $LASTEXITCODE
