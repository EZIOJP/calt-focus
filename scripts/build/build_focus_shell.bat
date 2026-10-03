@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "SRC=%REPO%\frontend\shell"
set "DST=%REPO%\dist-focus"

if not exist "%SRC%\index.html" (
  echo ERROR: missing %SRC%\index.html
  exit /b 1
)

echo Shipping Focus HTML shell: frontend\shell -^> dist-focus
if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%" >nul 2>&1
xcopy /E /I /Y /Q "%SRC%\*" "%DST%\" >nul
if errorlevel 1 (
  echo ERROR: xcopy failed
  exit /b 1
)

if not exist "%DST%\index.html" (
  echo ERROR: dist-focus\index.html missing after copy
  exit /b 1
)

echo OK: %DST%
dir /b "%DST%\js\pages" 2>nul
exit /b 0
