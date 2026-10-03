@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "SRC=%REPO%\frontend\shell"
set "DST=%REPO%\dist-focus"

if not exist "%SRC%\index.html" (
  echo ERROR: missing shell UI at %SRC%\index.html
  exit /b 1
)

echo Shipping Focus shell UI: "%SRC%" -^> "%DST%"
if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%"
robocopy "%SRC%" "%DST%" /E /NFL /NDL /NJH /NJS /nc /ns /np /R:1 /W:1 >nul
if errorlevel 8 (
  echo robocopy failed
  exit /b 1
)
if not exist "%DST%\index.html" (
  echo Ship failed — no index.html in dist-focus
  exit /b 1
)
if not exist "%DST%\js\main.js" (
  echo Ship failed — no js\main.js in dist-focus
  exit /b 1
)
echo OK: %DST%\index.html
endlocal
exit /b 0
