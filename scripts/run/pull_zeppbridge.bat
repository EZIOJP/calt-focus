@echo off
setlocal
cd /d "%~dp0..\.."
python "%~dp0pull_zeppbridge.py" %*
exit /b %ERRORLEVEL%
