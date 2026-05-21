# ONNX Runtime Image Classifier (C++)

A small, fast image classifier built on **ONNX Runtime** + **EfficientNet-Lite4**.

| Metric                      | Value                              |
| --------------------------- | ---------------------------------- |
| Model                       | EfficientNet-Lite4 (ImageNet-1k)   |
| Model size (FP32)           | ~49 MB                             |
| Model size (INT8 quantized) | ~13 MB                             |
| Top-1 accuracy (FP32)       | ~80.4 %                            |
| Top-1 accuracy (INT8)       | ~77.6 %                            |
| CPU latency, single image   | ~30–60 ms on a modern x64 desktop  |
| Runtime DLL footprint       | ~10 MB (onnxruntime.dll, CPU only) |
| External dependencies       | none beyond the C++17 runtime      |

The whole redistributable folder (`dist/`) ends up around **60 MB with FP32** or **~25 MB with the INT8 model** — entirely self-contained, no Python, no CUDA.

## Build

Requirements: Windows + Visual Studio 2022 (or any toolchain with C++17 + CMake ≥ 3.20) and PowerShell.

```powershell
# 1. fetch dependencies into third_party/  (≈ 50 MB download)
powershell -ExecutionPolicy Bypass -File scripts\fetch_onnxruntime.ps1
powershell -ExecutionPolicy Bypass -File scripts\fetch_stb.ps1

# 2. fetch model into models/
powershell -ExecutionPolicy Bypass -File scripts\fetch_model.ps1
# or, for the smaller / faster INT8 build:
# powershell -ExecutionPolicy Bypass -File scripts\fetch_model.ps1 -Quantized

# 3. configure + build (Release)
cmake -S . -B build -A x64
cmake --build build --config Release
```

The resulting binary is at `build\Release\classify.exe`. `onnxruntime.dll` is copied next to it automatically.

## Run

```powershell
build\Release\classify.exe path\to\image.jpg
```

Sample output:

```
Model:  models/efficientnet-lite4-11.onnx  (loaded in 142 ms)
Image:  cat.jpg  (inference 38 ms)
Top-5:
  1. [ 281] tabby, tabby cat                            72.41%
  2. [ 282] tiger cat                                   18.05%
  3. [ 285] Egyptian cat                                 6.92%
  4. [ 287] lynx, catamount                              0.94%
  5. [ 904] window screen                                0.21%
```

CLI flags:

| Flag          | Default                              | Meaning                              |
| ------------- | ------------------------------------ | ------------------------------------ |
| `--model PATH`| `models/efficientnet-lite4-11.onnx`  | switch to e.g. the INT8 model        |
| `--top K`     | `5`                                  | how many predictions to print        |
| `--threads N` | `0` (auto)                           | intra-op threads                     |
| `--bench N`   | `0` (off)                            | run inference N times, report mean   |

Example — benchmark with the INT8 model:

```powershell
build\Release\classify.exe --model models\efficientnet-lite4-11-int8.onnx --bench 50 cat.jpg
```

## Redistribution

```powershell
cmake --install build --config Release --prefix dist
```

This drops a self-contained `dist/` with `classify.exe`, `onnxruntime.dll`, and `models/`.

## 中文说明

* 模型：**EfficientNet-Lite4**（ImageNet 1000 类分类）。
* 三个目标：体积小（INT8 模型 13 MB）、速度快（CPU 上 30–60 ms / 张）、准确率高（Top-1 约 80%）。
* 全程零 Python / 零 CUDA 依赖，编译产物拷过去就能跑。
* 切换 INT8 模型：`fetch_model.ps1 -Quantized` 然后 `--model models\efficientnet-lite4-11-int8.onnx`，体积减到 ~13 MB，CPU 推理再快 1.5–2x。

## Project layout

```
ONNXRuntime/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp              # CLI driver
│   ├── classifier.h / .cpp   # ORT session wrapper + preprocessing
│   └── imagenet_labels.cpp   # 1000-class label table (auto-generated)
├── scripts/
│   ├── fetch_onnxruntime.ps1 # downloads prebuilt ORT CPU 1.20.1
│   ├── fetch_stb.ps1         # downloads stb_image.h
│   ├── fetch_model.ps1       # downloads the EfficientNet-Lite4 ONNX
│   └── gen_labels.ps1        # regenerates imagenet_labels.cpp
├── assets/
│   └── labels_map.txt        # canonical ImageNet labels (source of truth)
├── models/                   # populated by fetch_model.ps1
└── third_party/              # populated by fetch_* scripts (gitignored)
```

## Why these choices

* **EfficientNet-Lite4** sits at the sweet spot for "small + fast + accurate" on CPU. MobileNetV3 is smaller but ~12 points less accurate; ResNet50 is more accurate per-parameter but ~2× the size and latency.
* **CPU execution provider** keeps the redistributable footprint at ~10 MB of DLLs and avoids CUDA/DirectML/driver issues at deploy time. For a single-image classifier, CPU is already in the tens of milliseconds.
* **stb_image** decodes JPEG/PNG/BMP/GIF in a single header file — no libjpeg / libpng linkage churn.

If you later want GPU acceleration, swap the CPU package for `onnxruntime-win-x64-gpu` or `onnxruntime-directml` and add `opts_.AppendExecutionProvider_CUDA(...)` / `_DML(...)` in `classifier.cpp`.
