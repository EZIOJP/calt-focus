# Install CALT native C++ enforcer as Windows Service (Admin).
# Build first: scripts\desktop_tracker\build\build_native_enforcer.bat
#
# Copies the exe (+ MinGW runtime DLLs if needed) to C:\ProgramData\CALT\enforcer\
# so LocalSystem can start without Desktop path spaces or a user PATH.
# IMPORTANT: save this file as UTF-8 or ASCII only (no fancy dashes/quotes).
param(
  [switch]$Uninstall,
  [string]$DbPath = ""
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$svc = "CALTEnforcer"
$lockPath = Join-Path $Repo "data\productivity\behavior\enforcer_owner.lock"
$installDir = Join-Path $env:ProgramData "CALT\enforcer"
$Exe = Join-Path $installDir "calt_enforcer.exe"

if ($Uninstall) {
  $oldDb = [Environment]::GetEnvironmentVariable("CALT_DB", "Machine")
  Stop-Service $svc -Force -ErrorAction SilentlyContinue
  sc.exe delete $svc | Out-Null
  if ($oldDb) {
    $oldLock = Join-Path (Split-Path $oldDb -Parent) "behavior\enforcer_owner.lock"
    if (Test-Path $oldLock) { Remove-Item -Force $oldLock -ErrorAction SilentlyContinue }
  }
  [Environment]::SetEnvironmentVariable("CALT_DB", $null, "Machine")
  [Environment]::SetEnvironmentVariable("CALT_ENFORCER_LOCK", $null, "Machine")
  if (Test-Path $lockPath) { Remove-Item -Force $lockPath -ErrorAction SilentlyContinue }
  $legacyLock = Join-Path $Repo "data\behavior\enforcer_owner.lock"
  if (Test-Path $legacyLock) { Remove-Item -Force $legacyLock -ErrorAction SilentlyContinue }
  if (Test-Path $installDir) {
    Remove-Item -Recurse -Force $installDir -ErrorAction SilentlyContinue
  }
  Write-Host "Removed service $svc, ProgramData enforcer dir, and cleared CALT_DB / lock env"
  exit 0
}

$ExeCandidates = @(
  (Join-Path $Repo "backend\calt_enforcer\build\Release\calt_enforcer.exe"),
  (Join-Path $Repo "backend\calt_enforcer\build\calt_enforcer.exe")
)
$ExeSrc = $ExeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $ExeSrc) { throw "Missing calt_enforcer.exe - run scripts\build\build_native_enforcer.bat first" }

if (-not $DbPath) {
  $DbPath = Join-Path $Repo "data\productivity\productivity.db"
}
if (-not (Test-Path $DbPath)) {
  throw "Missing DB: $DbPath"
}

function Find-MingwRuntimeDir {
  $gpp = Get-Command g++ -ErrorAction SilentlyContinue
  if ($gpp) { return Split-Path $gpp.Source -Parent }
  $winget = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
  if (Test-Path $winget) {
    $hit = Get-ChildItem $winget -Recurse -Filter "libc++.dll" -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if ($hit) { return $hit.DirectoryName }
  }
  return $null
}

function Copy-EnforcerRuntime([string]$DestDir, [string]$SrcExe) {
  New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
  Copy-Item -Force $SrcExe (Join-Path $DestDir "calt_enforcer.exe")
  $srcDir = Split-Path $SrcExe -Parent
  foreach ($dll in @("libc++.dll", "libunwind.dll")) {
    $fromSrc = Join-Path $srcDir $dll
    if (Test-Path $fromSrc) {
      Copy-Item -Force $fromSrc (Join-Path $DestDir $dll)
      continue
    }
    $mingw = Find-MingwRuntimeDir
    if ($mingw) {
      $fromMingw = Join-Path $mingw $dll
      if (Test-Path $fromMingw) {
        Copy-Item -Force $fromMingw (Join-Path $DestDir $dll)
      }
    }
  }
}

Copy-EnforcerRuntime $installDir $ExeSrc
# Sidecar so a bare ProgramData launch (no inherited Machine env) still finds Focus SoT.
Set-Content -Path (Join-Path $installDir "calt_db.path") -Value $DbPath -Encoding utf8
Write-Host "Installed binary: $Exe"
Get-ChildItem $installDir | ForEach-Object { Write-Host "  $($_.Name)" }

[Environment]::SetEnvironmentVariable("CALT_DB", $DbPath, "Machine")
[Environment]::SetEnvironmentVariable("CALT_ENFORCER_LOCK", $lockPath, "Machine")

Get-Process calt_enforcer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
if (Test-Path $lockPath) { Remove-Item -Force $lockPath -ErrorAction SilentlyContinue }

$existing = Get-Service -Name $svc -ErrorAction SilentlyContinue
if ($existing) {
  Stop-Service $svc -Force -ErrorAction SilentlyContinue
  sc.exe delete $svc | Out-Null
  Start-Sleep -Seconds 2
}

$binPath = "`"$Exe`" --service"
New-Service -Name $svc -BinaryPathName $binPath -DisplayName "CALT Desktop Enforcer (native)" -StartupType Automatic | Out-Null

$svcKey = "HKLM:\SYSTEM\CurrentControlSet\Services\$svc"
New-ItemProperty -Path $svcKey -Name Environment -PropertyType MultiString -Force -Value @(
  "CALT_DB=$DbPath",
  "CALT_ENFORCER_LOCK=$lockPath"
) | Out-Null

sc.exe description $svc "Native hard-block + desktop tracker for CALT. SoftLand/Arm SoT in productivity.db." | Out-Null
sc.exe failure $svc reset= 0 actions= restart/5000/restart/5000/restart/5000 | Out-Null

# Verify LocalSystem-like load (no MinGW on PATH) before SCM start.
$oldPath = $env:PATH
$env:PATH = "C:\Windows\System32;C:\Windows"
try {
  $probe = Start-Process -FilePath $Exe -ArgumentList "--service" -WorkingDirectory $installDir -PassThru -Wait -NoNewWindow
  # 1063 = ERROR_FAILED_SERVICE_CONTROLLER_CONNECT (expected outside SCM)
  if ($probe.ExitCode -ne 1063 -and $probe.ExitCode -ne 0) {
    throw "Enforcer failed LocalSystem-like probe (exit $($probe.ExitCode)). Missing runtime DLL beside exe?"
  }
  Write-Host "Probe OK (exit $($probe.ExitCode) - ready for SCM)."
} finally {
  $env:PATH = $oldPath
}

Start-Service $svc
Start-Sleep -Seconds 2
$st = Get-Service $svc
if ($st.Status -ne "Running") {
  Write-Host "WARN: service state=$($st.Status). Check Event Viewer System for 7000/7009."
  Write-Host "Fallback: powershell -File scripts\desktop_tracker\install\install_enforcer_service.ps1 -Start"
  throw "CALTEnforcer did not reach Running"
}
Write-Host "OK: $svc $($st.Status)"
Write-Host "Exe=$Exe"
Write-Host "DB=$DbPath"
Write-Host "Auto-start enabled. Desktop tracking runs without calt_focus."
