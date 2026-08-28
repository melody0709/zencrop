# S-D/S-F-CLOSE-7 evidence: Watermark/Serial product-draw free helpers

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-7  
Prior: S-D/S-F-CLOSE-6 `c781ae49`

## Intent

**Ownership domain (single slice):** Watermark + Serial product-draw free helpers (合域 special tools).  
Collapse Preview/Export dual style-resolve+draw into sole free helpers.  
Document style product-read + draw APIs. Preview/Export only map crop/localRect + font fallbacks.

Not helper-only: product dual draw bodies deleted for both tools.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview/Export Watermark style-resolve + draw | `ScreenshotAnnotationRenderWatermarkLocal` |
| Preview/Export Serial style-resolve + draw | `ScreenshotAnnotationRenderSerialLocal` |

## Product-read / write contract

1. Watermark:  
   `ScreenshotAnnotationRenderWatermarkLocal(hdc, document, ann, cropLocal, fallbackFontSize, fallbackFontFamily)`  
   - Resolve Watermark style from Document  
   - Time-token replace via `ScreenshotReplaceWatermarkTimeFormatsLocal`  
   - Draw via `ScreenshotDrawWatermarkAnnotationLocal`
2. Serial:  
   `ScreenshotAnnotationRenderSerialLocal(hdc, document, ann, localRect)`  
   - Resolve Serial style from Document  
   - Draw via `ScreenshotDrawSerialAnnotationLocal`
3. Preview: crop/screen-local map; Export: crop-relative / relativeScreenRect map

## Touch paths

- `src/screenshot/render/AnnotationSpecialRenderer.h` — **new** free helpers
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview → free helpers
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export → free helpers

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Watermark/Serial Preview/Export dual draw body | **0** |
| Watermark+Serial product-draw free helpers | **on** |
| Geometry+Arrow+Marker+Pencil+BrokenLine+Text free helpers | **on** (CLOSE-2..6) |
| AnnotationRenderContext | **on** (CLOSE-1) |
| residual dual draw | Mosaic/Eraser/Magnifier/HighLight |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~110** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Watermark+Serial special-tool product-draw collapse. Next: Mosaic/Eraser/Magnifier/HighLight or registry. Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-8: Mosaic/Eraser/Magnifier/HighLight residual product-draw **or** registry table **or** S-A-CLOSE. Prefer inventory residual then multi-tool close. Ban micro-slices.
