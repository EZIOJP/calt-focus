@echo off
setlocal
cd /d "%~dp0bodycare_sidecar"
if not exist ".venv\Scripts\python.exe" (
  echo Creating venv...
  py -3.11 -m venv .venv 2>nul || py -3 -m venv .venv
  ".venv\Scripts\python.exe" -m pip install -U pip
  ".venv\Scripts\python.exe" -m pip install -r requirements.txt
)
echo Starting CALT body-care sidecar ^(Ctrl+C to stop^)...
".venv\Scripts\python.exe" main.py %*
endlocal
