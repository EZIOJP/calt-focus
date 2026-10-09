@echo off
REM CALT Sync QR — auto target (no device menu stuck)
cd /d "%~dp0"

where zeus >nul 2>&1
if errorlevel 1 (
  echo Installing @zeppos/zeus-cli...
  call npm i @zeppos/zeus-cli -g
)

echo.
echo ========================================
echo  CALT Sync — QR install (auto target)
echo ========================================
echo.
echo Phone: Developer Mode -^> + -^> Scan this QR
echo Want BOTH apps? Use: packages\sideload-both-watch-apps.bat
echo Stuck on menus? Use: packages\sideload-both-bridge.bat
echo.
pause

call zeus login
if errorlevel 1 (
  echo Login failed.
  pause
  exit /b 1
)

echo.
echo Preview target: Amazfit T-Rex 3
call zeus preview -t "Amazfit T-Rex 3"
if errorlevel 1 (
  echo -t failed, opening select...
  call zeus preview -s
)

pause
