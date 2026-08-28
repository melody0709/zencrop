# S-F-4 evidence: Watermark shared draw + dual helpers delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-4  
Prior: S-F-3 `e73270a0`

## Intent

**Ownership domain (single slice):** Extract sole free helpers:
- `ScreenshotReplaceWatermarkTimeFormatsLocal`
- `ScreenshotDrawWatermarkAnnotationLocal`

Delete dual `replaceTimeFormats` + `drawWatermark` bodies in live preview (`AnnotationRender.inl`) and export (`Export.inl`).

## Deleted dual authority

| Legacy dual | Sole after |
|---|---|
| Render/Export `replaceTimeFormats` lambdas | `ScreenshotReplaceWatermarkTimeFormatsLocal` |
| Render/Export `drawWatermark` GDI+ bodies | `ScreenshotDrawWatermarkAnnotationLocal` |

Export passes `cropLocal={0,0,width,height}` (export surface is crop-relative). Preview passes editor crop in screen-local coords.

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h` / `.cpp` — pure sole helpers + Watermark draw
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — dual lambdas/bodies deleted; call sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — dual lambdas/bodies deleted; call sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Watermark preview/export dual draw bodies | **0** |
| Stage 2 code commits | **~58** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: Watermark draw + time-format helper used by Watermark. Not helper-only.

## NEXT

Continue dual-draw collapse (Text/Marker/HighLight) under ADR-002. 合域强制.
