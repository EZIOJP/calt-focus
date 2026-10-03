@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "STUDY=%REPO%\..\Cognitive-Aware Learning Tutor"
set "SRC_DIST=%STUDY%\dist-focus"
set "DST=%REPO%\dist-focus"

if not exist "%SRC_DIST%\index.html" (
  echo Study dist-focus missing at:
  echo   %SRC_DIST%
  echo Build it once in Study: npm run build -- --config calt-focus/frontend/vite.config.ts
  echo Or run: scripts\build\build_react_focus_ui.bat
  exit /b 1
)

echo Syncing React UI: "%SRC_DIST%" -^> "%DST%"
if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%"
robocopy "%SRC_DIST%" "%DST%" /E /NFL /NDL /NJH /NJS /nc /ns /np >nul
if errorlevel 8 (
  echo robocopy failed
  exit /b 1
)
if not exist "%DST%\index.html" (
  echo Sync failed — no index.html
  exit /b 1
)
echo OK: %DST%\index.html
endlocal
exit /b 0
