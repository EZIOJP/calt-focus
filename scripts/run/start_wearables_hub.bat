@echo off
setlocal EnableExtensions
REM DEPRECATED for watch ingest — :8765 belongs to calt_sync_host (C++).
REM This Python hub fought the sync receiver (and python -m http.server) for the port.
echo.
echo CALT Sync watch uploads use calt_sync_host on :8765.
echo Run instead:  scripts\run\start_calt_sync_host.bat
echo.
echo NutriNode photo AI is separate — do not start this bat for Amazfit Dump ^& Send.
echo.
pause
exit /b 1
