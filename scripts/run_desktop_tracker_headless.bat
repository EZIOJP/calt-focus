@echo off
rem Compat shim — Python headless tracker removed. Native enforcer owns tracking.
rem Prefer: scripts\desktop_tracker\run\run_native_enforcer_console.bat
call "%~dp0run\run_native_enforcer_console.bat" %*
exit /b %ERRORLEVEL%
