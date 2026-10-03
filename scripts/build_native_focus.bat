@echo off
rem Compat shim - prefer scripts\desktop_tracker\build\build_native_focus.bat
call "%~dp0build\build_native_focus.bat" %*
exit /b %ERRORLEVEL%
