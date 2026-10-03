<#
  Phase 6b: active plan block → SoftLand via plan.apply_gate.
  Requires calt_enforcer running (with 6b binary).
#>
param()

$ErrorActionPreference = "Stop"
$gw = Join-Path $PSScriptRoot "gateway_cmd.ps1"
$fail = 0
$ids = @()

function Check($label, $cond, $detail = "") {
  if ($cond) {
    Write-Host "PASS  $label"
  } else {
    Write-Host "FAIL  $label"
    if ($detail) { Write-Host "      $detail" }
    $script:fail++
  }
}

function LocalIso([datetime]$dt) {
  $dt.ToString("yyyy-MM-dd'T'HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture)
}

$now = Get-Date
$startFree = LocalIso $now.AddMinutes(-1)
$endFree = LocalIso $now.AddMinutes(20)
$titleFree = "6b-free-" + [guid]::NewGuid().ToString("N").Substring(0, 8)

$payloadFree = @{
  title = $titleFree
  category = "break"
  start_at = $startFree
  end_at = $endFree
  planned_minutes = 21
  remaining_minutes = 21
  status = "scheduled"
} | ConvertTo-Json -Compress

$c = & $gw -Op "plan.upsert" -Payload $payloadFree
Check "create free block" ($c -match '"ok"\s*:\s*true') $c
if ($c -match '"id"\s*:\s*(\d+)') { $ids += [int]$Matches[1] }

$a = & $gw -Op "plan.apply_gate" -Payload '{}'
Check "plan.apply_gate ok" ($a -match '"ok"\s*:\s*true') $a
Check "free_until set" ($a -match '"free_until"\s*:\s*"[^"]+') $a
$endHour = $now.AddMinutes(20).ToString("HH")
Check "free_until hour matches" ($a -match $endHour) $a

foreach ($id in @($ids)) {
  & $gw -Op "plan.delete" -Payload "{`"id`":$id}" | Out-Null
}
$ids = @()

$titleStudy = "6b-study-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$payloadStudy = @{
  title = $titleStudy
  category = "study"
  start_at = (LocalIso $now.AddMinutes(-1))
  end_at = (LocalIso $now.AddMinutes(25))
  planned_minutes = 26
  remaining_minutes = 26
  status = "scheduled"
} | ConvertTo-Json -Compress

$c2 = & $gw -Op "plan.upsert" -Payload $payloadStudy
Check "create study block" ($c2 -match '"ok"\s*:\s*true') $c2
if ($c2 -match '"id"\s*:\s*(\d+)') { $ids += [int]$Matches[1] }

$a2 = & $gw -Op "plan.apply_gate" -Payload '{}'
Check "plan.apply_gate study ok" ($a2 -match '"ok"\s*:\s*true') $a2
Check "study apply returns applied" ($a2 -match '"applied"') $a2

foreach ($id in $ids) {
  $d = & $gw -Op "plan.delete" -Payload "{`"id`":$id}"
  Check "cleanup delete $id" ($d -match '"ok"\s*:\s*true') $d
}

& $gw -Op "plan.apply_gate" -Payload '{}' | Out-Null

if ($fail -eq 0) {
  Write-Host ""
  Write-Host "Plan 6b smoke: all checks passed"
} else {
  Write-Host ""
  Write-Host "Plan 6b smoke: $fail failure(s)"
  exit 1
}
