# Stage3 3-F — AppHost composition-root OCR progress facade

Date: 2026-07-23  
Package: Stage 3 **3-F AppHost composition root**  
Slice: screenshot↛ocr_ui session glue  
Prior: 3-B `6bc7bf59`；3-A-3 `9f023e3a`  
Code HEAD: this commit

## Intent

Delete remaining **screenshot → ocr_ui** include edge:

| Before | After |
|---|---|
| `ScreenshotSession.cpp` → `OcrProgressWindow.h` / `OcrDashboardWindow.h` | **0** |
| Session calls Dashboard/Progress static APIs directly | `ShowAppOcrProgress` / `CloseAppOcrProgress` |
| Progress show logic dual in main hotkey path + session | sole composition-root free functions in `main.cpp` |

**Composition root:** only `main.cpp` (AppHost entry) includes OCR UI headers for progress facade.

## Ownership domain

OCR progress show/hide sole at composition root. Features call AppMessages free functions; no feature→feature OCR UI includes.

## Landed

| Item | Path |
|---|---|
| `ShowAppOcrProgress` / `CloseAppOcrProgress` decls | `src/AppMessages.h` |
| Implementation | `src/main.cpp` (composition root) |
| ScreenshotSession rewire | drops OCR UI includes; uses facade |
| Hotkey OCR + WM_APP close paths | use facade |

## Deleted dual authority / cycle edges

| Edge | Status |
|---|---|
| screenshot → OcrDashboardWindow / OcrProgressWindow | **0** |
| `rg OcrDashboardWindow\|OcrProgressWindow` under `src/screenshot` | **0** (comment only) |

## Residual (explicit)

1. main.cpp still includes OCR UI (composition root — required)
2. Complete/Fail ExternalOcr still composition-root only (main WM_APP_OCR_RESULT)
3. 3-C Preview JS / 3-D Engine-Document / 3-E BatchWriter residual packages
4. Static cycle regression guard script optional for Stage3 Gate

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 68/68
rg 'OcrDashboardWindow|OcrProgressWindow' src/screenshot → 0 product includes
```

## NEXT

3-C / 3-D / 3-E residual or Stage3 Gate evidence (cycles broken + Settings clean + AppHost facade).
