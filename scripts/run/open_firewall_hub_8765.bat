@echo off
setlocal EnableExtensions
REM Allow LAN devices to reach Focus hub :8765 (NutriNode phone + CALT Sync).
REM Run once as Administrator.
netsh advfirewall firewall delete rule name="CALT Focus Hub 8765" >nul 2>&1
netsh advfirewall firewall delete rule name="CALT Focus Hub 8766 HTTPS" >nul 2>&1
netsh advfirewall firewall add rule name="CALT Focus Hub 8765" dir=in action=allow protocol=TCP localport=8765 profile=private,domain
netsh advfirewall firewall add rule name="CALT Focus Hub 8766 HTTPS" dir=in action=allow protocol=TCP localport=8766 profile=private,domain
if errorlevel 1 (
  echo FAILED — right-click this bat → Run as administrator
  pause
  exit /b 1
)
echo OK — HTTP :8765 and HTTPS camera :8766 allowed on private Wi-Fi
echo Windows Chrome webcam: https://127.0.0.1:8766/n
pause
endlocal
