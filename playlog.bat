@echo off
rem Launch the recomp with stdout+stderr captured to game_console.log so the
rem runtime's abort/error messages survive a crash (a bare 0xC0000409 dump says
rem nothing; the console text right before it usually names the cause).
rem Detaches like play.bat; args pass through.
cd /d "%~dp0"
start "" cmd /c ""build-cmake\BMHeroRecompiled.exe" %* > game_console.log 2>&1"
