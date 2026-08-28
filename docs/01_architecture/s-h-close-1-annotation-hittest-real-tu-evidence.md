# S-H-CLOSE-1 evidence: AnnotationHitTest real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-1  
Prior: S-D/S-F-CLOSE-10 `aa62f27c`

## Intent

**Ownership domain (single slice):** AnnotationHitTest Host class-method `.inl` → real TU.  
Net-delete production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host method lives in real `.cpp` TU.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.AnnotationHitTest.inl` class-method include | `OverlayWindowScreenshot.AnnotationHitTest.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — drop HitTest include
- `CMakeLists.txt` — list AnnotationHitTest.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **8** (was 9) |
| Dashboard class-method `.inl` | **0** |
| AnnotationHitTest real TU | **on** |
| Stage 2 code commits | **~114** (ADR-003 硬停 120 final) |

## Granularity note

One domain: first Screenshot Host class-method residual → real TU (D-I pattern). Next: more leaf/medium `.inl` (Surface/Export/Settings) or residual S-A / S-E. Near 硬停 120 — prefer high-value only. Docs same commit. No pin.

## NEXT

S-H-CLOSE-2: next leaf/medium Screenshot class-method `.inl` → real TU **or** S-A residual / S-E projection. Prefer high-value only near 硬停. Ban micro-slices.
