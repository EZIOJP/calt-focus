# Compat shim — prefer scripts\desktop_tracker\install\install_native_enforcer.ps1
param(
  [switch]$Uninstall,
  [string]$DbPath = ""
)
$ErrorActionPreference = "Stop"
$target = Join-Path $PSScriptRoot "install\install_native_enforcer.ps1"
$splat = @{}
if ($Uninstall) { $splat["Uninstall"] = $true }
if ($DbPath) { $splat["DbPath"] = $DbPath }
& $target @splat
exit $LASTEXITCODE
