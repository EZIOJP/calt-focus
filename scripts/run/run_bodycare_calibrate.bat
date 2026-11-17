@echo off
setlocal
cd /d "%~dp0"
call run_bodycare_sidecar.bat --calibrate --preview --oneshot-exit
endlocal
