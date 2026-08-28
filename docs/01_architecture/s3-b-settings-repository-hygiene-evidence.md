# Stage3 3-B — Settings repository hygiene (no ocr/batch/UI/net/engine)

Date: 2026-07-23  
Package: Stage 3 **3-B Settings repository**  
Prior: 3-A-3 `9f023e3a`  
Code HEAD: this commit

## Intent

Gate criterion: **Settings repository 无 UI/net/engine include** (and no ocr/batch).

| Before | After |
|---|---|
| `Settings.h` → `ocr/batch/PdfRenderOptions.h` | **0** |
| raster bound constants dual in batch header | sole `core/RasterBoundOptions.h` |
| Settings.cpp UI/net/engine includes | already **0** (pre-existing; AppHost path seed only) |

## Ownership domain

Raster bound constants/clamps sole in core. Settings repository no longer depends on ocr/batch package.

## Landed

| Item | Path |
|---|---|
| `core/RasterBoundOptions.h` | `kDefaultPdfMaxPixelEdge` / `MaxMegapixels` + clamps |
| `Settings.h` | include core RasterBoundOptions only |
| `PdfRenderOptions.h` | reuses core constants; deletes duplicate clamp defs |

## Deleted dual authority

| Edge | Status |
|---|---|
| Settings repository → ocr/batch | **0** |
| Settings repository → UI/net/engine | **0** (Settings.cpp) |

## Residual (explicit non-blockers for 3-B)

1. **SettingsDialog** still includes OcrEngine / LlamaServer / Network / HotkeyEdit — UI layer, not repository
2. **Settings.cpp** dual-reads AppHost for settings path seed (composition root; Gate allows AppHost, bans UI/net/engine)
3. Static include-edge regression guard script still optional (3-A residual / Stage3 Gate attach)

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 68/68
rg 'ocr/batch|OcrEngine|Network|LlamaServer' src/core/Settings.{h,cpp} → 0
```

## NEXT

3-C Preview JS / 3-D Engine-Document / 3-E BatchWriter / 3-F AppHost (screenshot→ocr_ui session glue + composition root).
