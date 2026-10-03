<#
  P5a pipe smoke. Requires calt_enforcer running. Read-only ops are safe;
  mutating ops are gated behind -Mutate so this can be run on a live day.
#>
param([switch]$Mutate)

$ErrorActionPreference = "Stop"
$gw = Join-Path $PSScriptRoot "gateway_cmd.ps1"
$fail = 0

function Check($label, $json, $pattern) {
  if ($json -match $pattern) { "PASS  $label" }
  else { "FAIL  $label`n      got: $json"; $script:fail++ }
}

$s = & $gw -Op "day.status"
Check "day.status returns pass quota" $s '"passes_limit":\s*2'
Check "day.status returns credits"    $s '"reward_available":'

if ($Mutate) {
  $r = & $gw -Op "day.grant_pass" -Payload '{"confirm":"nope"}'
  Check "grant_pass rejects wrong phrase" $r '"error":\s*"confirm_required"'

  $r = & $gw -Op "reward.claim" -Payload '{"confirm":"nope"}'
  Check "reward.claim rejects wrong phrase" $r '"error":\s*"confirm_required"'

  $r = & $gw -Op "day.mark_event" -Payload '{"event":"bogus"}'
  Check "mark_event rejects unknown event" $r '"error":\s*"bad_payload"'
}

if ($fail -eq 0) { "`nP5a smoke: all checks passed" } else { "`nP5a smoke: $fail failure(s)"; exit 1 }
