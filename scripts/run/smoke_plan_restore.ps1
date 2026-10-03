<#
  Restored planner ops smoke (start/complete/roll_forward/overlay/adherence/routine.apply).
  Requires calt_enforcer with restored gateway ops.
#>
$ErrorActionPreference = "Stop"
$gw = Join-Path $PSScriptRoot "gateway_cmd.ps1"
$fail = 0

function Check($label, $cond, $detail = "") {
  if ($cond) { Write-Host "PASS  $label" }
  else { Write-Host "FAIL  $label"; if ($detail) { Write-Host "      $detail" }; $script:fail++ }
}

function LocalIso([datetime]$dt) {
  $dt.ToString("yyyy-MM-dd'T'HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture)
}

$now = Get-Date
$title = "restore-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$c = & $gw -Op "plan.upsert" -Payload (@{
  title = $title
  category = "study"
  start_at = (LocalIso $now.AddHours(1))
  end_at = (LocalIso $now.AddHours(2))
  planned_minutes = 60
  remaining_minutes = 60
  status = "scheduled"
} | ConvertTo-Json -Compress)
Check "create block" ($c -match '"ok"\s*:\s*true') $c
$id = 0
if ($c -match '"id"\s*:\s*(\d+)') { $id = [int]$Matches[1] }

$s = & $gw -Op "plan.start" -Payload "{`"id`":$id}"
Check "plan.start" ($s -match '"in_progress"') $s

$done = & $gw -Op "plan.complete" -Payload "{`"id`":$id,`"minutes_spent`":15}"
Check "plan.complete partial" ($done -match '"ok"\s*:\s*true') $done

# Recreate for roll-forward with remaining time
$title2 = "restore-rf-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$c2 = & $gw -Op "plan.upsert" -Payload (@{
  title = $title2
  category = "study"
  start_at = (LocalIso $now.AddHours(3))
  end_at = (LocalIso $now.AddHours(4))
  planned_minutes = 60
  remaining_minutes = 45
  status = "scheduled"
} | ConvertTo-Json -Compress)
$id2 = 0
if ($c2 -match '"id"\s*:\s*(\d+)') { $id2 = [int]$Matches[1] }
$rf = & $gw -Op "plan.roll_forward" -Payload "{`"id`":$id2}"
Check "plan.roll_forward" ($rf -match '"rolled_block"' -and $rf -match '"new_block"') $rf

$from = $now.AddDays(-1).ToString("yyyy-MM-dd'T'00:00:00")
$to = $now.AddDays(1).ToString("yyyy-MM-dd'T'00:00:00")
$ovPayload = (@{ from = $from; to = $to } | ConvertTo-Json -Compress)
$ov = & $gw -Op "plan.overlay" -Payload $ovPayload
Check "plan.overlay" ($ov -match '"sessions"') $ov

$day = $now.ToString("yyyy-MM-dd")
$ad = & $gw -Op "plan.adherence" -Payload (@{ day = $day } | ConvertTo-Json -Compress)
Check "plan.adherence" ($ad -match '"planned_minutes"') $ad

$ap = & $gw -Op "routine.apply" -Payload (@{ skip_overlaps = $true } | ConvertTo-Json -Compress)
Check "routine.apply" ($ap -match '"created"') $ap

# cleanup
if ($id -gt 0) { & $gw -Op "plan.delete" -Payload "{`"id`":$id}" | Out-Null }
if ($id2 -gt 0) { & $gw -Op "plan.delete" -Payload "{`"id`":$id2}" | Out-Null }
if ($rf -match '"new_block"[^}]*"id"\s*:\s*(\d+)') {
  & $gw -Op "plan.delete" -Payload ("{`"id`":" + $Matches[1] + "}") | Out-Null
}

if ($fail -eq 0) { Write-Host ""; Write-Host "Planner restore smoke: all checks passed" }
else { Write-Host ""; Write-Host "Planner restore smoke: $fail failure(s)"; exit 1 }
