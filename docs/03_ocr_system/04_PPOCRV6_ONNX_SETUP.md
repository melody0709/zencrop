# PP-OCRv6 Local ONNX Setup

更新时间: 2026-07-30

## Scope

`PP-OCRv6 Local` is a new CPU-only OCR backend. It does not replace the existing Windows OCR, PaddleOCR Cloud, PaddleOCR-VL 1.6 Local VLM, or PaddleOCR Doc pipeline.

Internal mode:

```json
"mode": "ppocrv6_onnx"
```

Current provider is fixed to CPU. The settings file keeps `ppocrv6Provider: "cpu"` so a later DirectML/CUDA phase can add providers without changing the mode name.

## Model Layout

Set `PP-OCRv6 Local` -> `Model Dir` to the root folder that contains `small` and/or `medium`:

```text
ocr/
  pp-ocrv6/
    small/
      det/
        inference.onnx
      rec/
        inference.onnx
        ppocrv6_rec_dict.txt
        manifest.json
    medium/
      det/
        inference.onnx
      rec/
        inference.onnx
        ppocrv6_rec_dict.txt
        manifest.json
```

Supported variants:

- `small`: default for CPU responsiveness.
- `medium`: higher accuracy, heavier load time and CPU cost.

Model sources (upstream repositories for reference):

- HuggingFace small det: <https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx>
- HuggingFace small rec: <https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx>
- HuggingFace medium det: <https://huggingface.co/PaddlePaddle/PP-OCRv6_medium_det_onnx>
- HuggingFace medium rec: <https://huggingface.co/PaddlePaddle/PP-OCRv6_medium_rec_onnx>
- ModelScope small det: <https://modelscope.cn/models/PaddlePaddle/PP-OCRv6_small_det_onnx>
- ModelScope small rec: <https://modelscope.cn/models/PaddlePaddle/PP-OCRv6_small_rec_onnx>
- ModelScope medium det: <https://modelscope.cn/models/PaddlePaddle/PP-OCRv6_medium_det_onnx>
- ModelScope medium rec: <https://modelscope.cn/models/PaddlePaddle/PP-OCRv6_medium_rec_onnx>

The product download catalog pins each artifact to both a HuggingFace commit hash and a ModelScope commit hash, each with a verified SHA-256. The downloader supports per-artifact URL fallback, and the download dialog provides a "Mirror" dropdown so users can choose which source to try first (HuggingFace or ModelScope). Both mirrors serve byte-identical content (same size and SHA-256). See [00_OCR_MODEL_DOWNLOAD.md](00_OCR_MODEL_DOWNLOAD.md) for the pinned commit hash table.

### One-click download

The MSI/Portable package does **not** bundle models. Select PP-OCRv6 Local, then use Settings -> OCR -> `Manage Models...` to install small or medium. The native manager downloads pinned ONNX artifacts, verifies exact size and SHA-256, copies the verified runtime dictionary template, and generates `manifest.json` in C++.

The GUI path does not invoke PowerShell, Python, PyYAML, or `export_ppocrv6_dict.py`. See [00_OCR_MODEL_DOWNLOAD.md](00_OCR_MODEL_DOWNLOAD.md) for the download and resume contract.

## Dictionary Export and CTC Contract

The shipped dictionary template was derived from the official recognition metadata and is shared by the pinned small/medium models. The following exporter remains a development verification path, not an installed-product dependency:

```powershell
python scripts/python/export_ppocrv6_dict.py ocr/pp-ocrv6/small/rec/inference.yml
python scripts/python/export_ppocrv6_dict.py ocr/pp-ocrv6/medium/rec/inference.yml
python scripts/python/test_ppocrv6_dict_export_contract.py
```

The script uses `yaml.safe_load` (not hand-rolled `strip()` parsing) and writes:

- `ppocrv6_rec_dict.txt` — **base** dictionary only (18708 characters, one per line, UTF-8)
- `manifest.json` — CTC contract (manifest_version 2)

Official `CTCLabelDecode` (`use_space_char=true`) contract for small/medium rec ONNX:

