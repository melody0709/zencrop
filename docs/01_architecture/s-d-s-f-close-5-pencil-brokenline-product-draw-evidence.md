# S-D/S-F-CLOSE-5 evidence: Pencil/BrokenLine product-draw free helpers

Date: 2026-07-23  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-5  
Prior: S-D/S-F-CLOSE-4 `361230af`

## Intent

**Ownership domain (single slice):** Pencil + BrokenLine product-draw free helpers (合域 tool group).  
Collapse Preview/Export dual style-resolve+draw for Pencil and BrokenLine into sole free helpers.  
Shared `ResolvePencilBrokenLineDrawStyle` + draw APIs. Preview/Export only map points → local HDC coords + fallback pen width.

Not helper-only: product dual draw bodies deleted for both tools.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview/Export Pencil style-resolve + draw body | `ScreenshotAnnotationRenderPencilLocal` |
| Preview/Export BrokenLine style-resolve + draw body | `ScreenshotAnnotationRenderBrokenLineLocal` |

## Product-read / write contract

1. Pencil:  
   `ScreenshotAnnotationRenderPencilLocal(hdc, document, ann, localPoints, count, fallbackPenWidth)`  
   - Resolve Pencil/BrokenLine draw style from Document by id  
   - Draw via `ScreenshotDrawPencilStrokeLocal`
2. BrokenLine:  
   `ScreenshotAnnotationRenderBrokenLineLocal(hdc, document, ann, localStart, localEnd, localPoints, count, fallbackPenWidth)`  
   - Curve mode (`brokenLineMode==1` + ≥3 pts) → `ScreenshotDrawBrokenLineCurveLocal`  
   - Else segment loop / single segment → `ScreenshotDrawBrokenLineLocal`
3. Preview: `toLocal` map; Export: `relativePoint` map

## Touch paths

- `src/screenshot/render/AnnotationGeometryRenderer.h` — Pencil + BrokenLine free helpers
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
| Pencil/BrokenLine Preview/Export dual draw body | **0** |
| Pencil+BrokenLine product-draw free helpers | **on** |
| Geometry+Arrow+Marker free helpers | **on** (CLOSE-2/3/4) |
| AnnotationRenderContext | **on** (CLOSE-1) |
| full registry/renderer split | **NOT closed** |
| Stage 2 code commits | **~108** (ADR-003 硬停 120 final) |

## Granularity note

One domain: Pencil+BrokenLine tool-group product-draw collapse (shared style resolver). Remaining tool dual bodies: Text/Watermark/Serial/Mosaic/HighLight/Eraser/Magnifier (many already sole free draws; residual = product style-resolve wiring). Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-6: remaining tool product-draw collapse **or** registry table **or** S-A-CLOSE. Prefer inventory residual tool dual bodies then multi-tool registry. Ban micro-slices.
