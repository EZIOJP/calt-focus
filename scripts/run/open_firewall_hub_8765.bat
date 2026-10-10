@echo off
setlocal EnableExtensions
REM Allow LAN devices to reach Focus hub :8765 (NutriNode phone + CALT Sync).
REM Run once as Administrator.
netsh advfirewall firewall delete rule name="CALT Focus Hub 8765" >nul 2>&1
netsh advfirewall firewall delete rule name="CALT Focus Hub 8766 HTTPS" >nul 2>&1
netsh advfirewall firewall add rule name="CALT Focus Hub 8765" dir=in action=allow protocol=TCP localport=8765 profile=private,domain
if errorlevel 1 (
  echo FAILED — right-click this bat → Run as administrator
  pause
  exit /b 1
)
echo OK — CALT Focus :8765 allowed on private Wi-Fi
echo Phone Take photo:    http://^<PC-LAN-IP^>:8765/n
echo Windows live webcam: http://127.0.0.1:8765/n
pause
endlocal
