@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
title CALT Sync receiver :8765
echo Finalizing CALT Sync on :8765 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0ensure_calt_sync_host.ps1"
if errorlevel 1 (
  echo FAILED
  pause
  exit /b 1
)
echo.
echo Keep this window open or rely on the logon task:
echo   powershell -File scripts\run\ensure_calt_sync_host.ps1 -InstallTask
echo.
pause
endlocal
