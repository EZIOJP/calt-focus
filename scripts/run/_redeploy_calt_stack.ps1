# Redeploy enforcer + point watchdog at Focus, restart service, relaunch Focus (Admin).
$ErrorActionPreference = "Stop"
$Log = Join-Path $env:TEMP "calt_stack_redeploy.log"
function Log([string]$m) {
  $line = "$(Get-Date -Format o) $m"
  Add-Content -Path $Log -Value $line
  Write-Host $line
}
Log "=== stack redeploy start ==="
$Repo = "C:\Users\Lenovo\Desktop\calt-focus"
$EnforcerSrc = Join-Path $Repo "backend\calt_enforcer\build\calt_enforcer.exe"
$FocusSrc = Join-Path $Repo "backend\calt_focus\build\calt_focus.exe"
if (-not (Test-Path $EnforcerSrc)) { throw "Missing $EnforcerSrc — build enforcer first." }
if (-not (Test-Path $FocusSrc)) { throw "Missing $FocusSrc — build Focus first." }

$DestDir = "C:\ProgramData\CALT\enforcer"
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

Log "Stopping CALTEnforcer…"
Stop-Service CALTEnforcer -Force -ErrorAction SilentlyContinue
Start-Sleep 1
Get-Process calt_enforcer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process calt_focus -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep 1

Copy-Item -Force $EnforcerSrc (Join-Path $DestDir "calt_enforcer.exe")
Log "Copied enforcer"
# Sibling copy so ResolveFocusExePath finds Focus without CALT_REPO.
Copy-Item -Force $FocusSrc (Join-Path $DestDir "calt_focus.exe")
Log "Copied focus sibling"
$wv = Join-Path $Repo "backend\calt_focus\build\WebView2Loader.dll"
if (Test-Path $wv) { Copy-Item -Force $wv (Join-Path $DestDir "WebView2Loader.dll") }

$mingw = $null
try { $mingw = Split-Path (Get-Command g++ -ErrorAction Stop).Source -Parent } catch {}
if ($mingw) {
  foreach ($dll in @("libc++.dll", "libunwind.dll")) {
    $p = Join-Path $mingw $dll
    if (Test-Path $p) { Copy-Item -Force $p (Join-Path $DestDir $dll) }
  }
}

# Prefer the build-tree Focus (has RepoRoot walk + dist-focus). Watchdog reads this file.
$FocusLaunch = $FocusSrc
[System.IO.File]::WriteAllText((Join-Path $DestDir "focus_exe.path"), $FocusLaunch)
[System.IO.File]::WriteAllText((Join-Path $DestDir "calt_repo.path"), $Repo)

$lock = Join-Path $Repo "data\productivity\behavior\enforcer_owner.lock"
if (Test-Path $lock) { Remove-Item -Force $lock -ErrorAction SilentlyContinue }

Write-Host "Starting CALTEnforcer…"
Start-Service CALTEnforcer
Start-Sleep 2
Get-Service CALTEnforcer | Format-List Name, Status

$env:CALT_REPO = $Repo
$env:CALT_FOCUS_EXE = $FocusLaunch
Write-Host "Launching Focus…"
Start-Process -FilePath $FocusLaunch -WorkingDirectory (Split-Path $FocusLaunch -Parent)
Write-Host "Stack redeployed: enforcer service + Focus UI."
