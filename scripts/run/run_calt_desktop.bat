@echo off
setlocal
call "%~dp0..\..\_common.bat" env-only
if errorlevel 1 exit /b 1

cd /d "%ROOT%"

echo === CALT Focus (prebuilt UI â€” no Vite required) ===
echo UI: dist-focus\ from npm run build:focus
echo API: only :8000 if you use Arm/Disarm / live gate
echo.

rem One-time (or after UI edits): precompiled pages for the desktop shell
if not exist "%ROOT%\dist-focus\index.html" (
  echo dist-focus missing â€” building precompiled UI once...
  call npm run build:focus
  if errorlevel 1 (
    echo ERROR: npm run build:focus failed
    exit /b 1
  )
)

set "FOCUS_EXE="
if exist "%ROOT%\backend\calt_focus\build\Release\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\Release\calt_focus.exe"
if exist "%ROOT%\backend\calt_focus\build\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\calt_focus.exe"
if exist "%ROOT%\scripts\desktop_tracker\installer\installer_payload\bin\calt_focus.exe" if not defined FOCUS_EXE set "FOCUS_EXE=%ROOT%\scripts\desktop_tracker\installer\installer_payload\bin\calt_focus.exe"

if not defined FOCUS_EXE (
  echo calt_focus.exe missing â€” building...
  call "%ROOT%\scripts\desktop_tracker\build\build_native_focus.bat"
  if errorlevel 1 exit /b 1
  if exist "%ROOT%\backend\calt_focus\build\Release\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\Release\calt_focus.exe"
  if exist "%ROOT%\backend\calt_focus\build\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\calt_focus.exe"
)

if not defined FOCUS_EXE (
  echo ERROR: calt_focus.exe still missing after build.
  exit /b 1
)

rem Keep enforcer alive (best effort)
sc.exe query CALTEnforcer 2>nul | findstr /I "RUNNING" >nul
if not errorlevel 1 goto :launch
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\desktop_tracker\install\install_enforcer_service.ps1" -Start 1>nul 2>nul

:launch
echo Starting %FOCUS_EXE%
echo Prebuilt UI: %ROOT%\dist-focus
start "" "%FOCUS_EXE%"
echo.
echo Tray Run starts API if needed. You do NOT need npm run dev / Vite.
endlocal
exit /b 0
