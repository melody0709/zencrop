# S-H-CLOSE-6 evidence: ToolbarInteraction real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-6  
Prior: S-H-CLOSE-5 `1da6b447`

## Intent

**Ownership domain (single slice):** ToolbarInteraction Host class-method `.inl` → real TU.  
Net-delete production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host methods live in real `.cpp` TU.  
Promote `ShowScreenshotColorPickerDialog` from file-static to external linkage so multi-TU Host can call ColorPicker free helper still hosted in residual `.inl`.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.ToolbarInteraction.inl` class-method include | `OverlayWindowScreenshot.ToolbarInteraction.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |
| `ShowScreenshotColorPickerDialog` file-static | external free helper (still body in ColorPickerDialog.inl residual) |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — **deleted**
- `src/screenshot/overlay/OverlayWindowScreenshot.ColorPickerDialog.inl` — promote dialog free helper external linkage
- `src/screenshot/OverlayWindowScreenshot.inl` — drop ToolbarInteraction include
- `CMakeLists.txt` — list ToolbarInteraction.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **3** (was 4) |
| Dashboard class-method `.inl` | **0** |
| ToolbarInteraction real TU | **on** |
| Stage 2 code commits | **~119** (ADR-003 硬停 120 final) |

## Granularity note

One domain: sixth Screenshot Host class-method residual → real TU (3 methods + ColorPicker free-helper multi-TU promote). Residual: ToolbarRender / AnnotationEdit / ColorPickerDialog / umbrella. Near 硬停 120 — prefer residual inventory or last high-value knife only. Docs same commit. No pin.

## NEXT

S-H residual ToolbarRender/AnnotationEdit/ColorPicker **or** residual inventory docs near 硬停. Prefer high-value only. Ban micro-slices. ADR-003 硬停 120 final.
