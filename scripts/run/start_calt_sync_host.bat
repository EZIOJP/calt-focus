@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "EXE=%REPO%\backend\calt_sync_host\build\calt_sync_host.exe"
if not exist "%EXE%" set "EXE=%REPO%\backend\calt_sync_host\build\Release\calt_sync_host.exe"
if not exist "%EXE%" (
  echo Missing calt_sync_host.exe — run scripts\build\build_calt_sync_host.bat
  pause
  exit /b 1
)
title CALT Sync receiver :8765
echo CALT Sync receiver — idle until the watch uploads.
echo   Base URL  http://^<PC-LAN-IP^>:8765
echo   Token     calt-local-wearables
echo First time on Wi-Fi, as Admin: scripts\run\open_firewall_hub_8765.bat
echo.
"%EXE%"
endlocal
