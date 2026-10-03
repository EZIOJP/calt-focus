@echo off
setlocal
cd /d "%~dp0..\.."
set "ROOT=%CD%"
set "PORT=5190"
set "URL=http://127.0.0.1:%PORT%/wireframes/"

echo === CALT Focus wireframes ===
echo %URL%
echo Edit product UI in frontend\shell\  ^|  notes save in the browser
echo Ctrl+C stops the server.
echo.

set "PY="
where py >nul 2>&1 && set "PY=py"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY (
  echo ERROR: need Python ^(py or python^) on PATH.
  exit /b 1
)

start "calt-focus-wireframes" /MIN cmd /c "%PY% -m http.server %PORT% --directory \"%ROOT%\frontend\""
ping -n 2 127.0.0.1 >nul
start "" "%URL%"
echo Server running in a minimized window titled calt-focus-wireframes.
echo Close that window when done.
endlocal
exit /b 0
