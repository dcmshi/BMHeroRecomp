@echo off
rem Launch the recomp from the repo root so it finds assets/.
rem Double-click this instead of the exe directly. Args pass through
rem (e.g. play.bat --show-console).
cd /d "%~dp0"
start "" "build-cmake\BMHeroRecompiled.exe" %*
