# Download EfficientNet-Lite4 (ImageNet-1k, ONNX) from the onnx/models repository.
# - efficientnet-lite4-11.onnx       : FP32, ~49 MB, Top-1 ~80.4 %
# - efficientnet-lite4-11-int8.onnx  : INT8 quantized, ~13 MB, Top-1 ~77.6 % (smaller + faster on CPU)
# Pass -Quantized to grab the INT8 build instead of the FP32 one.

param(
    [switch]$Quantized
)

$ErrorActionPreference = "Stop"

$root      = Split-Path -Parent $PSScriptRoot
$modelsDir = Join-Path $root "models"
New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null

if ($Quantized) {
    $fileName = "efficientnet-lite4-11-int8.onnx"
    $url      = "https://github.com/onnx/models/raw/main/validated/vision/classification/efficientnet-lite4/model/efficientnet-lite4-11-int8.onnx"
} else {
    $fileName = "efficientnet-lite4-11.onnx"
    $url      = "https://github.com/onnx/models/raw/main/validated/vision/classification/efficientnet-lite4/model/efficientnet-lite4-11.onnx"
}

$dest = Join-Path $modelsDir $fileName
if (Test-Path $dest) {
    Write-Host "Model already present: $dest"
    exit 0
}

Write-Host "Downloading $url ..."
Invoke-WebRequest -Uri $url -OutFile $dest
$sizeMB = [math]::Round((Get-Item $dest).Length / 1MB, 1)
Write-Host "Saved $fileName ($sizeMB MB) to $modelsDir"
