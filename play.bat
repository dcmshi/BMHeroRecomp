@echo off
rem Launch the recomp from the repo root so it finds assets/.
rem Double-click this instead of the exe directly. Args pass through
rem (e.g. play.bat --show-console).
rem
rem LAUNCHES build-rwdi, NOT build-cmake. That is what build.ps1 builds by
rem default, the only config arena-soak.ps1 ever tests, and therefore the only
rem binary that has been proven to boot. It is RelWithDebInfo: same performance
rem as Release, but it carries a PDB so a crash produces a symbolized dump.
rem
rem This used to point at build-cmake, which nothing had built in a long time -
rem so the script would silently launch a months-old exe while every "green soak"
rem referred to a different one. Caught 2026-07-27 while handing over a build for
rem a feel test. If you deliberately want the plain Release build, run
rem build-cmake\BMHeroRecompiled.exe directly from this directory.
cd /d "%~dp0"
if not exist "build-rwdi\BMHeroRecompiled.exe" (
  echo build-rwdi\BMHeroRecompiled.exe not found - build it first:
  echo     .\build.ps1 -Config rwdi -Soak 5
  exit /b 1
)
start "" "build-rwdi\BMHeroRecompiled.exe" %*
