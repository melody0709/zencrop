# S-H-CLOSE-7 evidence: ToolbarRender real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-7  
Prior: S-H-CLOSE-6 `197df2b1`

## Intent

**Ownership domain (single slice):** ToolbarRender Host class-method `.inl` → real TU.  
Net-delete production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host method lives in real `.cpp` TU.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.ToolbarRender.inl` class-method include | `OverlayWindowScreenshot.ToolbarRender.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — drop ToolbarRender include
- `CMakeLists.txt` — list ToolbarRender.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **2** (was 3) — AnnotationEdit + umbrella ctor |
| free-helper residual `.inl` | ColorPickerDialog + ActionCatalog include |
| Dashboard class-method `.inl` | **0** |
| ToolbarRender real TU | **on** |
| Stage 2 code commits | **~120** (ADR-003 硬停 120 final) |

## Granularity note

One domain: seventh Screenshot Host class-method residual → real TU.  
**ADR-003 硬停 120 final reached.** Further Stage2 *code* commits require user ADR / budget decision.  
Residual after hard stop: AnnotationEdit class-method `.inl`; ColorPickerDialog free-helper `.inl`; umbrella ctor+includes; S-A/S-E/S-G residual; projection member; §11.5 full NOT closed. Docs same commit. No pin.

## NEXT

**Hard stop.** Residual inventory docs-only OK. No more Stage2 code knives without ADR/user override of ADR-003 硬停 120.
