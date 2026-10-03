# CALT Enforcer - keep-alive (native C++ preferred; Python fallback)
#
# Preferred no-admin (recommended for daily use):
#   powershell -ExecutionPolicy Bypass -File scripts\desktop_tracker\install\install_enforcer_service.ps1 -Start
#
# Real Windows Service (admin + native binary) â€” USE THIS for commitment / anti-impulse:
#   powershell -File scripts\desktop_tracker\install\install_native_enforcer.ps1
# The default Scheduled Task runs as the logged-in user and can be removed with:
#   Unregister-ScheduledTask -TaskName "CALT Enforcer" -Confirm:$false
# That is not a password-protected commitment device. Prefer the Admin Service when
# resisting self-bypass matters.
#
# Foreground debug:
#   scripts\desktop_tracker\run\run_native_enforcer_console.bat
#   OR: python -m backend.behavior.enforcer_service
#
# Unregister Task:
#   Unregister-ScheduledTask -TaskName "CALT Enforcer" -Confirm:$false

param(
  [string]$TaskName = "CALT Enforcer",
  [switch]$Start,
  [switch]$Unregister,
  [switch]$AllowPythonFallback
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path (Split-Path $ScriptDir -Parent) -Parent
$TrackerDir = Split-Path $ScriptDir -Parent

if ($Unregister) {
  Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
  Write-Host "Unregistered scheduled task: $TaskName"
  exit 0
}

$NativeExeCandidates = @(
  (Join-Path $Root "backend\calt_enforcer\build\Release\calt_enforcer.exe"),
  (Join-Path $Root "backend\calt_enforcer\build\calt_enforcer.exe"),
  (Join-Path $TrackerDir "installer\installer_payload\bin\calt_enforcer.exe")
)
$NativeExe = $NativeExeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$NativeConsoleBat = Join-Path $TrackerDir "run\run_native_enforcer_console.bat"

$useNative = $false
if ($NativeExe -and (Test-Path $NativeConsoleBat)) {
  $useNative = $true
}

if ($useNative) {
  # Console bat sets CALT_DB / lock; scheduled task keeps native alive at logon.
  $action = New-ScheduledTaskAction `
    -Execute "cmd.exe" `
    -Argument "/c `"$NativeConsoleBat`"" `
    -WorkingDirectory $Root
  Write-Host "OK: $TaskName -> native calt_enforcer (C++)"
  Write-Host "  Launcher: $NativeConsoleBat"
  Write-Host "  Exe:      $NativeExe"
} else {
  if (-not $AllowPythonFallback) {
    throw "Native calt_enforcer.exe not found. Run build\build_native_enforcer.bat, or pass -AllowPythonFallback (legacy only)."
  }
  $Py = Join-Path $Root ".venv\Scripts\python.exe"
  if (-not (Test-Path $Py)) {
    $Py = Join-Path $Root ".venv\Scripts\pythonw.exe"
  }
  if (-not (Test-Path $Py)) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { $Py = $cmd.Source } else { throw "python not found (venv or PATH); build native enforcer preferred" }
  }
  $Arg = "-m backend.behavior.enforcer_service"
  $action = New-ScheduledTaskAction -Execute $Py -Argument $Arg -WorkingDirectory $Root
  Write-Host "WARN: $TaskName -> Python enforcer_service (LEGACY FALLBACK - prefer native)"
  Write-Host "  Execute: $Py $Arg"
}

$settings = New-ScheduledTaskSettingsSet `
  -AllowStartIfOnBatteries `
  -DontStopIfGoingOnBatteries `
  -StartWhenAvailable `
  -MultipleInstances IgnoreNew `
  -RestartCount 999 `
  -RestartInterval (New-TimeSpan -Minutes 1) `
  -ExecutionTimeLimit ([TimeSpan]::Zero) `
  -Hidden

# Highest only works when this script is elevated; otherwise fall back to Limited.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principalCheck = New-Object Security.Principal.WindowsPrincipal($identity)
$elevated = $principalCheck.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$runLevel = if ($elevated) { "Highest" } else { "Limited" }
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel $runLevel
$logon = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME

Register-ScheduledTask `
  -TaskName $TaskName `
  -Action $action `
  -Trigger $logon `
  -Settings $settings `
  -Principal $principal `
  -Force | Out-Null

Write-Host "  WorkDir: $Root"
Write-Host "  RunLevel: $runLevel$(if (-not $elevated) { ' (not elevated â€” task is user-removable)' })"
Write-Host "  Restart-on-crash: RestartCount=999 / RestartInterval=1min (Task Scheduler)"
Write-Host "  NOTE: While the task/process is restarting, Arm kills briefly fail-open;"
Write-Host "        SoftLand decide still works via calt_msg_host + softland_policy.json."
Write-Host "  COMMITMENT: for password-grade stay-alive use Admin Service:"
Write-Host "        install\install_native_enforcer.ps1"
Write-Host "  Control UI: Focus tray via run\run_calt_desktop.bat"

if ($Start) {
  Start-ScheduledTask -TaskName $TaskName
  Write-Host "Started $TaskName"
}