```text
base dictionary from inference.yml: 18708
append ASCII space at runtime:      +1
effective dictionary:               18709
blank index:                        0
ONNX output classes:                18710

class 0:         blank
class 1..18708:  base dictionary[0..18707]
class 18709:     ASCII space U+0020
```

Base dict also contains `U+3000 IDEOGRAPHIC SPACE` (index 1748). That is a normal character and is distinct from the trailing ASCII space class. Both must be preserved.

The C++ backend:

1. loads `ppocrv6_rec_dict.txt` as the base dict (no trim of line content);
2. appends ASCII space at runtime (`PPOcrV6Ctc::AppendOfficialSpaceChar`);
3. rejects ONNX outputs whose class count is not `effective_dict_size + 1`;
4. includes space tokens in CTC confidence (mean of kept non-blank tokens).

Legacy `manifest.dict_size` remains the **base** size (18708), not the effective size. Prefer `base_dict_size` / `effective_dict_size` / `expected_output_classes`.

## Settings

Visible settings (main OCR page + **PP-OCRv6 Options** dialog):

- `Model Dir`: PP-OCRv6 root folder.
- `Variant`: `small` or `medium`; named presets never change it.
- `Threads`: ONNX Runtime CPU intra-op thread count, clamped to `1..16`.
- `Preset`: named field packs in Options dialog (does not auto-rewrite on JSON load).
- `Side (px)`: detection `limit_side_len`, clamped to **`64..4096`** (official 3.7 uses `64`).
- `Type`: `min` (short side ≥ Side) or `max` (long side ≤ Side).
- `Pixel thresh %` / `Box conf %` / `Crop expand %`: DB postprocess knobs.
- `Min text score %`: recognition score threshold; `0` keeps all non-empty lines.
- `Rec batch (0=Auto)`: `0` → runtime Auto **6**; otherwise `1..8`.

Named presets (Options dialog; scheme 1 — **same model**, speed/quality via resize only):

| Preset | Type/Side | Det/Box/Unclip | Batch | Role |
|---|---|---|---:|---|
| **Balanced** (daily default) | min/64 | 20/45/140 | 1 | Native res for normal screenshots |
| **Quality** | min/320 | 20/45/140 | 1 | Mild upscale when short side &lt; 320 |
| **Fast** | max/1280 | 20/45/140 | 1 | Mild downscale when long side &gt; 1280 |
| **Official 3.7** | min/64 | 30/60/150 | 6 | PaddleX pipeline reference |
| Custom | (manual) | | | |

Presets **never** change `Variant` (small/medium stays on the main OCR page).
`min/64` is a short-side **floor**, not “detect at 64px”. Avoid `min/960` for daily packs — it force-upscales common crops (~3× slower).

JSON id: `balanced` | `quality` | `fast` | `official_37` | `custom`.
Legacy ids (`screenshot_balanced`, `screenshot_small_text`, `fast_cpu`, `document_official_37`) are recognized, but their saved Variant and knob values are preserved as **Custom** because the old packs had different semantics. Select a new named preset explicitly to adopt the scheme-1 values.
Manual knob edit → Custom. No doc orientation / unwarp / textline orientation.

Hidden settings:

- `ppocrv6Provider`: fixed `cpu` for this phase.
- `ppocrv6DetMaxSideLimit`: default `4000`.

Fixed runtime behavior:

- `engine`: native ONNX Runtime C API.
- Recognition crops are **sorted by width** and bucketed (max width ratio 2.0) before batching; results restore reading order via `sourceBoxIndex`.
- `use_doc_orientation_classify`: false.
- `use_doc_unwarping`: false.
- `use_textline_orientation`: false.
- `lang`: not exposed; PP-OCRv6 small/medium are multilingual models.

## Detection Postprocess

The backend now has two DBPostProcess paths:

- `opencv+clipper2`: enabled at build time when OpenCV is found. This follows the official PP-OCR DB path: threshold bitmap, `findContours`, `minAreaRect`, `box_score_fast`, Clipper2 polygon unclip, a second `minAreaRect`, and perspective crop via `getPerspectiveTransform`/`warpPerspective`.
- `fallback+clipper2`: used when OpenCV is not installed. This keeps the previous connected-components detector but uses Clipper2 for `unclip`, so normal builds still work and are more accurate than the old radial expansion.

