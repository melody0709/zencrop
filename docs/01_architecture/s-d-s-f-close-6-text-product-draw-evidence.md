# S-D/S-F-CLOSE-6 evidence: Text non-edit product-draw free helper

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-6  
Prior: S-D/S-F-CLOSE-5 `7d6391a8`

## Intent

**Ownership domain (single slice):** Text non-edit product-draw free helper.  
Collapse Preview/Export `drawText` non-edit style-resolve+draw into sole free helper `ScreenshotAnnotationRenderTextLocal`.  
Document style product-read + `ScreenshotDrawTextAnnotationLocal`. Preview/Export only map localRect + placeholder/fallback font.  
Editing overlays (selection/caret) remain Preview Host-only after free helper.

Not helper-only: product dual Text non-edit draw bodies deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview Text non-edit style-resolve + draw | `ScreenshotAnnotationRenderTextLocal` |
| Export Text non-edit style-resolve + draw | same free helper |

## Product-read / write contract

1. Free helper:  
   `ScreenshotAnnotationRenderTextLocal(hdc, document, ann, localRect, emptyPlaceholder, fallbackFontFamily)`  
   - Resolve Text draw style from Document by id  
   - Color via `ScreenshotPresetColorLocal` when not custom  
   - Font size/family fallbacks  
   - Draw via `ScreenshotDrawTextAnnotationLocal`  
   - Returns text rect (for Preview editing overlays)
2. Preview: map localRect; call free helper; if editing, keep selection/caret Host overlays  
3. Export: map relative rect; call free helper (no editing)

## Touch paths

- `src/screenshot/render/AnnotationTextRenderer.h` — **new** free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview drawText → free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export drawText → free helper

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Text non-edit Preview/Export dual draw body | **0** |
| Text product-draw free helper | **on** |
| Geometry+Arrow+Marker+Pencil+BrokenLine free helpers | **on** (CLOSE-2..5) |
| AnnotationRenderContext | **on** (CLOSE-1) |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~109** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Text non-edit product-draw collapse. Editing overlays intentionally Host residual. Next: Watermark/Serial free helpers or registry. Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-7: Watermark/Serial product-draw free helpers **or** residual inventory + registry. Prefer Watermark+Serial 合域. Ban micro-slices.
