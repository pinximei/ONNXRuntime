# Download stb_image.h (single-header image loader, MIT/public-domain).
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\stb"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$target = Join-Path $dest "stb_image.h"
if (Test-Path $target) {
    Write-Host "stb_image.h already present"
    exit 0
}

$url = "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
Write-Host "Downloading stb_image.h ..."
Invoke-WebRequest -Uri $url -OutFile $target
Write-Host "stb_image.h saved to $target"
