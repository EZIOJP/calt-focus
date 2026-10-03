@echo off
rem Compat shim — legacy Python tray removed. Prefer Focus shell or native enforcer.
rem Daily UI: scripts\desktop_tracker\run\run_calt_desktop.bat
rem Tracking: scripts\desktop_tracker\run\run_native_enforcer_console.bat
call "%~dp0run\run_calt_desktop.bat" %*
exit /b %ERRORLEVEL%
