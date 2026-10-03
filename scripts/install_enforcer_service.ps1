# Compat shim — prefer scripts\desktop_tracker\install\install_enforcer_service.ps1
param(
  [string]$TaskName = "CALT Enforcer",
  [switch]$Start,
  [switch]$Unregister,
  [switch]$AllowPythonFallback
)
$ErrorActionPreference = "Stop"
$target = Join-Path $PSScriptRoot "install\install_enforcer_service.ps1"
$splat = @{ TaskName = $TaskName }
if ($Start) { $splat["Start"] = $true }
if ($Unregister) { $splat["Unregister"] = $true }
if ($AllowPythonFallback) { $splat["AllowPythonFallback"] = $true }
& $target @splat
exit $LASTEXITCODE
