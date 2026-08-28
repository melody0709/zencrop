# S-F-5 evidence: GDI+ rect rotation sole + dual lambda delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-5  
Prior: S-F-4 `a4b23ada`

## Intent

**Ownership domain (single slice):** Extract sole free helper `ScreenshotApplyGdiplusRectRotationLocal` and delete dual `applyRectRotation` lambdas in live preview (`AnnotationRender.inl`) and export (`Export.inl`).

## Deleted dual authority

| Legacy dual | Sole after |
|---|---|
| Render/Export `applyRectRotation` GDI+ lambdas | `ScreenshotApplyGdiplusRectRotationLocal` |

Call sites (text bg rotation, mosaic rotation, highlight rotation) now pure sole.

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h` / `.cpp` — pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — dual lambda deleted; call sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — dual lambda deleted; call sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| GDI+ applyRectRotation dual lambdas | **0** |
| Stage 2 code commits | **~59** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: GDI+ rect rotation free helper + both dual lambda deletes + all call sites. Not helper-only.

## NEXT

Continue dual-draw collapse (Text non-edit core / Marker / HighLight) under ADR-002. 合域强制.
