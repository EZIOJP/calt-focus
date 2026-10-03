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
echo Console enforcer. DB=%CALT_DB%
echo Ctrl+C to stop. For boot stay-alive use scripts\install\install_native_enforcer.ps1 as Admin.
"%EXE%"
endlocal
