@echo off
rem Compat shim - prefer scripts\desktop_tracker\run\run_calt_desktop.bat
call "%~dp0run\run_calt_desktop.bat" %*
exit /b %ERRORLEVEL%
