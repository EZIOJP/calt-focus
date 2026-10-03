@echo off
setlocal EnableDelayedExpansion
rem Force-quit CALT Focus (calt_focus.exe). Lives in calt-focus/.
echo === CALT Focus: quit ===

tasklist /FI "IMAGENAME eq calt_focus.exe" 2>nul | find /I "calt_focus.exe" >nul
if errorlevel 1 (
  echo calt_focus.exe is not running.
  endlocal
  exit /b 0
)

echo Closing calt_focus.exe ...
taskkill /F /IM calt_focus.exe >nul 2>&1
if errorlevel 1 (
  echo WARN: taskkill failed.
  echo SoftLand/Arm watchdog may be holding it â€” turn SoftLand off, Disarm, then Quit from tray.
  echo Or run this bat as Administrator.
  endlocal
  exit /b 1
)

set /a _n=0
:wait_gone
tasklist /FI "IMAGENAME eq calt_focus.exe" 2>nul | find /I "calt_focus.exe" >nul
if not errorlevel 1 (
  set /a _n+=1
  if !_n! GEQ 15 (
    echo ERROR: calt_focus.exe still running after taskkill.
    endlocal
    exit /b 1
  )
  timeout /t 1 /nobreak >nul
  goto wait_gone
)

echo Quit. Focus is no longer running.
endlocal
exit /b 0
