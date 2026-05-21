# Download a prebuilt ONNX Runtime CPU package and unpack to third_party/onnxruntime
# Usage: powershell -ExecutionPolicy Bypass -File scripts\fetch_onnxruntime.ps1
$ErrorActionPreference = "Stop"

$ortVersion = "1.20.1"
$pkgName    = "onnxruntime-win-x64-$ortVersion"
$url        = "https://github.com/microsoft/onnxruntime/releases/download/v$ortVersion/$pkgName.zip"

$root       = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root "third_party"
$dest       = Join-Path $thirdParty "onnxruntime"
$zipPath    = Join-Path $thirdParty "$pkgName.zip"

if (Test-Path (Join-Path $dest "include\onnxruntime_cxx_api.h")) {
    Write-Host "ONNX Runtime already present at $dest"
    exit 0
}

New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null

Write-Host "Downloading $url ..."
Invoke-WebRequest -Uri $url -OutFile $zipPath

Write-Host "Extracting ..."
$tmpExtract = Join-Path $thirdParty "_ort_tmp"
if (Test-Path $tmpExtract) { Remove-Item -Recurse -Force $tmpExtract }
Expand-Archive -Path $zipPath -DestinationPath $tmpExtract -Force

if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
Move-Item -Path (Join-Path $tmpExtract $pkgName) -Destination $dest

Remove-Item -Recurse -Force $tmpExtract
Remove-Item -Force $zipPath

Write-Host "ONNX Runtime installed at $dest"
