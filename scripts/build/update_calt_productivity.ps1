#Requires -Version 5.1
<#
.SYNOPSIS
  Rebuild Focus UI (React via Study) + natives for standalone calt-focus repo.
#>
param(
  [switch]$SkipNatives,
  [switch]$SkipUi,
  [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Repo

function Write-Info([string]$msg) {
  if (-not $Quiet) { Write-Host $msg }
}

function Test-FileLocked([string]$path) {
  if (-not (Test-Path $path)) { return $false }
  try {
    $fs = [System.IO.File]::Open($path, 'Open', 'ReadWrite', 'None')
    $fs.Close()
    return $false
  } catch {
    return $true
  }
}

function Copy-ReplaceOrNew([string]$src, [string]$dest) {
  if (-not (Test-Path $src)) { return @{ ok = $false; mode = "missing_src" } }
  $dir = Split-Path $dest -Parent
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
  if (Test-FileLocked $dest) {
    $newPath = "$dest.new"
    Copy-Item -Force $src $newPath
    return @{ ok = $true; mode = "pending_new"; path = $newPath }
  }
  Copy-Item -Force $src $dest
  return @{ ok = $true; mode = "replaced"; path = $dest }
}

$pending = [ordered]@{
  schema_version = 1
  updated_at     = (Get-Date).ToString("s")
  ui             = $null
  enforcer       = $null
  focus          = $null
  msg_host       = $null
  needs_restart  = $false
  notes          = @()
}

if (-not $SkipUi) {
  Write-Info "==> build React Focus UI + sync dist-focus"
  $ui = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "scripts\build\build_react_focus_ui.bat" `
    -WorkingDirectory $Repo -Wait -PassThru -NoNewWindow
  if ($ui.ExitCode -eq 0 -and (Test-Path (Join-Path $Repo "dist-focus\index.html"))) {
    $pending.ui = "ok"
  } else {
    # Fall back to sync-only if Study already has a built dist-focus
    $sync = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "scripts\build\sync_react_dist_from_study.bat" `
      -WorkingDirectory $Repo -Wait -PassThru -NoNewWindow
    if ($sync.ExitCode -eq 0) {
      $pending.ui = "synced"
      $pending.notes += "used Study dist-focus sync (vite rebuild skipped/failed)"
    } else {
      $pending.ui = "failed"
      $pending.notes += "UI build/sync failed"
    }
  }
}

if (-not $SkipNatives) {
  Write-Info "==> build natives"
  foreach ($bat in @(
      "scripts\build\build_native_enforcer.bat",
      "scripts\build\build_native_focus.bat",
      "scripts\build\build_calt_msg_host.bat"
    )) {
    $p = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", $bat -WorkingDirectory $Repo -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) { $pending.notes += "$bat failed" }
  }

  $enfSrc = @(
    (Join-Path $Repo "backend\calt_enforcer\build\Release\calt_enforcer.exe"),
    (Join-Path $Repo "backend\calt_enforcer\build\calt_enforcer.exe")
  ) | Where-Object { Test-Path $_ } | Select-Object -First 1
  $focusSrc = @(
    (Join-Path $Repo "backend\calt_focus\build\Release\calt_focus.exe"),
    (Join-Path $Repo "backend\calt_focus\build\calt_focus.exe")
  ) | Where-Object { Test-Path $_ } | Select-Object -First 1
  $msgSrc = @(
    (Join-Path $Repo "backend\calt_msg_host\build\Release\calt_msg_host.exe"),
    (Join-Path $Repo "backend\calt_msg_host\build\calt_msg_host.exe")
  ) | Where-Object { Test-Path $_ } | Select-Object -First 1

  if ($enfSrc) {
    $r = Copy-ReplaceOrNew $enfSrc (Join-Path $Repo "backend\calt_enforcer\build\calt_enforcer.exe")
    $pending.enforcer = $r.mode
    if ($r.mode -eq "pending_new") { $pending.needs_restart = $true }
    $prog = Join-Path $env:ProgramData "CALT\enforcer\calt_enforcer.exe"
    if (Test-Path (Split-Path $prog -Parent)) {
      $r2 = Copy-ReplaceOrNew $enfSrc $prog
      if ($r2.mode -eq "pending_new") { $pending.needs_restart = $true }
    }
  }
  if ($focusSrc) {
    $r = Copy-ReplaceOrNew $focusSrc (Join-Path $Repo "backend\calt_focus\build\calt_focus.exe")
    $pending.focus = $r.mode
    if ($r.mode -eq "pending_new") { $pending.needs_restart = $true }
  }
  if ($msgSrc) {
    $pending.msg_host = "ok"
  }
}

$behavior = Join-Path $Repo "data\productivity\behavior"
New-Item -ItemType Directory -Force -Path $behavior | Out-Null
$pendingPath = Join-Path $behavior "pending_update.json"
$pending | ConvertTo-Json -Depth 6 | Set-Content -Path $pendingPath -Encoding UTF8
Write-Info "Wrote $pendingPath"
if ($pending.needs_restart) {
  Write-Info "Natives staged as .new — SoftLand off + Disarm, then Apply pending update."
} else {
  Write-Info "Update complete."
}
