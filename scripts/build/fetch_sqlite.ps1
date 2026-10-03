# Download SQLite amalgamation into backend/calt_enforcer/third_party/sqlite
$ErrorActionPreference = "Stop"
# scripts/build -> repo root
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Out = Join-Path $Repo "backend\calt_enforcer\third_party\sqlite"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
if (Test-Path (Join-Path $Out "sqlite3.c")) {
  Write-Host "Already present: $Out\sqlite3.c"
  exit 0
}
$Zip = Join-Path $env:TEMP "sqlite-amalgamation.zip"
$Url = "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
Write-Host "Downloading $Url ..."
Invoke-WebRequest -Uri $Url -OutFile $Zip
$Extract = Join-Path $env:TEMP "sqlite-amalgamation-extract"
if (Test-Path $Extract) { Remove-Item -Recurse -Force $Extract }
Expand-Archive -Path $Zip -DestinationPath $Extract -Force
$Inner = Get-ChildItem $Extract -Directory | Select-Object -First 1
Copy-Item (Join-Path $Inner.FullName "sqlite3.c") (Join-Path $Out "sqlite3.c") -Force
Copy-Item (Join-Path $Inner.FullName "sqlite3.h") (Join-Path $Out "sqlite3.h") -Force
Write-Host "OK: $Out\sqlite3.c"
