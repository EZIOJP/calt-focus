# Dev emergency stop: SoftLand OFF + Arm disarm + stop CALTEnforcer + kill processes.
# Double-click: stop_calt_enforcer.bat at repo root (self-elevates for service stop).
#
#   powershell -File scripts\run\stop_calt_enforcer.ps1
#   powershell -File scripts\run\stop_calt_enforcer.ps1 -AlsoQuitFocus
#   powershell -File scripts\run\stop_calt_enforcer.ps1 -Password "your-unlock"
#
param(
  [switch]$AlsoQuitFocus,
  [string]$Password = '',
  [switch]$NoElevate
)

$ErrorActionPreference = 'Continue'
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not (Test-Path (Join-Path $Repo 'data\productivity'))) {
  $Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}
$Behavior = Join-Path $Repo 'data\productivity\behavior'
$PolPath = Join-Path $Behavior 'enforcer_policy.json'
$SoftPath = Join-Path $Behavior 'softland_policy.json'
$LockPath = Join-Path $Behavior 'enforcer_owner.lock'
$Gw = Join-Path $PSScriptRoot 'gateway_cmd.ps1'

function Test-IsAdmin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $p = New-Object Security.Principal.WindowsPrincipal($id)
  return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-Gateway([string]$Op, [string]$Payload, [int]$TimeoutMs = 5000) {
  if (-not (Test-Path -LiteralPath $Gw)) { return $false, 'gateway_cmd.ps1 missing' }
  $tmp = Join-Path $env:TEMP ("calt_stop_gw_" + [guid]::NewGuid().ToString() + '.json')
  try {
    # PayloadFile avoids nested-powershell eating JSON quotes (enabled -> bad_payload).
    [IO.File]::WriteAllText($tmp, $Payload, [Text.UTF8Encoding]::new($false))
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $Gw -Op $Op -PayloadFile $tmp -TimeoutMs $TimeoutMs 2>&1
    $text = ($out | Out-String).Trim()
    $ok = ($LASTEXITCODE -eq 0) -and ($text -match '"ok"\s*:\s*true')
    return $ok, $text
  } catch {
    return $false, $_.Exception.Message
  } finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
  }
}

function Read-UnlockPassword {
  if ($Password) { return $Password }
  if ($env:CALT_UNLOCK_PASSWORD) { return $env:CALT_UNLOCK_PASSWORD }
  if (-not (Test-Path -LiteralPath $PolPath)) { return '' }
  try {
    $j = Get-Content -LiteralPath $PolPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($j.unlock_password) { return [string]$j.unlock_password }
    if ($j.unlock_phrase) { return [string]$j.unlock_phrase }
  } catch {}
  return ''
}

function Write-DisarmedPolicy {
  if (-not (Test-Path -LiteralPath $PolPath)) { return }
  try {
    $j = Get-Content -LiteralPath $PolPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $exes = @($j.exes | ForEach-Object { $_.ToString() })
    if ($exes.Count -eq 0) { $exes = @('steam.exe') }
    $exesJson = ($exes | ForEach-Object { '    "' + ($_ -replace '\\', '\\\\' -replace '"', '\"') + '"' }) -join ",`n"
    $pwd = if ($j.unlock_password) { [string]$j.unlock_password } else { '' }
    $phrase = if ($j.unlock_phrase) { [string]$j.unlock_phrase } else { '' }
    $body = @"
{
  "hard_block_armed": false,
  "gate_locked": false,
  "incubation_active": false,
  "exes": [
$exesJson
  ],
  "note": "dev_emergency_stop",
  "lock_mode": "none",
  "lock_until_unix": 0,
  "unlock_password": "$pwd",
  "unlock_phrase": "$phrase",
  "anti_tamper": false,
  "protect_uninstall": false
}
"@
    [IO.File]::WriteAllText($PolPath, $body.Trim() + "`n", [Text.UTF8Encoding]::new($false))
    Write-Host "  wrote disarmed $PolPath"
  } catch {
    Write-Host "  WARN: could not patch enforcer_policy.json: $($_.Exception.Message)"
  }
}

function Write-SoftlandOff {
  if (-not (Test-Path -LiteralPath $SoftPath)) { return }
  try {
    $raw = Get-Content -LiteralPath $SoftPath -Raw -Encoding UTF8
    if ($raw -match '"softland_enabled"\s*:\s*true') {
      $raw = $raw -replace '"softland_enabled"\s*:\s*true', '"softland_enabled": false'
      Set-Content -LiteralPath $SoftPath -Value $raw -Encoding UTF8 -NoNewline
      Write-Host "  wrote softland_enabled=false"
    } else {
      Write-Host "  softland already off (mirror)"
    }
  } catch {
    Write-Host "  WARN: could not patch softland_policy.json: $($_.Exception.Message)"
  }
}

Write-Host ''
Write-Host '=== CALT enforcer STOP (dev emergency) ==='
Write-Host "Repo: $Repo"

