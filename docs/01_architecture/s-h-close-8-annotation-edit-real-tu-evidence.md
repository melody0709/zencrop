# S-H-CLOSE-8 evidence: AnnotationEdit real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-8  
Prior: S-H-CLOSE-7 `b0b76ac6`  
**User override of ADR-003 硬停 120** (2026-07-23 AskUserQuestion) authorized resume.

## Intent

**Ownership domain (single slice):** AnnotationEdit Host class-method `.inl` → real TU.  
Net-delete largest Screenshot class-method residual (~13 methods).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host methods live in real `.cpp` TU.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.AnnotationEdit.inl` class-method include | `OverlayWindowScreenshot.AnnotationEdit.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |

## Notes

- Timer constants `ScreenshotRefreshTimerId` / `ScreenshotRefreshFrameMs` duplicated as file-static in AnnotationEdit.cpp (same values as OverlayWindow.cpp; multi-TU visibility).
- Includes CropAdjustMath + ArrowGeometry for resize/magnifier helpers previously available only via umbrella TU.

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — drop AnnotationEdit include
- `CMakeLists.txt` — list AnnotationEdit.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **1** (umbrella ctor only; was 2) |
| Dashboard class-method `.inl` | **0** |
| AnnotationEdit real TU | **on** |
| Stage 2 code commits | **~121** (user override 硬停 120) |

## Granularity note

One domain: AnnotationEdit residual → real TU. Residual class-method surface: umbrella only. ColorPickerDialog free-helper body still umbrella-hosted. Next: ColorPicker body TU / umbrella shrink **or** S-E projection residual. Docs same commit. No pin.

## NEXT

S-H-CLOSE-9: ColorPicker body real TU + umbrella shrink toward residual class-method `.inl` **0**, **or** S-E projection member residual. Prefer high-value only under override budget.
