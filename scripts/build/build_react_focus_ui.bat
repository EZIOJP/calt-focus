@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "STUDY=%REPO%\..\Cognitive-Aware Learning Tutor"
set "VITE_CFG=%STUDY%\calt-focus\frontend\vite.config.ts"

if not exist "%VITE_CFG%" (
  echo Missing Study Focus vite config: %VITE_CFG%
  exit /b 1
)
if not exist "%STUDY%\node_modules" (
  echo Study node_modules missing — run npm install in Study first.
  exit /b 1
)

echo Building React Focus UI in Study ^(outDir Study\dist-focus^)...
pushd "%STUDY%"
call npx --yes vite build --config "calt-focus/frontend/vite.config.ts"
set "ERR=%ERRORLEVEL%"
popd
if not "%ERR%"=="0" (
  echo vite build failed
  exit /b 1
)

call "%~dp0sync_react_dist_from_study.bat"
exit /b %ERRORLEVEL%
