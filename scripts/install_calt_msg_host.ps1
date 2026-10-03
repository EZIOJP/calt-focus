# Compat shim — prefer scripts\desktop_tracker\install\install_calt_msg_host.ps1
param(
  [Parameter(Mandatory = $true)]
  [string]$ExtensionId,
  [string]$ExePath = ""
)
$ErrorActionPreference = "Stop"
$target = Join-Path $PSScriptRoot "install\install_calt_msg_host.ps1"
$splat = @{ ExtensionId = $ExtensionId }
if ($ExePath) { $splat["ExePath"] = $ExePath }
& $target @splat
exit $LASTEXITCODE
