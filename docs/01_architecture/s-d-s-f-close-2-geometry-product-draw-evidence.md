# S-D/S-F-CLOSE-2 evidence: Geometry product-draw free helper

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-2  
Prior: S-D/S-F-CLOSE-1 `9499d011`

## Intent

**Ownership domain (single slice):** Geometry product-draw free helper.  
Collapse Preview `drawOne` Geometry branch + Export Geometry branch dual style-resolve+draw into sole free helper `ScreenshotAnnotationRenderGeometryLocal`.  
Document style product-read + `ScreenshotDrawGeometryAnnotationLocal`. Preview/Export only map coords → localRect + fallback pen width.

Not helper-only: product dual Geometry draw bodies deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview Geometry style-resolve + draw body | `ScreenshotAnnotationRenderGeometryLocal` |
| Export Geometry style-resolve + draw body | same free helper |

## Product-read / write contract

1. Free helper:  
   `ScreenshotAnnotationRenderGeometryLocal(hdc, document, ann, localRect, fallbackPenWidth)`  
   - Resolve Geometry/Arrow draw style from Document by id  
   - Color via `ScreenshotPresetColorLocal` when not custom  
   - Angle via `IsRotatableGeometryScreenshotAnnotationLocal`  
   - Draw via `ScreenshotDrawGeometryAnnotationLocal`
2. Preview: map screen crop → local HDC rect; call free helper  
3. Export: map export-relative rect; call free helper  

## Touch paths

- `src/screenshot/render/AnnotationGeometryRenderer.h` — **new** free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — Preview Geometry → free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export Geometry → free helper

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
rg ScreenshotDrawGeometryAnnotationLocal Overlay*.inl product dual → only free helper / low-level draw
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| Geometry Preview/Export dual draw body | **0** |
| Geometry product-draw free helper | **on** |
| AnnotationRenderContext | **on** (CLOSE-1) |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~105** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Geometry product-draw collapse. Arrow/Marker/etc. next CLOSE slices. Docs same commit. No pin. No AnnotationLegacyDocument growth.

## NEXT

S-D/S-F-CLOSE-3: Arrow product-draw free helper (same pattern) **or** multi-tool registry table. Prefer Arrow next (mirror Geometry). Ban micro-slices.