The canonical `x64-release` preset requires OpenCV and prefers the repo-local `third_party\opencv` package. `build.bat` compiles one product target, then installs its exact executable, matching OpenCV DLLs, and external assets into `build\run\x64-release\`. A versioned ZIP is emitted under `build\packages\` only when `build.bat --package` is requested.

## Validation

Required files for `small`:

```text
small/det/inference.onnx
small/rec/inference.onnx
small/rec/ppocrv6_rec_dict.txt
```

Required files for `medium`:

```text
medium/det/inference.onnx
medium/rec/inference.onnx
medium/rec/ppocrv6_rec_dict.txt
```

If any required file is missing, OCR returns a clear error and the old OCR backends remain unaffected.

## CLI Smoke Test

For local verification, `ZenCrop.exe` has a hidden OCR test entry:

```powershell
build\run\x64-release\ZenCrop.exe --ocr-test tmp\sample.png --ocr-test-output tmp\ppocrv6_result.json --ocr-mode ppocrv6_onnx
```

Because `ZenCrop.exe` is a Windows GUI-subsystem executable, PowerShell does not wait for it when launched directly. For repeatable local validation, use:

```powershell
$p = Start-Process -FilePath .\build\run\x64-release\ZenCrop.exe -ArgumentList @(
  "--ocr-test", "tmp\sample.png",
  "--ocr-test-output", "tmp\ppocrv6_result.json",
  "--ocr-mode", "ppocrv6_onnx",
  "--ocr-test-repeat", "3"
) -Wait -PassThru -WindowStyle Hidden
$p.ExitCode
```

The command uses `%LOCALAPPDATA%\ZenCrop\settings.json` by default, so set:

```json
{
  "ocr": {
    "mode": "ppocrv6_onnx",
    "timeoutMs": 180000,
    "ppocrv6ModelDir": "D:\\path\\to\\ocr\\pp-ocrv6",
    "ppocrv6Variant": "small",
    "ppocrv6Provider": "cpu",
    "ppocrv6CpuThreads": 4,
    "ppocrv6RecBatchSize": 1,
    "ppocrv6DetLimitSideLen": 960,
    "ppocrv6DetLimitType": "min",
    "ppocrv6DetMaxSideLimit": 4000,
    "ppocrv6DetThreshPct": 20,
    "ppocrv6DetBoxThreshPct": 45,
    "ppocrv6DetUnclipRatioPct": 140,
    "ppocrv6RecScoreThreshPct": 0
  }
}
```

The JSON output contains:

```json
{
  "mode": "ppocrv6_onnx",
  "success": true,
  "elapsedMs": 500,
  "text": "...",
  "error": ""
}
```

Verified on 2026-06-28 with official small and medium ONNX models:

- `small`: success, about 440-500 ms on the local test image.
- `medium`: success, about 1030-1125 ms on the same local test image.
- Missing model directory: returns `PP-OCRv6 model directory does not exist.` instead of silently falling back to another OCR backend.

Repeat validation on 2026-06-29 after process-local session/dict cache, dynamic recognition width, and runtime rec batch support:

- Default `ppocrv6RecBatchSize=1`, `small`, repeat=3: success, 422 ms / 328 ms / 281 ms.
- Default `ppocrv6RecBatchSize=1`, `medium`, repeat=3: success, 1078 ms / 953 ms / 828 ms.
- `ppocrv6RecBatchSize=4`: verified successfully with both variants.
- Historical note (pre-2026-07-18 CTC space fix): outputs were incorrectly recorded as
  `ZenCropOCRTest\r\nPP-OCRv6Local123ABC\r\n中文识别本地CPU` because the decoder dropped
  class 18709 (ASCII space). That is **not** correct official output.
- After the 2026-07-18 CTC contract fix (base dict + runtime ASCII space, U+3000 preserved),
  the same sample is expected to keep English spaces, e.g.
  `ZenCrop OCR Test\r\nPP-OCRv6 Local 123 ABC\r\n中文识别本地CPU` (third line spaces depend on the model).
- Long-line smoke image `tmp\ppocrv6_e2e\long_line.png`: both variants run successfully with dynamic recognition width; short text keeps the default 320 recognition width; long text can expand up to 3200 because the official ONNX recognition model input width is dynamic.
- Rotated smoke image `tmp\ppocrv6_e2e\rotated_15.png`: rotated crop path runs successfully.

OpenCV 5.0.0 / Clipper2 validation on 2026-06-29:

- OpenCV 5.0.0 was downloaded from the official GitHub release via Scoop, then copied into the ignored repo-local dependency directory `third_party\opencv`; Scoop is not used by the build path.
- `rtk cmd /c build.bat`: passed with auto-detected `third_party\opencv`, producing both `ZenCrop_opencv.exe` and `ZenCrop_noopencv.exe`.
- Build linked `opencv_world500.lib` and copied only `opencv_world500.dll` to `build\` (about 80 MB); the debug `opencv_world500d.dll` is not copied.
- Import table check: `ZenCrop_opencv.exe` imports `opencv_world500.dll`; `ZenCrop_noopencv.exe` imports `WebView2Loader.dll` and `onnxruntime.dll` but not OpenCV.
- OpenCV 5 API note: `minAreaRect` and `getPerspectiveTransform` are declared by `opencv2/geometry/2d.hpp`; CMake requests `core`, `geometry`, and `imgproc`.
- Pre-fix sample timings remain historically valid; text content from those runs lacked ASCII spaces and must not be treated as the post-fix baseline.
- Dual-exe revalidation: `ZenCrop_opencv.exe` and `ZenCrop_noopencv.exe` both recognized the sample image successfully.
- The no-OpenCV source path still runs through `fallback+clipper2`, but the current `build.bat` expects OpenCV to be present so it can emit both requested comparison binaries. Without OpenCV, use a no-OpenCV-only build path or CMake with `ZENCROP_ENABLE_OPENCV_DBPOST=OFF`.

### CTC space fix validation (2026-07-18)

- Hermetic: `tests\build_and_run.bat test_ppocrv6_ctc_decode_contract` — synthetic `ZEN CROP 123`, U+3000, B/T/C layouts, wrong class count, space confidence.
- Dict export: `python scripts/python/test_ppocrv6_dict_export_contract.py` — small/medium YAML/TXT/manifest, U+3000 at index 1748, `expected_output_classes=18710`.
- Runtime smoke: multiengine contract with `ZENCROP_RUN_PPOCRV6_SMOKE=1` requires space-preserving text and blocks (rejects glued `ZENCROP123`).

### P1 options + batch plan validation (2026-07-18)

- Hermetic batch plan: `tests\build_and_run.bat test_ppocrv6_rec_batch_plan_contract` — width sort, ratio split, padded units, restore `sourceBoxIndex`, Auto batch=0→6.
- Hermetic presets: `tests\build_and_run.bat test_ppocrv6_preset_contract` — Official 3.7 pack Side=64 / 30/60/150 / batch 6; modelDir/threads untouched.
- Side clamp product range is **64..4096** (Settings load/save + Options dialog + engine `BuildConfig`).
- Rec batch `0` is Auto (runtime 6); recognition packs similar widths (max ratio 2.0) then restores reading order.

## Text-line Blocks Output (2026-07-18)

PP-OCRv6 Local now fills the shared `OcrOutput` block contract used by Dashboard Canvas overlay, History, and Batch writers. These are **text-line blocks**, not semantic document layout (title/table/image/formula).

### Contract

| Field | Value |
|---|---|
| `blocks[i].label` | always `text` |
| `blocks[i].source` | always `ppocrv6_onnx` |
| `blocks[i].content` | recognition line text |
| `blocks[i].confidence` | recognition score (not detection score) |
| `blocks[i].polygon` | original-image 4-point det box (or empty → bbox fallback) |
| `blocks[i].bbox` | original-image axis-aligned rect |
| `blocks[i].id` | `page_1:ppocrv6_line_<sourceBoxIndex+1>` (stable within one OCR output) |
| `blocks[i].order` | dense `1..N` in reading order |
| `bboxes` / `bboxClasses` | parallel to `blocks`, class always `text` |
| `text` | accepted lines joined with Windows `\r\n` |

Acceptance gate (single source of truth for text **and** blocks):

```text
!text.empty() && recScore >= recScoreThresh
```

Identity rules:

- Each recognition crop carries `sourceBoxIndex`; crop/preprocess skips never pair by parallel array index.
- Batch success requires `results.size() == inputs.size()`; fewer **or** more results → whole batch discarded, then per-item single fallback.
- Single fallback accepts only `results.size() == 1`.
- IDs are **output-local**: stable for History/JSON round-trip of that result; not promised across re-OCR, model, or DBPostProcess path changes.

### Presentation (Dashboard / layout preview)

Pure `source=ppocrv6_onnx` + `label=text` snapshots use **TextLine** presentation:

- normal fill alpha `0` (outline-first); hover/selected `60`/`90`
- order badge hidden until hover/selected/Reading Order mode
- bbox-overlap quality issue disabled (rotated lines share large AABBs); confidence/small/huge/edited issues remain
- Cloud/Doc semantic layout presentation is unchanged
- Style is resolved from the **current blocks snapshot**, never from Settings OCR route

Empty Layout toggle copy is engine-neutral: “No visual block data for this image.”

### Tests

```powershell
# Hermetic assembler + presentation (no model required)
tests\build_and_run.bat test_ppocrv6_block_contract

