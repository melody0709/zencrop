# S-D/S-F-CLOSE-10 evidence: Eraser/HighLight product-draw free helpers

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-10  
Prior: S-D/S-F-CLOSE-9 `55347a5f`

## Intent

**Ownership domain (single slice):** Eraser + HighLight product-draw free helpers (合域 special tools residual).  
Collapse Preview/Export dual style-resolve+draw into sole free helpers.  
Document style product-read + restore/draw APIs. Preview/Export only map local coords + source-pixel policy + batch mask call.

Not helper-only: product dual Eraser restore bodies + dual HighLight style-fill bodies deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview `applyEraser` + `restoreFrozenPixel`/`restoreEraserBrush`/`pointInEraserRect` | `ScreenshotAnnotationRenderEraserLocal` |
| Export `applyEraser` + `restoreBasePixel`/`restoreEraserBrush`/`pointInEraserRect` | same free helper |
| Preview HighLight style-resolve + `ScreenshotHighLightRenderInfo` fill | `ScreenshotAnnotationMakeHighLightRenderInfo` |
| Export HighLight style-resolve + fill | same free helper |

Product still peeks Eraser `pathMode` for point-map dispatch only (not dual draw).  
Source-pixel policy (frozen frame vs magnifierErase base) stays product-side.  
HighLight batch mask draw (`ScreenshotDrawHighLightMaskLocal`) stays product (needs full list).

## Product-read / write contract

1. Eraser free helper:  
   `ScreenshotAnnotationRenderEraserLocal(dest, w, h, source, sw, sh, cropLocal, document, ann, localStart, localEnd, localPoints, count, fallbackPen)`  
   - Resolve Eraser style from Document by id  
   - pathMode==1: brush restore along points  
   - else: rect/ellipse restore (rotated)  
   - restore dest pixel = `0xFF000000 | (source & 0x00FFFFFF)` inside cropLocal
2. HighLight free helper:  
   `ScreenshotAnnotationMakeHighLightRenderInfo(out, document, ann, localRect, fallbackPen)`  
   - Resolve HighLight style from Document  
   - Fill `ScreenshotHighLightRenderInfo` (rect/color/width/opacity/stroke/ellipse/angle)  
   - Preview/Export collect batch + call `ScreenshotDrawHighLightMaskLocal`
3. Preview: frozen frame as source; screen→local map  
4. Export: magnifierErasePixels as source; relative map

## Touch paths

- `src/screenshot/render/AnnotationSpecialRenderer.h` — Eraser + HighLight free helpers
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Eraser Preview/Export dual restore body | **0** |
| HighLight Preview/Export dual style-fill body | **0** |
| Eraser+HighLight product-draw free helpers | **on** |
| free helpers (Geometry..Mosaic+Eraser+HighLight) | **on** |
| residual dual product-draw tool bodies | **0** (tool style+draw) |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~113** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Eraser+HighLight residual product-draw collapse (合域 special residual). Source-pixel policy + HighLight batch mask stay product. Product-draw free helpers for all tools ON. Next: registry table **or** S-A-CLOSE **or** S-E projection residual. Near 硬停 120 — prefer high-value only. Docs same commit. No pin.

## NEXT

S-D/S-F registry **or** S-A-CLOSE characterization **or** S-E projection residual. Prefer high-value only near 硬停. Ban micro-slices.
