@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "SRC=%REPO%\backend\calt_sync_host"
set "BUILD=%SRC%\build"

where cmake >nul 2>&1
if errorlevel 1 (
  echo Install CMake, then re-run.
  exit /b 1
)

where g++ >nul 2>&1
if errorlevel 1 (
  echo ERROR: g++ not found.
  exit /b 1
)

if exist "%BUILD%\CMakeCache.txt" (
  findstr /C:"Visual Studio" "%BUILD%\CMakeCache.txt" >nul 2>&1
  if not errorlevel 1 rmdir /s /q "%BUILD%"
)

cmake -S "%SRC%" -B "%BUILD%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 exit /b 1
cmake --build "%BUILD%"
if errorlevel 1 exit /b 1
echo Built: %BUILD%\calt_sync_host.exe
endlocal
