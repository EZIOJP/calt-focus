@echo off
setlocal
rem Dev emergency stop when CALT enforcer goes rogue.
rem SoftLand OFF + Disarm + stop CALTEnforcer service + kill calt_enforcer.exe
rem
rem   stop_calt_enforcer.bat
rem   stop_calt_enforcer.bat focus     (also kill Focus / msg-host)
rem
cd /d "%~dp0"
set "PS1=%~dp0scripts\run\stop_calt_enforcer.ps1"
if not exist "%PS1%" (
  echo ERROR: missing %PS1%
  exit /b 1
)

set "EXTRA="
if /I "%~1"=="focus" set "EXTRA=-AlsoQuitFocus"
if /I "%~1"=="-AlsoQuitFocus" set "EXTRA=-AlsoQuitFocus"

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %EXTRA%
set "EC=%ERRORLEVEL%"
echo.
pause
exit /b %EC%
