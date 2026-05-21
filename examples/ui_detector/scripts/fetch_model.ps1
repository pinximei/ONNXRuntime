# Download Microsoft OmniParser icon_detect (YOLOv8) ONNX from the
# community-converted onnx-community/OmniParser-icon_detect_640x640 repo.
# Default: FP32 (12.3 MB). Pass -Quantized for INT8 (3.3 MB).
param(
    [switch]$Quantized
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot   # examples\ui_detector
$dest = Join-Path $root "models"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

if ($Quantized) {
    $name = "omniparser-icon_detect-640-int8.onnx"
    $url  = "https://huggingface.co/onnx-community/OmniParser-icon_detect_640x640/resolve/main/onnx/model_int8.onnx"
} else {
    $name = "omniparser-icon_detect-640.onnx"
    $url  = "https://huggingface.co/onnx-community/OmniParser-icon_detect_640x640/resolve/main/onnx/model.onnx"
}
$target = Join-Path $dest $name

if (Test-Path $target) {
    $sz = [math]::Round((Get-Item $target).Length / 1MB, 2)
    Write-Host "$name already present ($sz MB)"
    exit 0
}

Write-Host "Downloading $name from $url ..."
Invoke-WebRequest -Uri $url -OutFile $target -UserAgent "Mozilla/5.0"
$sz = [math]::Round((Get-Item $target).Length / 1MB, 2)
Write-Host "Saved $name ($sz MB) to $dest"
