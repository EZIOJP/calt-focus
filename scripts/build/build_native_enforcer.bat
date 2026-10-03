@echo off
setlocal EnableExtensions
rem Standalone Focus repo: scripts\build → repo root
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "SRC=%REPO%\backend\calt_enforcer"
set "BUILD=%SRC%\build"

powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%\scripts\build\fetch_sqlite.ps1"
if errorlevel 1 exit /b 1

where cmake >nul 2>&1
if errorlevel 1 (
  echo Install CMake, then re-run.
  exit /b 1
)

set "USE_VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
)
if defined VSINSTALL set "USE_VS=1"

if exist "%BUILD%\CMakeCache.txt" (
  findstr /C:"CMAKE_GENERATOR:INTERNAL=NMake" "%BUILD%\CMakeCache.txt" >nul 2>&1
  if not errorlevel 1 (
    echo Clearing stale NMake CMake cache...
    rmdir /s /q "%BUILD%"
  )
)

if defined USE_VS (
  echo Using Visual Studio + MSVC at %VSINSTALL%
  cmake -S "%SRC%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64
  if errorlevel 1 exit /b 1
  cmake --build "%BUILD%" --config Release
  if errorlevel 1 exit /b 1
) else (
  where g++ >nul 2>&1
  if errorlevel 1 (
    echo.
    echo ERROR: No C++ toolchain found.
    echo Preferred: Visual Studio 2022 Build Tools + "Desktop development with C++"
    echo Fallback: LLVM MinGW ^(winget install MartinStorsjo.LLVM-MinGW.UCRT^)
    exit /b 1
  )
  echo Using MinGW/g++ fallback ^(MSVC preferred when VC Tools are installed^).
  if exist "%BUILD%\CMakeCache.txt" (
    findstr /C:"Visual Studio" "%BUILD%\CMakeCache.txt" >nul 2>&1
    if not errorlevel 1 (
      echo Clearing VS CMake cache for MinGW...
      rmdir /s /q "%BUILD%"
    )
  )
  cmake -S "%SRC%" -B "%BUILD%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
  if errorlevel 1 exit /b 1
  cmake --build "%BUILD%"
  if errorlevel 1 exit /b 1
)

set "PAYLOAD_BIN=%REPO%\scripts\installer\installer_payload\bin"
if not exist "%PAYLOAD_BIN%" mkdir "%PAYLOAD_BIN%"

if exist "%BUILD%\Release\calt_enforcer.exe" (
  echo Built: %BUILD%\Release\calt_enforcer.exe
  copy /Y "%BUILD%\Release\calt_enforcer.exe" "%PAYLOAD_BIN%\calt_enforcer.exe" >nul
  echo Copied for Inno: %PAYLOAD_BIN%\calt_enforcer.exe
) else if exist "%BUILD%\calt_enforcer.exe" (
  echo Built: %BUILD%\calt_enforcer.exe
  copy /Y "%BUILD%\calt_enforcer.exe" "%PAYLOAD_BIN%\calt_enforcer.exe" >nul
  echo Copied for Inno: %PAYLOAD_BIN%\calt_enforcer.exe
) else (
  echo Build finished but exe not found under %BUILD%
  exit /b 1
)
endlocal
