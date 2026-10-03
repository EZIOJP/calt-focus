@echo off
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "SRC=%REPO%\backend\calt_msg_host"
set "BUILD=%SRC%\build"

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

if defined USE_VS (
  echo Using Visual Studio + MSVC at %VSINSTALL%
  cmake -S "%SRC%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64
  if errorlevel 1 exit /b 1
  cmake --build "%BUILD%" --config Release
  if errorlevel 1 exit /b 1
) else (
  where g++ >nul 2>&1
  if errorlevel 1 (
    echo ERROR: No C++ toolchain found. See build_native_enforcer.bat notes.
    exit /b 1
  )
  echo Using MinGW/g++ for calt_msg_host
  if exist "%BUILD%\CMakeCache.txt" (
    findstr /C:"Visual Studio" "%BUILD%\CMakeCache.txt" >nul 2>&1
    if not errorlevel 1 rmdir /s /q "%BUILD%"
    findstr /C:"Ninja" "%BUILD%\CMakeCache.txt" >nul 2>&1
    if not errorlevel 1 rmdir /s /q "%BUILD%"
  )
  cmake -S "%SRC%" -B "%BUILD%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
  if errorlevel 1 exit /b 1
  cmake --build "%BUILD%"
  if errorlevel 1 exit /b 1
)

if exist "%BUILD%\Release\calt_msg_host.exe" (
  echo Built: %BUILD%\Release\calt_msg_host.exe
) else if exist "%BUILD%\calt_msg_host.exe" (
  echo Built: %BUILD%\calt_msg_host.exe
) else (
  echo Build finished but exe not found under %BUILD%
  exit /b 1
)
endlocal
exit /b 0
