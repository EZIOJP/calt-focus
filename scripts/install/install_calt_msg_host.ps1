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

# Always keep an Objectarray}. `$ids = $null; $ids += "a"; $ids += "b"` becomes the
# 64-char glue "ab" — Edge then refuses nativeMessaging and SoftLand kills the browser.
$ids = [System.Collections.Generic.List[string]]::new()
if ($ExtensionIds -and $ExtensionIds.Count -gt 0) {
  foreach ($x in $ExtensionIds) {
    if ($x -and $x.Trim()) { [void]$ids.Add($x.Trim()) }
  }
}
if ($ExtensionId -and $ExtensionId.Trim()) { [void]$ids.Add($ExtensionId.Trim()) }

# Auto-detect unpacked Gate / SelfTracker ids from Edge (and Chrome) when none passed.
if ($ids.Count -eq 0) {
  $prefPaths = @(
    (Join-Path $env:LOCALAPPDATA "Microsoft\Edge\User Data\Default\Secure Preferences"),
    (Join-Path $env:LOCALAPPDATA "Google\Chrome\User Data\Default\Secure Preferences")
  )
  $needle = @("calt-gate-extension", "selftracker-extension", "calt-gate", "selftracker")
  foreach ($pref in $prefPaths) {
    if (-not (Test-Path $pref)) { continue }
    try {
      $doc = Get-Content -Raw -Encoding UTF8 $pref | ConvertFrom-Json
      $settings = $doc.extensions.settings
      if (-not $settings) { continue }
      foreach ($prop in $settings.PSObject.Properties) {
        $extId = [string]$prop.Name
        # Chromium extension ids are exactly 32 chars [a-p]. Reject glued/corrupt keys
        # (seen: Gate+SelfTracker concatenated into one 64-char "id" → native host refuses Edge).
        if ($extId.Length -ne 32 -or $extId -notmatch '^[a-p]{32}$') { continue }
        $path = [string]$prop.Value.path
        if (-not $path) { continue }
        foreach ($n in $needle) {
          if ($path -like "*$n*") {
            if (-not $ids.Contains($extId)) { [void]$ids.Add($extId) }
            break
          }
        }
      }
    } catch {
      # Secure Preferences can be locked / odd-shaped — fall through to manual ids.
    }
  }
}

if ($ids.Count -eq 0) {
  throw "Pass -ExtensionId <id> and/or -ExtensionIds @('id1','id2') for Gate + SelfTracker (or load unpacked Gate in Edge first)."
}

$ExePath = (Resolve-Path $ExePath).Path
$exeJson = $ExePath.Replace('\', '\\')
$ManifestDir = Join-Path $env:LOCALAPPDATA "CALT\NativeMessagingHosts"
New-Item -ItemType Directory -Force -Path $ManifestDir | Out-Null
$ManifestPath = Join-Path $ManifestDir "com.calt.msg_host.json"

$origins = @($ids | ForEach-Object { "chrome-extension://$_/" })
# Hand-built JSON, UTF-8 no BOM — Chromium reports "host is forbidden" on BOM / bad origins.
$originLines = ($origins | ForEach-Object { "    `"$_`"" }) -join ",`n"
$json = @"
{
  "name": "com.calt.msg_host",
  "description": "CALT SoftLand get_mode + track_tab (solo pack)",
  "path": "$exeJson",
  "type": "stdio",
  "allowed_origins": [
$originLines
  ]
}
"@
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($ManifestPath, $json.Trim() + "`n", $utf8NoBom)

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
