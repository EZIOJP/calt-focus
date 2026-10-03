@echo off
REM Uninstall CALT native enforcer Windows service (Admin recommended)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_native_enforcer.ps1" -Uninstall
