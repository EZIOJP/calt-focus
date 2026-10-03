@echo off
rem Compat shim - prefer scripts\desktop_tracker\build\build_calt_msg_host.bat
call "%~dp0build\build_calt_msg_host.bat" %*
exit /b %ERRORLEVEL%
