# Stage3 3-A-3 — ocr_ui↛screenshot reverse cycle edges delete

Date: 2026-07-23  
Package: Stage 3 **3-A dependency cycle break**  
Slice: 3-A-3 screenshot ↔ ocr ui reverse  
Prior: 3-A-2 `02fccf8b`；3-A-1 `f46aed94`  
Code HEAD: this commit

## Intent

Delete **ocr_ui → screenshot** reverse edges that closed the screenshot↔ocr_ui cycle:

| Before | After |
|---|---|
| OCR UI includes `screenshot/ScreenshotUtils.h` for clipboard | **0** |
| `Screenshot::CopyTextToClipboard` / `CopyBitmapToClipboard` from OCR UI | core `CopyTextToClipboard` / `CopyBitmapToClipboard` |
| clipboard implementation sole under screenshot package | sole under `core/ClipboardUtils` |

**Dependency direction after:** screenshot may still call OCR UI (progress/dashboard session glue — AppHost/3-F later); ocr_ui ↛ screenshot.

## Ownership domain

Platform clipboard sole in core. OCR UI no longer depends on screenshot package for clipboard.

## Landed

| Item | Path |
|---|---|
| `core/ClipboardUtils.h` / `.cpp` | text + bitmap (DIB/DIBV5/PNG) clipboard |
| CMake | register ClipboardUtils.cpp |
| OCR UI consumers | Result/History/Blocks/ImagePreview → core |
| Dead includes deleted | OcrDashboardWindow.cpp / Messages.cpp |
| Screenshot wrappers | `Screenshot::CopyTextToClipboard` → core; bitmap keeps HDROP-enhanced path for screenshot capture |

## Deleted dual authority / cycle edges

| Edge | Status |
|---|---|
| ocr_ui → screenshot/ScreenshotUtils | **0** |
| `rg ScreenshotUtils` under `src/ocr/ui` | **0** |

## Residual

| Group | Residual |
|---|---|
| screenshot → ocr_ui | ON (ScreenshotSession → Progress/Dashboard; AppHost/3-F) |
| engine → net | ON (one-way after 3-A-1) |
| batch → document | ON (one-way after 3-A-2) |

All three GOAL §6 **bidirectional cycles broken** (each has reverse direction 0). Remaining edges are one-way allowed composition.

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 68/68
rg 'ScreenshotUtils' src/ocr/ui   → 0
```

## Ban check

- Same commit: land core clipboard + delete ocr_ui screenshot includes + rewire call sites
- Not helper-only; real cycle reverse deleted
- hermetic green

## NEXT

3-A package residual inventory (one-way edges) → 3-B Settings hygiene / 3-F AppHost for screenshot→ocr_ui session glue.
