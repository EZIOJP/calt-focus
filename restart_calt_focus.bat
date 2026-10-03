@echo off
setlocal EnableDelayedExpansion
rem Close + restart CALT Focus (calt_focus.exe). Lives in calt-focus/.
set "FOCUS_DIR=%~dp0"
set "ROOT=%FOCUS_DIR%.."
cd /d "%ROOT%"

echo === CALT Focus: close + restart ===

tasklist /FI "IMAGENAME eq calt_focus.exe" 2>nul | find /I "calt_focus.exe" >nul
if errorlevel 1 (
  echo calt_focus.exe is not running.
) else (
  echo Closing calt_focus.exe ...
  taskkill /F /IM calt_focus.exe >nul 2>&1
  if errorlevel 1 (
    echo WARN: taskkill failed â€” quit from tray ^(SoftLand off^) and re-run this bat.
    exit /b 1
  )
  set /a _n=0
  :wait_gone
  tasklist /FI "IMAGENAME eq calt_focus.exe" 2>nul | find /I "calt_focus.exe" >nul
  if not errorlevel 1 (
    set /a _n+=1
    if !_n! GEQ 15 (
      echo ERROR: calt_focus.exe still running after taskkill.
      exit /b 1
    )
    timeout /t 1 /nobreak >nul
    goto wait_gone
  )
  echo Closed.
)

set "FOCUS_EXE="
if exist "%ROOT%\backend\calt_focus\build\Release\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\Release\calt_focus.exe"
if exist "%ROOT%\backend\calt_focus\build\calt_focus.exe" set "FOCUS_EXE=%ROOT%\backend\calt_focus\build\calt_focus.exe"
if exist "%ROOT%\scripts\desktop_tracker\installer\installer_payload\bin\calt_focus.exe" if not defined FOCUS_EXE set "FOCUS_EXE=%ROOT%\scripts\desktop_tracker\installer\installer_payload\bin\calt_focus.exe"

if not defined FOCUS_EXE (
  echo ERROR: calt_focus.exe not found. Build first:
  echo   scripts\desktop_tracker\build\build_native_focus.bat
  exit /b 1
)

echo Starting !FOCUS_EXE!
start "" "!FOCUS_EXE!"
echo Done. Check the tray for CALT Focus.
endlocal
exit /b 0
