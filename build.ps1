# One-command build for the recomp fork. Composes the three-toolchain PATH,
# rebuilds patches/ when stale, builds, and optionally soaks.
#
#   .\build.ps1                     # rwdi build, auto patch rebuild
#   .\build.ps1 -Soak 5             # ...then 5 boot-soak iterations
#   .\build.ps1 -Config cmake       # the plain Release build dir
#   .\build.ps1 -Patches always     # force the patches rebuild
#   .\build.ps1 -Soak 5 -AnimProbe  # build, soak, then run the A1.4 anim gate
#
# PATH ORDER IS LOAD-BEARING (see CLAUDE.md build recipe):
#   LLVM15 first  - patches/ needs clang/ld.lld v15; VS's clang has no MIPS
#                   backend and MSYS2's LLVM-22 lld rejects the old flags.
#   VS dev shell  - MSVC link.exe must precede msys.
#   MSYS2 last    - make from usr/bin, plus the N64Recomp runtime DLLs.
param(
    [ValidateSet("rwdi","cmake")]          [string] $Config  = "rwdi",
    [ValidateSet("auto","always","never")] [string] $Patches = "auto",
    [int]    $Soak = 0,
    [switch] $AnimProbe,
    [string] $Llvm15 = $(if ($env:BMHERO_LLVM15) { $env:BMHERO_LLVM15 } else { "C:\Users\dshi\GitRepos\.tools\llvm15" }),
    [string] $Msys   = $(if ($env:BMHERO_MSYS)   { $env:BMHERO_MSYS   } else { "C:\msys64" })
)
$ErrorActionPreference = "Stop"
$root     = $PSScriptRoot
$buildDir = Join-Path $root $(if ($Config -eq "rwdi") { "build-rwdi" } else { "build-cmake" })

function Fail($msg) { Write-Host "`nBUILD FAILED: $msg" -ForegroundColor Red; exit 1 }

# Validate argument combinations BEFORE spending a build on them.
if (($Soak -gt 0 -or $AnimProbe) -and $Config -ne "rwdi") {
    Fail "-Soak/-AnimProbe require -Config rwdi (arena-soak.ps1 launches build-rwdi)"
}

# --- 1. toolchains -----------------------------------------------------------
if (-not (Test-Path (Join-Path $Llvm15 "bin\clang.exe"))) {
    Fail "LLVM 15 not found at '$Llvm15' (need bin\clang.exe). patches/ requires clang-15 - VS's clang has no MIPS backend. Override with -Llvm15 or `$env:BMHERO_LLVM15."
}
if (-not (Test-Path (Join-Path $Msys "usr\bin\make.exe"))) {
    Fail "MSYS2 not found at '$Msys' (need usr\bin\make.exe). Override with -Msys or `$env:BMHERO_MSYS."
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found - is Visual Studio 2022 installed?" }
$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Fail "no VS installation with the x64 C++ toolset found" }

Write-Host "toolchains:"
Write-Host "  LLVM15 : $Llvm15"
Write-Host "  VS     : $vsPath"
Write-Host "  MSYS2  : $Msys"

$devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
if (-not (Test-Path $devShell)) { Fail "VS DevShell module not found at $devShell" }
# VsDevCmd shells out to vswhere by bare name; without its directory on PATH it
# prints "'vswhere.exe' is not recognized" and skips parts of the setup.
$vswhereDir = Split-Path $vswhere -Parent
if ($env:PATH -notlike "*$vswhereDir*") { $env:PATH = "$vswhereDir;" + $env:PATH }
Import-Module $devShell
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Set-Location $root

$env:PATH = (Join-Path $Llvm15 "bin") + ";" + $env:PATH + ";" +
            (Join-Path $Msys "ucrt64\bin") + ";" + (Join-Path $Msys "usr\bin")

# --- 2. patches --------------------------------------------------------------
$patchDir = Join-Path $root "patches"
$patchBin = Join-Path $patchDir "patches.bin"
$doPatches = switch ($Patches) {
    "always" { $true }
    "never"  { $false }
    default  {
        if (-not (Test-Path $patchBin)) { $true }
        else {
            $binTime = (Get-Item $patchBin).LastWriteTime
            $newer = Get-ChildItem "$patchDir\*" -Include *.c,*.h -File |
                     Where-Object { $_.LastWriteTime -gt $binTime }
            if ($newer) {
                Write-Host "`nstale patch sources: $(($newer | ForEach-Object Name) -join ', ')"
                $true
            } else { $false }
        }
    }
}
if ($doPatches) {
    # ninja does NOT reliably re-run the patch make; a stale patches.bin is a
    # mismatch crash at boot, so clean is mandatory here, not just make.
    # CC=clang / LD=ld.lld mirror what the fork's CMakeLists passes (it invokes
    # `cmake -E env CC=clang LD=ld.lld make`). Without them make falls back to
    # its default `cc`, which resolves to MSYS2 gcc and rejects every MIPS flag
    # in the Makefile. LLVM15\bin is first on PATH, so `clang` is v15 here.
    Write-Host "`n=== patches: make clean && make (CC=clang LD=ld.lld) ==="
    Push-Location $patchDir
    try {
        $env:CC = "clang"; $env:LD = "ld.lld"
        & make clean
        if ($LASTEXITCODE -ne 0) { Fail "make clean failed" }
        & make
        if ($LASTEXITCODE -ne 0) { Fail "patches make failed" }
    } finally {
        Pop-Location
        Remove-Item Env:\CC -ErrorAction SilentlyContinue
        Remove-Item Env:\LD -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "`npatches up to date - skipping (use -Patches always to force)"
}

# --- 3. build ----------------------------------------------------------------
if (-not (Test-Path $buildDir)) {
    Fail "build dir '$buildDir' does not exist - configure it with cmake first"
}
Write-Host "`n=== cmake --build $buildDir --target BMHeroRecompiled ==="
& cmake --build $buildDir --target BMHeroRecompiled
if ($LASTEXITCODE -ne 0) { Fail "cmake build failed" }

$exe = Join-Path $buildDir "BMHeroRecompiled.exe"
if (-not (Test-Path $exe)) { Fail "build reported success but $exe is missing" }
$mb = [math]::Round((Get-Item $exe).Length / 1MB, 1)
Write-Host "`nBUILD OK: $exe ($mb MB)" -ForegroundColor Green

# --- 4. soak -----------------------------------------------------------------
if ($Soak -gt 0 -or $AnimProbe) {
    $soakScript = Join-Path $root "tools\arena-soak.ps1"
    $totalFails = 0

    if ($Soak -gt 0) {
        Write-Host "`n=== boot soak: $Soak iterations ==="
        & powershell -ExecutionPolicy Bypass -File $soakScript -N $Soak
        $totalFails += $LASTEXITCODE
    }
    if ($AnimProbe) {
        Write-Host "`n=== anim probe (A1.4 set-bomb pose gate) ==="
        & powershell -ExecutionPolicy Bypass -File $soakScript -AnimProbe
        $totalFails += $LASTEXITCODE
    }

    if ($totalFails -ne 0) { Fail "$totalFails soak/probe failure(s) - do NOT hand this build over" }
    Write-Host "`nSOAK GREEN - this exact build is cleared for handoff." -ForegroundColor Green
}
exit 0
