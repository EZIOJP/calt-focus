# Register CALT Native Messaging Host for Edge/Chrome (SoftLand get_mode + track_tab).
# Usage (from repo root):
#   powershell -File scripts\desktop_tracker\install\install_calt_msg_host.ps1 -ExtensionId <gate_id>
#   powershell -File scripts\desktop_tracker\install\install_calt_msg_host.ps1 -ExtensionIds @('<gate_id>','<selftracker_id>')
# Get Edge unpacked ids from edge://extensions (Developer mode).

param(
  [string]$ExtensionId = "",
  [string[]]$ExtensionIds = @(),
  [string]$ExePath = ""
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $ExePath) {
  $cand = @(
    (Join-Path $Repo "backend\calt_msg_host\build\Release\calt_msg_host.exe"),
    (Join-Path $Repo "backend\calt_msg_host\build\calt_msg_host.exe")
  )
  foreach ($c in $cand) {
    if (Test-Path $c) { $ExePath = (Resolve-Path $c).Path; break }
  }
}
if (-not $ExePath -or -not (Test-Path $ExePath)) {
  throw "calt_msg_host.exe not found. Run scripts\desktop_tracker\build\build_calt_msg_host.bat first."
}

$ids = @()
if ($ExtensionIds -and $ExtensionIds.Count -gt 0) { $ids += $ExtensionIds }
if ($ExtensionId) { $ids += $ExtensionId }
$ids = $ids | Where-Object { $_ -and $_.Trim() } | Select-Object -Unique
if (-not $ids -or $ids.Count -eq 0) {
  throw "Pass -ExtensionId <id> and/or -ExtensionIds @('id1','id2') for Gate + SelfTracker."
}

$ExePath = (Resolve-Path $ExePath).Path
$ManifestDir = Join-Path $env:LOCALAPPDATA "CALT\NativeMessagingHosts"
New-Item -ItemType Directory -Force -Path $ManifestDir | Out-Null
$ManifestPath = Join-Path $ManifestDir "com.calt.msg_host.json"

$origins = @($ids | ForEach-Object { "chrome-extension://$_/" })
$obj = [ordered]@{
  name = "com.calt.msg_host"
  description = "CALT SoftLand get_mode + track_tab (solo pack)"
  path = $ExePath
  type = "stdio"
  allowed_origins = $origins
}
# Chromium rejects / mis-parses UTF-8 BOM on native-host manifests ("host forbidden").
$json = ($obj | ConvertTo-Json -Compress:$false)
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($ManifestPath, $json + "`n", $utf8NoBom)

$keys = @(
  "HKCU:\Software\Google\Chrome\NativeMessagingHosts\com.calt.msg_host",
  "HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\com.calt.msg_host"
)
foreach ($k in $keys) {
  New-Item -Path $k -Force | Out-Null
  Set-ItemProperty -Path $k -Name "(default)" -Value $ManifestPath
}

Write-Host "Registered com.calt.msg_host"
Write-Host "  exe:      $ExePath"
Write-Host "  manifest: $ManifestPath"
foreach ($o in $origins) { Write-Host "  origin:   $o" }
Write-Host "Reload Gate + SelfTracker after nativeMessaging permission."
