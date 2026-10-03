@echo off
rem Compat shim - prefer scripts\desktop_tracker\build\build_native_enforcer.bat
call "%~dp0build\build_native_enforcer.bat" %*
exit /b %ERRORLEVEL%
