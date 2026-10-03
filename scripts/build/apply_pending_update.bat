@echo off
REM Apply staged *.exe.new binaries after Focus/enforcer have exited.
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "FOCUS_NEW=%REPO%\backend\calt_focus\build\calt_focus.exe.new"
set "FOCUS_EXE=%REPO%\backend\calt_focus\build\calt_focus.exe"
set "ENF_NEW=%REPO%\backend\calt_enforcer\build\calt_enforcer.exe.new"
set "ENF_EXE=%REPO%\backend\calt_enforcer\build\calt_enforcer.exe"
set "PAYLOAD=%REPO%\scripts\installer\installer_payload\bin"
set "NEED_FOCUS=0"
set "NEED_ENF=0"
if exist "%FOCUS_NEW%" set "NEED_FOCUS=1"
if exist "%ENF_NEW%" set "NEED_ENF=1"
if "%NEED_FOCUS%"=="0" if "%NEED_ENF%"=="0" (
  echo No *.exe.new found — nothing to apply.
  goto :start_focus
)

echo Waiting for processes to exit (max ~90s)...
set /A LOOPS=0
:wait
set /A LOOPS+=1
if %LOOPS% GTR 90 (
  echo Timed out waiting for exit. Close Focus/enforcer manually, then re-run this bat.
  pause
  exit /b 1
)
if "%NEED_FOCUS%"=="1" (
  tasklist /FI "IMAGENAME eq calt_focus.exe" 2>nul | find /I "calt_focus.exe" >nul && (timeout /t 1 /nobreak >nul & goto wait)
)
if "%NEED_ENF%"=="1" (
  tasklist /FI "IMAGENAME eq calt_enforcer.exe" 2>nul | find /I "calt_enforcer.exe" >nul && (timeout /t 1 /nobreak >nul & goto wait)
)

if exist "%FOCUS_NEW%" (
  echo Applying Focus update...
  copy /Y "%FOCUS_NEW%" "%FOCUS_EXE%" >nul
  if exist "%PAYLOAD%\calt_focus.exe" copy /Y "%FOCUS_NEW%" "%PAYLOAD%\calt_focus.exe" >nul
  del /F /Q "%FOCUS_NEW%" >nul 2>&1
)
if exist "%ENF_NEW%" (
  echo Applying Enforcer update...
  copy /Y "%ENF_NEW%" "%ENF_EXE%" >nul
  if exist "%PAYLOAD%\calt_enforcer.exe" copy /Y "%ENF_NEW%" "%PAYLOAD%\calt_enforcer.exe" >nul
  del /F /Q "%ENF_NEW%" >nul 2>&1
  if exist "C:\ProgramData\CALT\enforcer\calt_enforcer.exe" (
    copy /Y "%ENF_EXE%" "C:\ProgramData\CALT\enforcer\calt_enforcer.exe" >nul
  )
)

:start_focus
sc.exe start CALTEnforcer >nul 2>&1
if exist "%FOCUS_EXE%" (
  echo Starting Focus...
  start "" "%FOCUS_EXE%"
) else if exist "%REPO%\backend\calt_focus\build\Release\calt_focus.exe" (
  start "" "%REPO%\backend\calt_focus\build\Release\calt_focus.exe"
) else (
  echo Focus exe missing — build first.
  exit /b 1
)
endlocal
exit /b 0
