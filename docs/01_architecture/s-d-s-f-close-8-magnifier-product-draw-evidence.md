# S-D/S-F-CLOSE-8 evidence: Magnifier product-draw free helper

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-8  
Prior: S-D/S-F-CLOSE-7 `3da70cfb`

## Intent

**Ownership domain (single slice):** Magnifier product-draw free helper.  
Collapse Preview/Export dual style-resolve+draw into sole free helper `ScreenshotAnnotationRenderMagnifierLocal`.  
Document style product-read + `ScreenshotDrawMagnifierLocal`. Preview/Export only map dest/source rects + source pixel buffer + Host fallbacks.

Not helper-only: product dual Magnifier style-resolve+draw bodies deleted. Source-pixel policy (eraseMark) remains product-side (needs Host runtime buffer).

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview Magnifier style-resolve + draw | `ScreenshotAnnotationRenderMagnifierLocal` |
| Export Magnifier style-resolve + draw | same free helper |

## Product-read / write contract

1. Free helper:  
   `ScreenshotAnnotationRenderMagnifierLocal(hdc, document, ann, localDest, localSource, sourcePixels, w, h, fallbackPen, fallbackRadius, fallbackMag)`  
   - Resolve Magnifier style from Document  
   - Color via `ScreenshotPresetColorLocal` when not custom  
   - Draw via `ScreenshotDrawMagnifierLocal`
2. Preview/Export: map rects; choose source pixels (frozen / scene snapshot / erase buffer); call free helper  
3. Product still peeks `eraseMark` for source-pixel policy only (not dual draw authority)

## Touch paths

- `src/screenshot/render/AnnotationSpecialRenderer.h` — Magnifier free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview drawMagnifier
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export drawMagnifier

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Magnifier Preview/Export dual draw body | **0** |
| Magnifier product-draw free helper | **on** |
| free helpers (Geometry..Serial+Magnifier) | **on** |
| residual dual draw | Mosaic/Eraser/HighLight |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~111** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Magnifier product-draw collapse. Source-pixel policy stays product (Host buffer). Next: Mosaic/Eraser/HighLight residual or S-A-CLOSE. Near 硬停 120 — prefer high-value only. Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-9: Mosaic/Eraser/HighLight residual product-draw **or** S-A-CLOSE characterization. Prefer residual inventory if multi-tool too wide for 1 knife near 硬停. Ban micro-slices.
