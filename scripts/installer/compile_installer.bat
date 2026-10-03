@echo off
rem Compile CALT Productivity Inno installer (needs Inno Setup 6 ISCC).
setlocal EnableExtensions
cd /d "%~dp0"
set "ISS=%CD%\install_calt_desktop.iss"
set "ISCC="
set "FAILED=0"

echo.
echo === CALT Productivity installer compile ===
echo Script dir: %CD%
echo.

if exist "%LocalAppData%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"

if not defined ISCC (
  echo ERROR: Inno Setup 6 not found ^(ISCC.exe^).
  echo.
  echo Install it, then re-run this script:
  echo   winget install --id JRSoftware.InnoSetup -e
  echo   OR download: https://jrsoftware.org/isinfo.php
  echo.
  echo After install, you can also open install_calt_desktop.iss in Compil32 and Build.
  set "FAILED=1"
  goto :Done
)

if not exist "%ISS%" (
  echo ERROR: Missing %ISS%
  set "FAILED=1"
  goto :Done
)

rem Ensure native exe is in payload\bin when already built
if exist "%~dp0..\..\..\backend\calt_enforcer\build\calt_enforcer.exe" (
  if not exist "%~dp0installer_payload\bin" mkdir "%~dp0installer_payload\bin"
  copy /Y "%~dp0..\..\..\backend\calt_enforcer\build\calt_enforcer.exe" "%~dp0installer_payload\bin\calt_enforcer.exe" >nul
)
if exist "%~dp0..\..\..\backend\calt_enforcer\build\Release\calt_enforcer.exe" (
  if not exist "%~dp0installer_payload\bin" mkdir "%~dp0installer_payload\bin"
  copy /Y "%~dp0..\..\..\backend\calt_enforcer\build\Release\calt_enforcer.exe" "%~dp0installer_payload\bin\calt_enforcer.exe" >nul
)

echo Compiling with: %ISCC%
echo Source: %ISS%
echo.
"%ISCC%" "%ISS%"
if errorlevel 1 (
  echo.
  echo ERROR: ISCC failed.
  set "FAILED=1"
  goto :Done
)

echo.
echo OK - output under: %CD%\Output\
dir /b "%CD%\Output\CALTProductivitySetup-*.exe" 2>nul
dir /b "%CD%\Output\CALTDesktopSetup-*.exe" 2>nul
if not exist "%CD%\Output\*.exe" echo ^(no .exe yet ??? check ISCC messages above^)

:Done
echo.
if "%FAILED%"=="1" (
  echo Compile failed.
  pause
  endlocal & exit /b 1
)
echo Done.
pause
endlocal & exit /b 0

