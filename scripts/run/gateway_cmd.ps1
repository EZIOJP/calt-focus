# Send one command to the calt_enforcer gateway named pipe and print the reply.
#
#   powershell -File scripts\desktop_tracker\run\gateway_cmd.ps1 -Op softland.set_enabled -Payload '{"enabled":true}'
#   powershell -File scripts\desktop_tracker\run\gateway_cmd.ps1 -Op arm.set -PayloadFile .\arm.json
#
# The enforcer must be running (console or service). No Python / :8000 involved.
param(
  [Parameter(Mandatory = $true)][string]$Op,
  [string]$Payload = '{}',
  [string]$PayloadFile = '',
  [int]$TimeoutMs = 4000
)

$ErrorActionPreference = 'Stop'
if ($PayloadFile) {
  if (-not (Test-Path -LiteralPath $PayloadFile)) { throw "PayloadFile not found: $PayloadFile" }
  $Payload = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $PayloadFile)).Trim()
}
$id = [guid]::NewGuid().ToString()
$req = '{"v":1,"id":"' + $id + '","op":"' + $Op + '","payload":' + $Payload + '}'
$bytes = [Text.Encoding]::UTF8.GetBytes($req)

# The enforcer serves one pipe instance from its tick loop and recycles it after
# each reply, so a connect can break once — retry like the Focus client does.
$last = ''
foreach ($attempt in 1..3) {
  try {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(
      '.', 'calt_enforcer_cmd', [System.IO.Pipes.PipeDirection]::InOut, [System.IO.Pipes.PipeOptions]::None)
    $pipe.Connect($TimeoutMs)
    $pipe.ReadMode = [System.IO.Pipes.PipeTransmissionMode]::Message
    $pipe.Write($bytes, 0, $bytes.Length)
    $pipe.Flush()
    $buf = New-Object byte[] 65536
    $read = $pipe.Read($buf, 0, $buf.Length)
    $pipe.Dispose()
    if ($read -gt 0) {
      [Text.Encoding]::UTF8.GetString($buf, 0, $read)
      exit 0
    }
    $last = 'empty reply'
  } catch {
    $last = $_.Exception.Message
  }
  Start-Sleep -Milliseconds 200
}

Write-Error "gateway command failed after 3 attempts: $last"
exit 1
