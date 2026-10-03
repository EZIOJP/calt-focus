<#
  Phase 6a planner gateway smoke. Requires calt_enforcer running.
  Read-only by default; -Mutate creates then deletes a throwaway block.
#>
param([switch]$Mutate)

$ErrorActionPreference = "Stop"
$gw = Join-Path $PSScriptRoot "gateway_cmd.ps1"
$fail = 0

function Check($label, $json, $pattern) {
  if ($json -match $pattern) { "PASS  $label" }
  else { "FAIL  $label`n      got: $json"; $script:fail++ }
}

$from = [DateTime]::UtcNow.AddDays(-7).ToString("yyyy-MM-dd'T'00:00:00'Z'", [Globalization.CultureInfo]::InvariantCulture)
$to = [DateTime]::UtcNow.AddDays(14).ToString("yyyy-MM-dd'T'00:00:00'Z'", [Globalization.CultureInfo]::InvariantCulture)

$s = & $gw -Op "plan.list" -Payload ("{`"from`":`"$from`",`"to`":`"$to`}")
Check "plan.list returns ok" $s '"ok":\s*true'
Check "plan.list returns blocks array" $s '"blocks":\s*\['

$r = & $gw -Op "routine.list" -Payload '{}'
Check "routine.list returns ok" $r '"ok":\s*true'
Check "routine.list returns routines array" $r '"routines":\s*\['

$bad = & $gw -Op "plan.get" -Payload '{"id":0}'
Check "plan.get rejects id=0" $bad '"error":\s*"bad_payload"'

if ($Mutate) {
  $title = "6a-smoke-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
  $start = [DateTime]::UtcNow.AddHours(2).ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
  $createPayload = "{`"title`":`"$title`",`"category`":`"study`",`"start_at`":`"$start`",`"duration_minutes`":25}"
  $c = & $gw -Op "plan.upsert" -Payload $createPayload
  Check "plan.upsert create ok" $c '"ok":\s*true'
  Check "plan.upsert returns block title" $c ([regex]::Escape($title))

  if ($c -match '"id":\s*(\d+)') {
    $id = $Matches[1]
    $u = & $gw -Op "plan.upsert" -Payload ("{`"id`":$id,`"title`":`"$title-upd`"}")
    Check "plan.upsert update ok" $u '"ok":\s*true'
    Check "plan.upsert update title" $u ([regex]::Escape("$title-upd"))

    $d = & $gw -Op "plan.delete" -Payload ("{`"id`":$id}")
    Check "plan.delete ok" $d '"ok":\s*true'
  } else {
    "FAIL  could not parse created block id`n      got: $c"
    $script:fail++
  }
}

if ($fail -eq 0) { "`nPlan 6a smoke: all checks passed" } else { "`nPlan 6a smoke: $fail failure(s)"; exit 1 }
