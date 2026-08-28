# S-H-CLOSE-2 evidence: Surface real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-2  
Prior: S-H-CLOSE-1 `d4288c50`

## Intent

**Ownership domain (single slice):** Surface Host class-method `.inl` → real TU.  
Net-delete production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host method lives in real `.cpp` TU.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.Surface.inl` class-method include | `OverlayWindowScreenshot.Surface.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.Surface.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.Surface.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — drop Surface include
- `CMakeLists.txt` — list Surface.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **7** (was 8) |
| Dashboard class-method `.inl` | **0** |
| Surface real TU | **on** |
| Stage 2 code commits | **~115** (ADR-003 硬停 120 final) |

## Granularity note

One domain: second Screenshot Host class-method residual → real TU. Next: Export/Settings/AnnotationRender or residual S-A/S-E. Near 硬停 120 — prefer high-value only. Docs same commit. No pin.

## NEXT

S-H-CLOSE-3: next medium Screenshot class-method `.inl` → real TU (Export ~546 LOC). Prefer high-value only near 硬停. Ban micro-slices.
