@echo off
setlocal
cd /d "%~dp0"
echo Stand at your desk — recording ~4s upper-body baseline...
call run_bodycare_sidecar.bat --calibrate-stand --preview --oneshot-exit
endlocal
