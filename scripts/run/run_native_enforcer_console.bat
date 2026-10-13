@echo off
setlocal
cd /d "%~dp0..\.."
set "REPO=%CD%"
rem Productivity SoT (Focus) — not Study vocab_app.db
set "CALT_DB=%REPO%\data\productivity\productivity.db"
set "CALT_ENFORCER_LOCK=%REPO%\data\productivity\behavior\enforcer_owner.lock"
set "EXE="
if exist "%REPO%\backend\calt_enforcer\build\Release\calt_enforcer.exe" set "EXE=%REPO%\backend\calt_enforcer\build\Release\calt_enforcer.exe"
if exist "%REPO%\backend\calt_enforcer\build\calt_enforcer.exe" set "EXE=%REPO%\backend\calt_enforcer\build\calt_enforcer.exe"
if not defined EXE (
  echo Missing calt_enforcer.exe — run scripts\build\build_native_enforcer.bat
  exit /b 1
)
if not exist "%CALT_DB%" (
  echo Missing %CALT_DB%
  exit /b 1
)
rem Dual pipe listeners (service + console) make Focus plan.overlay hang forever.
for /f "tokens=3" %%s in ('sc query CALTEnforcer ^| findstr /i "STATE"') do set "SVC_STATE=%%s"
if /i "%SVC_STATE%"=="RUNNING" (
  echo CALTEnforcer service is already RUNNING — refuse console start.
  echo Dual enforcers steal \\.\pipe\calt_enforcer_cmd and empty the calendar day.
  echo Use Focus tray "Restart enforcer" or: sc stop CALTEnforcer
  exit /b 2
)
echo Console enforcer. DB=%CALT_DB%
echo Ctrl+C to stop. For boot stay-alive use scripts\install\install_native_enforcer.ps1 as Admin.
"%EXE%"
endlocal
