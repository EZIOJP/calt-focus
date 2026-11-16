# Ensure CALT Sync receiver owns :8765 exclusively and stays reachable for the watch.
# Usage:
#   powershell -File scripts\run\ensure_calt_sync_host.ps1
#   powershell -File scripts\run\ensure_calt_sync_host.ps1 -InstallTask
param(
  [switch]$InstallTask,
  [switch]$NoStart
)

$ErrorActionPreference = 'Stop'
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Exe = Join-Path $Repo 'backend\calt_sync_host\build\calt_sync_host.exe'
if (-not (Test-Path $Exe)) {
  $Exe = Join-Path $Repo 'backend\calt_sync_host\build\Release\calt_sync_host.exe'
}
if (-not (Test-Path $Exe)) {
  throw "Missing calt_sync_host.exe - run scripts\build\build_calt_sync_host.bat"
}

function Get-LanIp {
  $Prefer = @(
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
      Where-Object {
        $_.IPAddress -notlike '127.*' -and
        $_.IPAddress -notlike '169.254.*' -and
        $_.PrefixOrigin -ne 'WellKnown'
      } |
      Sort-Object { if ($_.InterfaceAlias -match 'Wi-?Fi|Wireless') { 0 } else { 1 } }
  )
  if ($Prefer -and $Prefer[0].IPAddress) { return [string]$Prefer[0].IPAddress }
  return '192.168.0.110'
}

function Stop-Port8765Conflicts {
  $listeners = Get-NetTCPConnection -LocalPort 8765 -State Listen -ErrorAction SilentlyContinue
  foreach ($c in @($listeners)) {
    $procId = $c.OwningProcess
    if (-not $procId) { continue }
    $proc = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if (-not $proc) { continue }
    if ($proc.ProcessName -eq 'calt_sync_host') { continue }
    Write-Host "Stopping conflict on :8765 - $($proc.ProcessName) pid=$procId"
    Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
  }
  Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
      $_.Name -match 'python' -and $_.CommandLine -and
      ($_.CommandLine -match '8765' -or $_.CommandLine -match 'http\.server')
    } |
    ForEach-Object {
      Write-Host "Stopping python hitchhiker pid=$($_.ProcessId)"
      Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

function Ensure-Firewall {
  $rule = Get-NetFirewallRule -DisplayName 'CALT Focus Hub 8765' -ErrorAction SilentlyContinue
  if ($rule) { return }
  try {
    netsh advfirewall firewall add rule name="CALT Focus Hub 8765" dir=in action=allow protocol=TCP localport=8765 profile=private,domain | Out-Null
    Write-Host "Firewall: added CALT Focus Hub 8765"
  } catch {
    Write-Host "Firewall: could not add rule (run open_firewall_hub_8765.bat as Admin once)"
  }
}

function Write-UrlMirror([string]$LanIp) {
  $behavior = Join-Path $Repo 'data\productivity\behavior'
  $url = "http://${LanIp}:8765"
  $body = @"
{
  "ok": true,
  "receiver": "calt_sync_host",
  "port": 8765,
  "token": "calt-local-wearables",
  "base_url": "$url",
  "ingest_path": "/api/wearables/zepp",
  "health_path": "/health",
  "hint": "Zepp Base URL must be this, not localhost"
}
"@
  $utf8 = New-Object System.Text.UTF8Encoding $false
  [IO.File]::WriteAllText((Join-Path $behavior 'calt_sync_url.json'), $body.Trim() + "`n", $utf8)
  Write-Host "Final URL: $url"
  Write-Host "Token:     calt-local-wearables"
  return $url
}

if ($InstallTask) {
  $taskName = 'CALT Sync Host'
  $ps = Join-Path $PSScriptRoot 'ensure_calt_sync_host.ps1'
  $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$ps`""
  try {
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $arg -WorkingDirectory $Repo
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null
    Write-Host "Installed scheduled task: $taskName (At logon)"
  } catch {
    Write-Host "Could not install logon task (need Admin): $($_.Exception.Message)"
    Write-Host "Receiver will still start for this session."
  }
}

Stop-Port8765Conflicts
Ensure-Firewall
$lan = Get-LanIp
$url = Write-UrlMirror $lan

if (-not $NoStart) {
  $alive = Get-Process calt_sync_host -ErrorAction SilentlyContinue
  if (-not $alive) {
    $env:CALT_REPO = $Repo
    Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe -Parent) -WindowStyle Minimized
    Start-Sleep 1
  }
  $alive = Get-Process calt_sync_host -ErrorAction SilentlyContinue
  if (-not $alive) { throw 'calt_sync_host failed to start' }
  Write-Host "Receiver running pid=$($alive.Id)"
}

# Probe loopback first (fast). LAN URL is for the phone — wrong DHCP must not hang Focus.
try {
  $rLocal = Invoke-WebRequest -Uri 'http://127.0.0.1:8765/health' -UseBasicParsing -TimeoutSec 2
  Write-Host "Local health OK: $($rLocal.Content)"
} catch {
  Write-Host "Local health failed: $($_.Exception.Message)"
}
try {
  $r = Invoke-WebRequest -Uri "$url/health" -UseBasicParsing -TimeoutSec 2
  Write-Host "LAN health OK ($url): $($r.Content)"
} catch {
  Write-Host "LAN health probe failed on $url - phone needs this IP; update Wi-Fi / firewall if needed."
}

Write-Host ""
Write-Host "Zepp phone settings (finalize):"
Write-Host "  Base URL = $url"
Write-Host "  Token    = calt-local-wearables"
Write-Host "Phone browser check: $url/health"
