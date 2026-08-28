# S-H-CLOSE-3 evidence: Export real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-3  
Prior: S-H-CLOSE-2 `3b67d542`

## Intent

**Ownership domain (single slice):** Export Host class-method `.inl` → real TU.  
Net-delete production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host method lives in real `.cpp` TU.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.Export.inl` class-method include | `OverlayWindowScreenshot.Export.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.Export.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — drop Export include
- `CMakeLists.txt` — list Export.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **6** (was 7) |
| Dashboard class-method `.inl` | **0** |
| Export real TU | **on** |
| Stage 2 code commits | **~116** (ADR-003 硬停 120 final) |

## Granularity note

One domain: third Screenshot Host class-method residual → real TU. Next: AnnotationRender/Settings or residual S-A/S-E. Near 硬停 120 — prefer high-value only. Docs same commit. No pin.

## NEXT

S-H-CLOSE-4: next medium Screenshot class-method `.inl` → real TU (AnnotationRender ~1074 LOC). Prefer high-value only near 硬停. Ban micro-slices.
