# Download Microsoft.Web.WebView2 NuGet into backend/calt_focus/third_party/webview2
$ErrorActionPreference = "Stop"
# scripts/build -> repo root
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Out = Join-Path $Repo "backend\calt_focus\third_party\webview2"
$Inc = Join-Path $Out "include"
$X64 = Join-Path $Out "x64"
New-Item -ItemType Directory -Force -Path $Inc | Out-Null
New-Item -ItemType Directory -Force -Path $X64 | Out-Null

if ((Test-Path (Join-Path $Inc "WebView2.h")) -and (Test-Path (Join-Path $X64 "WebView2Loader.dll"))) {
  Write-Host "Already present: $Out"
  exit 0
}

$Ver = "1.0.2903.40"
$Zip = Join-Path $env:TEMP "Microsoft.Web.WebView2.$Ver.nupkg.zip"
$Url = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$Ver/microsoft.web.webview2.$Ver.nupkg"
Write-Host "Downloading $Url ..."
Invoke-WebRequest -Uri $Url -OutFile $Zip
$Extract = Join-Path $env:TEMP "webview2-nuget-$Ver"
if (Test-Path $Extract) { Remove-Item -Recurse -Force $Extract }
Expand-Archive -Path $Zip -DestinationPath $Extract -Force

$SrcInc = Join-Path $Extract "build\native\include"
$SrcDll = Join-Path $Extract "build\native\x64\WebView2Loader.dll"
if (-not (Test-Path (Join-Path $SrcInc "WebView2.h"))) {
  throw "WebView2.h missing in NuGet package"
}
if (-not (Test-Path $SrcDll)) {
  throw "WebView2Loader.dll (x64) missing in NuGet package"
}

if (Test-Path $Inc -PathType Leaf) {
  Remove-Item -Force $Inc
  New-Item -ItemType Directory -Force -Path $Inc | Out-Null
}

Copy-Item (Join-Path $SrcInc "*") $Inc -Force
Copy-Item $SrcDll (Join-Path $X64 "WebView2Loader.dll") -Force
$SrcLib = Join-Path $Extract "build\native\x64\WebView2Loader.dll.lib"
if (Test-Path $SrcLib) {
  Copy-Item $SrcLib (Join-Path $X64 "WebView2Loader.dll.lib") -Force
}
Write-Host "OK: $Out"
