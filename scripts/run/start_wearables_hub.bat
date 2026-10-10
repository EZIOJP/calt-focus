@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
title CALT Focus NutriNode
echo NutriNode and watch ingest are served by CALT Focus on :8765.
echo This script no longer starts the Python hub — that process would take the port.
echo.
echo   Open CALT Focus first.
echo   Phone Take photo:        http://^<PC-LAN-IP^>:8765/n
echo   Windows live webcam:     http://127.0.0.1:8765/n
echo   Amazfit / Zepp Base URL: http://^<PC-LAN-IP^>:8765
echo   Token: calt-local-wearables
echo.
echo First time on Wi-Fi? Run as Admin: scripts\run\open_firewall_hub_8765.bat
echo Photo AI: GEMINI_API_KEY / LLM_CLOUD_API_KEY / LLM_API_KEY
echo            or behavior\nutrition\nutrition_llm.json
echo.
exit /b 0
