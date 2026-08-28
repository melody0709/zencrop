# S-D/S-F-CLOSE-3 evidence: Arrow product-draw free helper

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-3  
Prior: S-D/S-F-CLOSE-2 `103332d7`

## Intent

**Ownership domain (single slice):** Arrow product-draw free helper.  
Collapse Preview `drawOne` Arrow branch + Export Arrow branch dual style-resolve+draw into sole free helper `ScreenshotAnnotationRenderArrowLocal`.  
Document style product-read + `ScreenshotDrawArrowShapeLocal`. Preview/Export only map points → local HDC coords + fallback pen width.

Not helper-only: product dual Arrow draw bodies deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview Arrow style-resolve + draw body | `ScreenshotAnnotationRenderArrowLocal` |
| Export Arrow style-resolve + draw body | same free helper |

## Product-read / write contract

1. Free helper:  
   `ScreenshotAnnotationRenderArrowLocal(hdc, document, ann, localStart, localEnd, localPoints, count, fallbackPenWidth)`  
   - Resolve Geometry/Arrow draw style from Document by id  
   - Color via `ScreenshotPresetColorLocal` when not custom  
   - Multi-point path when `localPointCount >= 2`; else start/end  
   - Draw via `ScreenshotDrawArrowShapeLocal`
2. Preview: `toLocal` map points; call free helper  
3. Export: `relativePoint` map points; call free helper  

## Touch paths

- `src/screenshot/render/AnnotationGeometryRenderer.h` — Arrow free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview Arrow → free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export Arrow → free helper

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Arrow Preview/Export dual draw body | **0** |
| Arrow product-draw free helper | **on** |
| Geometry product-draw free helper | **on** (CLOSE-2) |
| AnnotationRenderContext | **on** (CLOSE-1) |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~106** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Arrow product-draw collapse. Marker/Pencil/etc. next CLOSE slices. Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-4: Marker product-draw free helper (same pattern) **or** multi-tool registry. Prefer Marker next. Ban micro-slices.
