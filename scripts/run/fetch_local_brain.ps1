# Download Qwen2.5-1.5B-Instruct Q4_K_M (~1.12 GB) and the llama.cpp CPU server.
# Focus starts llama-server on 127.0.0.1 when both files are present.
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$models = Join-Path $root "data\models"
$tools = Join-Path $root "tools\llama"
New-Item -ItemType Directory -Force -Path $models, $tools | Out-Null

$gguf = Join-Path $models "qwen2.5-1.5b-instruct-q4_k_m.gguf"
if (-not (Test-Path $gguf)) {
  Write-Host "Downloading Qwen2.5-1.5B-Instruct Q4_K_M (~1.12 GB)..."
  curl.exe -L --fail --retry 3 -o $gguf "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf"
} else {
  Write-Host "Model already present: $gguf"
}

$server = Join-Path $tools "llama-server.exe"
if (-not (Test-Path $server)) {
  $zip = Join-Path $env:TEMP "llama-b10976-bin-win-cpu-x64.zip"
  Write-Host "Downloading llama.cpp b10976 Windows CPU build..."
  curl.exe -L --fail --retry 3 -o $zip "https://github.com/ggml-org/llama.cpp/releases/download/b10976/llama-b10976-bin-win-cpu-x64.zip"
  Expand-Archive -Force -Path $zip -DestinationPath $tools
  Get-ChildItem -Path $tools -Recurse -Filter "llama-server.exe" | Select-Object -First 1 | ForEach-Object {
    if ($_.FullName -ne $server) {
      Copy-Item $_.FullName $server -Force
      Copy-Item (Join-Path $_.DirectoryName "*") $tools -Force
    }
  }
}
if (-not (Test-Path $server)) {
  throw "llama-server.exe was not found after unzip. Look in tools\llama."
}
Write-Host ""
Write-Host "Ready. Open CALT Focus. It loads the GGUF on 127.0.0.1:8099."
Write-Host "Check: http://127.0.0.1:8765/api/brain/status"
Write-Host "Another GGUF: set CALT_LLM_GGUF to its path before starting Focus."
