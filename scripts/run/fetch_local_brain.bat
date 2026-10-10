@echo off
setlocal EnableExtensions
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch_local_brain.ps1"
if errorlevel 1 (
  echo.
  echo Download failed.
  pause
  exit /b 1
)
echo.
pause
endlocal
