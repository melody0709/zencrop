# S-D/S-F-CLOSE-4 evidence: Marker product-draw free helper

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-4  
Prior: S-D/S-F-CLOSE-3 `f008aa14`

## Intent

**Ownership domain (single slice):** Marker product-draw free helper.  
Collapse Preview `drawOne` Marker branch + Export Marker branch dual style-resolve+draw into sole free helper `ScreenshotAnnotationRenderMarkerLocal`.  
Document style product-read + `ScreenshotDrawMarkerAnnotationLocal` (pixel buffer). Preview/Export only map points → local coords + fallback pen width.

Not helper-only: product dual Marker draw bodies deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview Marker style-resolve + draw body | `ScreenshotAnnotationRenderMarkerLocal` |
| Export Marker style-resolve + draw body | same free helper |

Export pathMode==2 rect coord map stays product-side (relativeRect); free helper owns style+draw.

## Product-read / write contract

1. Free helper:  
   `ScreenshotAnnotationRenderMarkerLocal(pixels, w, h, document, ann, localStart, localEnd, localPoints, count, fallbackPenWidth)`  
   - Resolve Marker draw style from Document by id  
   - Color via `ScreenshotPresetColorLocal` when not custom  
   - Angle via `IsRotatableGeometryScreenshotAnnotationLocal`  
   - Draw via `ScreenshotDrawMarkerAnnotationLocal`
2. Preview: `toLocal` map; call free helper  
3. Export: `relativePoint` / pathMode==2 `relativeRect` map; call free helper  

## Touch paths

- `src/screenshot/render/AnnotationGeometryRenderer.h` — Marker free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview Marker → free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export Marker → free helper

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Marker Preview/Export dual draw body | **0** |
| Marker product-draw free helper | **on** |
| Geometry+Arrow product-draw free helpers | **on** (CLOSE-2/3) |
| AnnotationRenderContext | **on** (CLOSE-1) |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~107** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Marker product-draw collapse. Pencil/BrokenLine next CLOSE slices. Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-5: Pencil/BrokenLine product-draw free helper **or** multi-tool registry. Prefer Pencil next. Ban micro-slices.
