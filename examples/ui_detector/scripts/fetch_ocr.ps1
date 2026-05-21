# Download PP-OCRv4 recognition ONNX + character dictionary.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot   # examples\ui_detector
$dest = Join-Path $root "models"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$files = @(
    @{
        name = "ppocrv4_rec.onnx"
        urls = @(
            "https://huggingface.co/SWHL/RapidOCR/resolve/main/PP-OCRv4/ch_PP-OCRv4_rec_infer.onnx",
            "https://hf-mirror.com/SWHL/RapidOCR/resolve/main/PP-OCRv4/ch_PP-OCRv4_rec_infer.onnx"
        )
    },
    @{
        name = "ppocr_keys_v1.txt"
        urls = @(
            "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/release/2.7/ppocr/utils/ppocr_keys_v1.txt",
            "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/ppocr_keys_v1.txt"
        )
    },
    @{
        name = "en_ppocrv3_rec.onnx"
        urls = @(
            "https://huggingface.co/SWHL/RapidOCR/resolve/main/PP-OCRv3/en_PP-OCRv3_rec_infer.onnx",
            "https://hf-mirror.com/SWHL/RapidOCR/resolve/main/PP-OCRv3/en_PP-OCRv3_rec_infer.onnx"
        )
    },
    @{
        name = "en_dict.txt"
        urls = @(
            "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/release/2.7/ppocr/utils/en_dict.txt",
            "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/en_dict.txt"
        )
    }
)

foreach ($f in $files) {
    $target = Join-Path $dest $f.name
    if (Test-Path $target) {
        $sz = [math]::Round((Get-Item $target).Length / 1KB, 1)
        Write-Host "$($f.name) already present ($sz KB)"
        continue
    }
    $ok = $false
    foreach ($u in $f.urls) {
        try {
            Write-Host "Downloading $($f.name) from $u ..."
            Invoke-WebRequest -Uri $u -OutFile $target -UserAgent "Mozilla/5.0"
            # Health check: dict files can be small (>=100 B), ONNX should be > 100 KB.
            $min = if ($f.name.EndsWith(".onnx")) { 100000 } else { 100 }
            if ((Get-Item $target).Length -gt $min) { $ok = $true; break }
            Remove-Item $target
        } catch {
            Write-Host "  failed: $($_.Exception.Message)"
            Remove-Item $target -ErrorAction SilentlyContinue
        }
    }
    if (-not $ok) { throw "Could not download $($f.name) from any source." }
    $sz = [math]::Round((Get-Item $target).Length / 1KB, 1)
    Write-Host "Saved $($f.name) ($sz KB)"
}

Write-Host ""
Write-Host "OCR assets ready in $dest"
