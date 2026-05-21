# Download stb_image.h (single-header image loader, MIT/public-domain).
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\stb"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$files = @("stb_image.h", "stb_image_write.h")
foreach ($f in $files) {
    $target = Join-Path $dest $f
    if (Test-Path $target) {
        Write-Host "$f already present"
        continue
    }
    $url = "https://raw.githubusercontent.com/nothings/stb/master/$f"
    Write-Host "Downloading $f ..."
    Invoke-WebRequest -Uri $url -OutFile $target
    Write-Host "$f saved to $target"
}
