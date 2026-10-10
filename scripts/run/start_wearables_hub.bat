@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
title CALT Focus wearables hub :8765
echo Starting Python hub :8765 HTTP + :8766 HTTPS ^(camera^)...
echo Quit CALT Focus first — while Focus is open it owns :8765 for watch dumps.
echo.
echo   Windows Chrome webcam:   https://127.0.0.1:8766/n
echo   Phone Chrome NutriNode:  http://^<PC-LAN-IP^>:8765/n
echo   Amazfit / Zepp Base URL: http://^<PC-LAN-IP^>:8765
echo   Token: calt-local-wearables
echo.
echo First time on Wi-Fi? Run as Admin: scripts\run\open_firewall_hub_8765.bat
echo Photo AI: GEMINI_API_KEY / LLM_CLOUD_API_KEY ^(or Study .env^)
if exist "data\productivity\behavior\nutrition\nutrition_llm.json" (
  echo Photo AI config: behavior\nutrition\nutrition_llm.json
)
echo.
python "%~dp0wearables_hub.py"
if errorlevel 1 (
  echo.
  echo Failed — is Python on PATH?
  pause
)
endlocal
