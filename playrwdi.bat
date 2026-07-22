@echo off
rem Launch the RelWithDebInfo build (build-rwdi, has a PDB) from the repo root
rem so assets/ resolves. Crash dumps from THIS exe symbolize in cdb — every
rem crash names its function instead of a bare offset. Args pass through.
cd /d "%~dp0"
start "" "build-rwdi\BMHeroRecompiled.exe" %*
