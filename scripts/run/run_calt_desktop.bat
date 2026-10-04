@echo off
setlocal
cd /d "%~dp0..\.."
set "ROOT=%CD%"
set "CALT_REPO=%ROOT%"

echo === CALT Focus (prebuilt UI) ===
echo UI: dist-focus\
echo SoftLand/Arm: native enforcer (no Study :8000)
echo.

if not exist "%ROOT%\dist-focus\index.html" (
  echo dist-focus missing — syncing React UI from Study sibling...
  call "%ROOT%\scripts\build\sync_react_dist_from_study.bat"
  if errorlevel 1 (
    echo ERROR: could not restore dist-focus from Study
    exit /b 1
  )
)

set "FOCUS_EXE="
if exist "%ROOT%\backend\calt_focus\build\Release\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\Release\calt_focus.exe"
if exist "%ROOT%\backend\calt_focus\build\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\calt_focus.exe"
if exist "%ROOT%\scripts\installer\installer_payload\bin\calt_focus.exe" if not defined FOCUS_EXE set "FOCUS_EXE=%ROOT%\scripts\installer\installer_payload\bin\calt_focus.exe"

if not defined FOCUS_EXE (
  echo calt_focus.exe missing — building...
  call "%ROOT%\scripts\build\build_native_focus.bat"
  if errorlevel 1 exit /b 1
  if exist "%ROOT%\backend\calt_focus\build\Release\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\Release\calt_focus.exe"
  if exist "%ROOT%\backend\calt_focus\build\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\calt_focus.exe"
)

if not defined FOCUS_EXE (
  echo ERROR: calt_focus.exe still missing after build.
  exit /b 1
)

sc.exe query CALTEnforcer 2>nul | findstr /I "RUNNING" >nul
if not errorlevel 1 goto :launch
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\install\install_enforcer_service.ps1" -Start 1>nul 2>nul

:launch
echo Starting %FOCUS_EXE%
echo Prebuilt UI: %ROOT%\dist-focus
start "" "%FOCUS_EXE%"
endlocal
exit /b 0
