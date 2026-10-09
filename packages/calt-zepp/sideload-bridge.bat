@echo off
REM Install CALT Sync via Developer Bridge (NO QR scanner needed)
cd /d "%~dp0"

where zeus >nul 2>&1
if errorlevel 1 (
  echo Installing @zeppos/zeus-cli...
  call npm i @zeppos/zeus-cli -g
)

echo.
echo ========================================
echo  EASIER PATH: Zeus Bridge (no QR/scan)
echo ========================================
echo.
echo ON PHONE (Zepp app):
echo  1. Profile -^> Settings -^> About -^> tap Zepp logo 7 times
echo  2. Go back: Profile -^> your watch / Devices page
echo  3. Scroll to BOTTOM -^> open "Developer Mode"
echo  4. Tap + (top right) -^> "Bridge"  (not Scan)
echo  5. Leave Bridge ON; same Zepp account as PC
echo.
echo Then this script will run: zeus login + zeus bridge
echo In the bridge prompt type:
echo    connect
echo    install
echo.
pause

call zeus login
if errorlevel 1 (
  echo Login failed / cancelled.
  pause
  exit /b 1
)

echo.
echo Starting bridge... type: connect   then   install
echo.
call zeus bridge

pause
