@echo off
setlocal
cd /d "%~dp0"
set "CC=clang"
where clang >nul 2>&1
if errorlevel 1 (
  echo clang not found ? install LLVM/MinGW or edit this bat.
  exit /b 1
)
echo Building fixture EXEs with clang...
clang -O2 -o calt_test_block_target.exe calt_test_block_target.c
if errorlevel 1 exit /b 1
clang -O2 -o calt_test_safe.exe calt_test_safe.c
if errorlevel 1 exit /b 1
echo OK: calt_test_block_target.exe calt_test_safe.exe
dir /b *.exe
endlocal
