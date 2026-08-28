# S-H-CLOSE-5 evidence: Settings real TU

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-5  
Prior: S-H-CLOSE-4 `330e2a10`

## Intent

**Ownership domain (single slice):** Settings Host class-method `.inl` → real TU.  
Net-delete production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host methods live in real `.cpp` TU.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.Settings.inl` class-method include | `OverlayWindowScreenshot.Settings.cpp` real TU |
| Umbrella include from `OverlayWindowScreenshot.inl` | CMake lists real `.cpp` |

## Notes

- `kScreenshotMosaicStrengthMaxLocal` duplicated as local static constexpr in Settings.cpp (same value 28 as `ScreenshotImageUtils.cpp` / umbrella residual). Toolbar residual still uses umbrella constant until those TUs convert.

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.cpp` — **new** real TU
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — drop Settings include
- `CMakeLists.txt` — list Settings.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **4** (was 5) |
| Dashboard class-method `.inl` | **0** |
| Settings real TU | **on** |
| Stage 2 code commits | **~118** (ADR-003 硬停 120 final) |

## Granularity note

One domain: fifth Screenshot Host class-method residual → real TU (11 methods). Residual: ToolbarRender / ToolbarInteraction / AnnotationEdit / umbrella. Near 硬停 120 — prefer high-value only. Docs same commit. No pin.

## NEXT

S-H residual Toolbar/AnnotationEdit (large) **or** residual inventory docs near 硬停. Prefer high-value only. Ban micro-slices.
