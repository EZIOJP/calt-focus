# Send one native-messaging request to calt_msg_host.exe and print the reply.
#
#   powershell -File scripts\desktop_tracker\run\msg_host_cmd.ps1 -Json '{"type":"get_mode","url":"https://youtube.com"}'
#
# Same stdio framing Edge uses (4-byte little-endian length prefix), so this
# exercises the real Gate path without a browser.
param(
  [Parameter(Mandatory = $true)][string]$Json,
  [string]$Exe
)

$ErrorActionPreference = 'Stop'
if (-not $Exe) {
  $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
  $Exe = Join-Path $repo 'backend\calt_msg_host\build\calt_msg_host.exe'
}
if (-not (Test-Path $Exe)) { Write-Error "missing $Exe - run build\build_calt_msg_host.bat"; exit 1 }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($psi)

$payload = [Text.Encoding]::UTF8.GetBytes($Json)
$stdin = $proc.StandardInput.BaseStream
$stdin.Write([BitConverter]::GetBytes([int]$payload.Length), 0, 4)
$stdin.Write($payload, 0, $payload.Length)
$stdin.Flush()
$stdin.Close()

$out = $proc.StandardOutput.BaseStream
$lenBuf = New-Object byte[] 4
if ($out.Read($lenBuf, 0, 4) -ne 4) { Write-Error 'no reply header'; exit 3 }
$len = [BitConverter]::ToInt32($lenBuf, 0)
$body = New-Object byte[] $len
$got = 0
while ($got -lt $len) {
  $n = $out.Read($body, $got, $len - $got)
  if ($n -le 0) { break }
  $got += $n
}
$proc.WaitForExit(3000) | Out-Null
[Text.Encoding]::UTF8.GetString($body, 0, $got)
