# Download Piper (C++ / ONNX, rhasspy 2023.11.14-2) and two English voices.
# Qwen speaks with en_GB-alan-medium. Normal speaks with en_US-lessac-medium.
# Focus runs piper.exe. No Python.
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$tools = Join-Path $root "tools\piper"
$voices = Join-Path $root "data\models\piper"
New-Item -ItemType Directory -Force -Path $tools, $voices | Out-Null

$piper = Join-Path $tools "piper.exe"
$nested = Join-Path $tools "piper\piper.exe"
if (-not (Test-Path $piper) -and -not (Test-Path $nested)) {
  $zip = Join-Path $env:TEMP "piper_windows_amd64.zip"
  Write-Host "Downloading Piper Windows build..."
  curl.exe -L --fail --retry 3 -o $zip "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip"
  Expand-Archive -Force -Path $zip -DestinationPath $tools
}
if (-not (Test-Path $piper) -and -not (Test-Path $nested)) {
  throw "piper.exe was not found after unzip. Look in tools\piper."
}

function Get-Voice($name, $rel) {
  $onnx = Join-Path $voices "$name.onnx"
  $json = Join-Path $voices "$name.onnx.json"
  $base = "https://huggingface.co/rhasspy/piper-voices/resolve/main/$rel/$name"
  if (-not (Test-Path $onnx)) {
    Write-Host "Downloading $name (~60 MB)..."
    curl.exe -L --fail --retry 3 -o $onnx "$base.onnx"
  }
  if (-not (Test-Path $json)) {
    curl.exe -L --fail --retry 3 -o $json "$base.onnx.json"
  }
}

Get-Voice "en_GB-alan-medium" "en/en_GB/alan/medium"
Get-Voice "en_US-lessac-medium" "en/en_US/lessac/medium"

Write-Host ""
Write-Host "Ready. Open CALT Focus. Qwen speaks with Piper."
Write-Host "Check: http://127.0.0.1:8765/api/speech/status"