# PDF page N write/reload + layout preview pixel contract (no model required)
tests\build_and_run.bat test_ppocrv6_writer_contract

# Multi-engine availability + optional real-model smoke with block geometry checks
$env:ZENCROP_PPOCRV6_MODEL_DIR = (Resolve-Path "ocr\pp-ocrv6").Path
$env:ZENCROP_PPOCRV6_VARIANT = "small"   # or medium
$env:ZENCROP_RUN_PPOCRV6_SMOKE = "1"
$env:ZENCROP_REQUIRE_PPOCRV6 = "1"
# optional: $env:ZENCROP_PPOCRV6_REC_BATCH_SIZE = "4"
tests\build_and_run.bat test_ocr_multiengine_runtime_contract
```

Verified on 2026-07-18:

- Hermetic `test_ppocrv6_block_contract`: identity/threshold/CRLF/TextLine presentation, NaN-quad clear, duplicate `sourceBoxIndex` rejection, `OcrLayoutBlocksForPage` page-2 remap — all passed.
- Writer `test_ppocrv6_writer_contract`:
  - `WritePdfPageSuccess(page=2)` + `ScanJobs` reload → `pageIndex==1`, `page_2:ppocrv6_line_*`, exact bbox, polygon points (epsilon), `confidence≈0.91`.
  - `WriteLayoutArtifacts` pixels → TextLine no interior fill / edge stroke / no order badge; semantic control has fill + dark badge.
- Real smoke `small` (batch 1) + `medium` (batch 4): `smoke passed (with blocks)` — non-empty blocks, dense order, `page_1:ppocrv6_line_*` IDs, CRLF rebuild matches `output.text`.
- Product `build.bat`: `Build Success` → `build\run\x64-release\ZenCrop.exe`.

Remaining manual release checks: boarding-pass four-state Dashboard screenshots (normal/hover/selected/Reading Order) and 1000+ line paint/hit-test pressure timing.

Pure assembler header: `src/ocr/engine/PPOcrV6BlockAssembler.h`  
Presentation policy: `src/ocr/OcrBlockPresentation.h`  
Writer contracts: `tests/test_ppocrv6_writer_contract.cpp`

## GGUF Note

Do not use GGUF for this backend. The GGUF route is for VLM-style OCR/document understanding and remains covered by the existing PaddleOCR-VL 1.6 Local / Doc path. PP-OCRv6 text detection and recognition are best matched to ONNX Runtime for this CPU-only local backend.
