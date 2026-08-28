# S-D/S-F-CLOSE-9 evidence: Mosaic product-draw free helper

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-9  
Prior: S-D/S-F-CLOSE-8 `dbab532b`

## Intent

**Ownership domain (single slice):** Mosaic product-draw free helper (ToolMosaic + ToolAutoMosaic).  
Collapse Preview/Export dual style-resolve+draw into sole free helper `ScreenshotAnnotationRenderMosaicLocal`.  
Document style product-read + `ScreenshotDrawMosaicAnnotationLocal`. Preview/Export only map local coords + clip + Host fallbacks.

Not helper-only: product dual Mosaic style-resolve+draw bodies deleted (early axis-aligned pass, rotated pass, pathMode==1 path, preview draw).

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview `drawMosaicPreview` style-resolve + draw | `ScreenshotAnnotationRenderMosaicLocal` |
| Export early axis-aligned Mosaic pass | same free helper |
| Export rotated Mosaic pass | same free helper |
| Export `applyMosaicPath` pathMode==1 | same free helper |

Product may still peek `pathMode` / angle for **dispatch/filter only** (not dual draw).

## Product-read / write contract

1. Free helper:  
   `ScreenshotAnnotationRenderMosaicLocal(pixels, w, h, hdc, document, ann, localStart, localEnd, localPoints, count, clipLocal, fallbackPen, mosaicStrength)`  
   - Resolve Mosaic style from Document by id  
   - Draw via `ScreenshotDrawMosaicAnnotationLocal`
2. Preview: `toLocal` + crop clip; call free helper  
3. Export: `relativeRect` / `relativePoint` + full-surface clip; call free helper  

## Touch paths

- `src/screenshot/render/AnnotationSpecialRenderer.h` — Mosaic free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export multi-pass

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Mosaic Preview/Export dual draw body | **0** |
| Mosaic product-draw free helper | **on** |
| free helpers (Geometry..Magnifier+Mosaic) | **on** |
| residual dual draw | Eraser/HighLight |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~112** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Mosaic product-draw collapse (incl. AutoMosaic). Multi Export pass sites collapsed. Next: Eraser/HighLight residual or S-A-CLOSE. Near 硬停 120. Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-10: Eraser/HighLight residual product-draw **or** S-A-CLOSE characterization. Prefer high-value only near 硬停. Ban micro-slices.
