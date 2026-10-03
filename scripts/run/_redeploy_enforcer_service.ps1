# Redeploy enforcer binary to ProgramData and restart CALTEnforcer (Admin).
$ErrorActionPreference = "Stop"
$Repo = "C:\Users\Lenovo\Desktop\calt-focus"
$Src = Join-Path $Repo "backend\calt_enforcer\build\calt_enforcer.exe"
if (-not (Test-Path $Src)) { throw "Missing $Src" }
$DestDir = "C:\ProgramData\CALT\enforcer"
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

Stop-Service CALTEnforcer -Force -ErrorAction SilentlyContinue
Start-Sleep 1
Get-Process calt_enforcer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep 1

Copy-Item -Force $Src (Join-Path $DestDir "calt_enforcer.exe")
$mingw = Split-Path (Get-Command g++).Source -Parent
foreach ($dll in @("libc++.dll", "libunwind.dll")) {
  $p = Join-Path $mingw $dll
  if (Test-Path $p) { Copy-Item -Force $p (Join-Path $DestDir $dll) }
}

$lock = Join-Path $Repo "data\productivity\behavior\enforcer_owner.lock"
if (Test-Path $lock) { Remove-Item -Force $lock -ErrorAction SilentlyContinue }

Start-Service CALTEnforcer
Start-Sleep 2
Get-Service CALTEnforcer | Format-List Name,Status
Write-Host "Redeployed. Pipe ACL fix active."
