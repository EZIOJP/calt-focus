# Redeploy enforcer (+ Focus path for watchdog) and restart CALTEnforcer (Admin).
# Prefer scripts\run\_redeploy_calt_stack.ps1 when you also want Focus relaunched.
$ErrorActionPreference = "Stop"
& "$PSScriptRoot\_redeploy_calt_stack.ps1"
