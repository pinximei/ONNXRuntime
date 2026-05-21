# UI Detector — Screenshot Element Recognizer

Detects interactable UI elements in a screenshot and reads the text inside each one. Pure C++ + ONNX Runtime, no OpenCV / no Qt / no Python at runtime.

## What it does

| Stage | Model | Source |
|---|---|---|
| **Find UI elements** | [Microsoft OmniParser icon_detect](https://huggingface.co/onnx-community/OmniParser-icon_detect_640x640) (YOLOv8, 12 MB) | Detects all interactable regions in the screenshot (buttons, icons, tabs, inputs…) |
| **Read text — Chinese model** | PaddleOCR PP-OCRv4 recognition (10 MB) | Reads CJK + mixed content |
| **Read text — English model** | PaddleOCR PP-OCRv3 English recognition (9 MB) | Reads pure-ASCII content, avoids the "DDD → 品園" misread you get when feeding Latin to a Chinese-only model |

Each detection box is run through **both** OCR models and the better result is selected based on character composition and confidence.

## Highlights

- **Pure C++17 + ONNX Runtime**, single executable, ~170 KB. No GUI framework installed at build time except MFC (Windows SDK ships it with VS2022).
- Two front-ends:
  - **`detect.exe`** — CLI: takes an image path, prints text/positions, writes an annotated PNG + JSON.
  - **`detect_gui.exe`** — MFC desktop GUI: open image, see boxes in image pane + table on the right, click a row to highlight the corresponding box.
- Background thread keeps the GUI responsive while inference runs.

## Build

Prerequisites: the main project's third-party assets (ONNX Runtime + stb headers). From the main project root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch_onnxruntime.ps1
powershell -ExecutionPolicy Bypass -File scripts\fetch_stb.ps1
```

Then in this example folder:

```powershell
cd examples\ui_detector
powershell -ExecutionPolicy Bypass -File scripts\fetch_model.ps1
powershell -ExecutionPolicy Bypass -File scripts\fetch_ocr.ps1

cmake -S . -B build -A x64
cmake --build build --config Release
```

You get:
- `build\Release\detect.exe`     — CLI
- `build\Release\detect_gui.exe` — MFC GUI
- `build\Release\onnxruntime.dll` — runtime (copied automatically)

## Run

### CLI

```powershell
.\build\Release\detect.exe path\to\screenshot.png
```

Output:
- Console: top-10 boxes by score + every box that produced text
- `<screenshot>_annotated.png` — original image with colored boxes
- `<screenshot>_boxes.json`    — list of `{x,y,w,h,score,text,text_score}`

Useful flags:

| Flag | Default | Meaning |
|---|---|---|
| `--conf F` | 0.05 | Detection confidence threshold |
| `--iou F`  | 0.50 | NMS IoU threshold |
| `--min-text-score F` | 0.50 | Drop OCR results below this confidence |
| `--no-ocr` | off | Detection only (~75 ms vs ~900 ms) |
| `--threads N` | 0 | ORT intra-op threads (0=auto) |

### GUI

```powershell
.\build\Release\detect_gui.exe
```

(Run it from `examples\ui_detector\` so `models\` is reachable.)

Click **Open image...**, pick a screenshot, watch the table populate. Click any row → that box flashes yellow in the image view.

## Performance

Measured on a 1920×1080 desktop screenshot, CPU only:

| Stage | Time |
|---|---|
| Detector load    | ~90 ms |
| OCR load (cn+en) | ~250 ms |
| Detection (one image) | ~75 ms |
| OCR (36 boxes, ~23 ms/box) | ~830 ms |
| **End-to-end** | **~1 s** |

## Known limitations

- **Identical-looking characters**: PP-OCRv4 mobile occasionally swaps look-alike Chinese characters under small font sizes — `未→末`, `投→技`, `版→贩`. Upgrading to PaddleOCR's server-size model (~90 MB) cuts this by ~3-5 % but slows OCR ~2×.
- **Favicon contamination**: When a detection box surrounds a tab label *and* its favicon, the OCR may read the favicon as a character (` 探索未..`, `C DeepSeek`). True fix would be to run a text-detection pass *inside* each detected box.
- **No widget-type classification**: OmniParser only outputs one class ("interactive element"). To distinguish "button" vs "input" vs "checkbox", you would need a second classifier or rule-based heuristics on top.

## File map

```
examples/ui_detector/
├── CMakeLists.txt              # builds ui_core.lib + detect.exe + detect_gui.exe
├── README.md
├── scripts/
│   ├── fetch_model.ps1         # downloads OmniParser icon_detect ONNX (12 MB)
│   └── fetch_ocr.ps1           # downloads PP-OCRv4 cn rec + en rec + dicts (~20 MB)
├── src/
│   ├── detector.{h,cpp}        # YOLOv8 inference + NMS + area filter
│   ├── recognizer.{h,cpp}      # PaddleOCR rec inference + CTC decoding
│   ├── ocr_pipeline.{h,cpp}    # runs both rec models, picks better result
│   ├── image_io.{h,cpp}        # PNG load (stb_image) + annotate + JSON writer
│   ├── main.cpp                # CLI entry point
│   └── gui/
│       ├── App.{h,cpp}         # CWinApp
│       ├── MainFrame.{h,cpp}   # split-pane main window
│       ├── ImageView.{h,cpp}   # custom CWnd, double-buffered painter
│       ├── Worker.{h,cpp}      # background thread + PostMessage
│       ├── Resource.{h,rc}     # version info; UI is built in code
│       ├── stdafx.{h,cpp}      # precompiled header for MFC
│       └── …
└── models/                     # populated by fetch_*.ps1 (gitignored)
```
