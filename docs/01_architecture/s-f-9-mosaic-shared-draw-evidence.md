# S-F-9 evidence: Mosaic shared draw + dual preview/export body delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-9  
Prior: S-F-8 `0a10f5c5`

## Intent

**Ownership domain (single slice):** Extract sole free helper `ScreenshotDrawMosaicAnnotationLocal` (rect/path/blur/rotated) and delete dual Mosaic draw bodies in live preview (`drawMosaicPreview`) and export (early axis pass + `drawRotatedMosaic` + `applyMosaicPath`).

## Deleted dual authority

| Legacy dual | Sole after |
|---|---|
| Render `drawMosaicPreview` (~147 lines) | `ScreenshotDrawMosaicAnnotationLocal` |
| Export early axis-aligned mosaic loop | same free helper |
| Export `drawRotatedMosaic` | same free helper |
| Export `applyMosaicPath` | same free helper |

Helper lives in `ScreenshotImageUtils` (already owns blur/stroke-mask primitives). GDI+ rotation inlined (no ArrowGeometry link dep).

## Touch paths

- `src/screenshot/ScreenshotImageUtils.h` / `.cpp` — pure sole Mosaic draw
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — dual body deleted; call sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — three dual bodies deleted; call sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Mosaic preview/export dual draw bodies | **0** |
| Stage 2 code commits | **~63** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: Mosaic shared draw + all dual body deletes. Not helper-only.

## NEXT

Shared-draw residual inventory: Geometry/Arrow/Pencil/Serial/Watermark/Text/Marker/HighLight/Mosaic DONE; Magnifier/BrokenLine already shared pre-S-F. Then Document product-read deepen / tool-group vertical under ADR-002. 合域强制.