# Elevate for service control when needed
$svc = Get-Service -Name 'CALTEnforcer' -ErrorAction SilentlyContinue
$needAdmin = $svc -and $svc.Status -eq 'Running' -and -not (Test-IsAdmin)
if ($needAdmin -and -not $NoElevate) {
  Write-Host 'Elevating for service stop...'
  $argList = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', "`"$PSCommandPath`"",
    '-NoElevate'
  )
  if ($AlsoQuitFocus) { $argList += '-AlsoQuitFocus' }
  if ($Password) { $argList += @('-Password', "`"$Password`"") }
  try {
    $p = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $p.ExitCode
  } catch {
    Write-Host "WARN: UAC elevation failed ($($_.Exception.Message)). Continuing without admin..."
  }
}

# 1) SoftLand OFF via gateway (confirm UNLOCK)
Write-Host '[1/5] SoftLand OFF...'
$ok, $out = Invoke-Gateway 'softland.set_enabled' '{"enabled":false,"confirm":"UNLOCK"}'
if ($ok) { Write-Host "  gateway ok: $out" }
else { Write-Host "  gateway: $out (will patch mirror after kill)" }

# 2) Disarm Arm via gateway
Write-Host '[2/5] Disarm Arm...'
$pwd = Read-UnlockPassword
$armPayload = @{
  hard_block_armed = $false
  gate_locked      = $false
  lock_mode        = 'none'
  anti_tamper      = $false
}
if ($pwd) {
  $armPayload.provided_unlock = $pwd
  $armPayload.unlock_password = $pwd
}
$armJson = ($armPayload | ConvertTo-Json -Compress)
$ok, $out = Invoke-Gateway 'arm.set' $armJson 8000
if ($ok) { Write-Host "  gateway ok: $out" }
else { Write-Host "  gateway: $out (will force policy after kill)" }

# 3) Stop Windows service
Write-Host '[3/5] Stop CALTEnforcer service...'
if ($svc) {
  try {
    Stop-Service -Name 'CALTEnforcer' -Force -ErrorAction Stop
    Start-Sleep -Seconds 1
    $svc.Refresh()
    Write-Host "  service status: $($svc.Status)"
  } catch {
    Write-Host "  WARN: Stop-Service failed: $($_.Exception.Message)"
    if (-not (Test-IsAdmin)) {
      Write-Host '  Re-run this bat as Administrator if the service stays Running.'
    }
  }
} else {
  Write-Host '  service not installed'
}

# Disable scheduled-task keep-alive if present
Get-ScheduledTask -TaskName 'CALT Enforcer' -ErrorAction SilentlyContinue | ForEach-Object {
  try {
    Stop-ScheduledTask -TaskName $_.TaskName -ErrorAction SilentlyContinue
    Disable-ScheduledTask -TaskName $_.TaskName -ErrorAction SilentlyContinue | Out-Null
    Write-Host "  disabled scheduled task: $($_.TaskName)"
  } catch {}
}

# 4) Kill processes
Write-Host '[4/5] Kill calt_enforcer.exe...'
Get-Process -Name 'calt_enforcer' -ErrorAction SilentlyContinue | ForEach-Object {
  try {
    Stop-Process -Id $_.Id -Force -ErrorAction Stop
    Write-Host "  killed pid $($_.Id)"
  } catch {
    Write-Host "  WARN: could not kill pid $($_.Id): $($_.Exception.Message)"
  }
}
Start-Sleep -Milliseconds 400
$still = Get-Process -Name 'calt_enforcer' -ErrorAction SilentlyContinue
if ($still) {
  Write-Host '  ERROR: calt_enforcer still running — need Admin taskkill'
  & taskkill.exe /F /IM calt_enforcer.exe 2>&1 | Out-Host
} else {
  Write-Host '  calt_enforcer not running'
}

if ($AlsoQuitFocus) {
  Write-Host '  Also quitting calt_focus.exe...'
  & taskkill.exe /F /IM calt_focus.exe 2>$null | Out-Null
  & taskkill.exe /F /IM calt_msg_host.exe 2>$null | Out-Null
}

# 5) Force mirrors + drop owner lock (so a stray restart is fail-open)
Write-Host '[5/5] Force disarmed mirrors...'
Write-DisarmedPolicy
Write-SoftlandOff
if (Test-Path -LiteralPath $LockPath) {
  Remove-Item -LiteralPath $LockPath -Force -ErrorAction SilentlyContinue
  Write-Host '  removed enforcer_owner.lock'
}

# Final status
$svc2 = Get-Service -Name 'CALTEnforcer' -ErrorAction SilentlyContinue
$proc = Get-Process -Name 'calt_enforcer' -ErrorAction SilentlyContinue
Write-Host ''
Write-Host '--- result ---'
if ($svc2) { Write-Host "service: $($svc2.Status) (StartType=$($svc2.StartType))" }
else { Write-Host 'service: (not installed)' }
Write-Host ("enforcer process: " + $(if ($proc) { 'STILL RUNNING' } else { 'stopped' }))
if (Test-Path -LiteralPath $PolPath) {
  try {
    $p = Get-Content -LiteralPath $PolPath -Raw | ConvertFrom-Json
    Write-Host "policy hard_block_armed=$($p.hard_block_armed) lock_mode=$($p.lock_mode)"
  } catch {}
}
Write-Host ''
if ($proc) {
  Write-Host 'FAILED: enforcer still alive. Right-click bat -> Run as administrator.'
  exit 1
}
Write-Host 'OK: enforcer stopped. SoftLand/Arm cleared for next start.'
Write-Host 'Restart later: scripts\install\install_native_enforcer.ps1 (Admin) or Start-Service CALTEnforcer'
Write-Host ''
exit 0
