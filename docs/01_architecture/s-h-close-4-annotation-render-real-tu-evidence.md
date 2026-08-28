# S-H-CLOSE-4 evidence: AnnotationRender real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-4  
Prior: S-H-CLOSE-3 `a5bb594a`

## Intent

**Ownership domain (single slice):** AnnotationRender Host class-method `.inl` → real TU.  
Net-delete production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host method lives in real `.cpp` TU.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.AnnotationRender.inl` class-method include | `OverlayWindowScreenshot.AnnotationRender.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — drop AnnotationRender include
- `CMakeLists.txt` — list AnnotationRender.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **5** (was 6) |
| Dashboard class-method `.inl` | **0** |
| AnnotationRender real TU | **on** |
| Stage 2 code commits | **~117** (ADR-003 硬停 120 final) |

## Granularity note

One domain: fourth Screenshot Host class-method residual → real TU. Residual: Settings / ToolbarRender / ToolbarInteraction / AnnotationEdit / umbrella. Near 硬停 120 — prefer high-value only. Docs same commit. No pin.

## NEXT

S-H-CLOSE-5: next residual Screenshot class-method `.inl` → real TU (Settings ~1097 LOC) **or** residual S-A/S-E. Prefer high-value only near 硬停. Ban micro-slices.
