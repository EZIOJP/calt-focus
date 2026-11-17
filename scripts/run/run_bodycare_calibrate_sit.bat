@echo off
setlocal
cd /d "%~dp0"
echo Sit at your desk — recording ~4s upper-body baseline...
call run_bodycare_sidecar.bat --calibrate-sit --preview --oneshot-exit
endlocal
