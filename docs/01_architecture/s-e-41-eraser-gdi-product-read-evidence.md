# S-E-41 evidence: Eraser GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-41  
Prior: S-E-40 `3e88272e`

## Intent

**Ownership domain (single slice):** Eraser **GDI draw style product-read** from Document by stable id. Preview + Export Eraser style props (penWidth/pathMode/ellipse) prefer Document item; Host geometry (start/end/points/angle) remains GDI layout. Net-delete Host ann style dual authority at Eraser draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Eraser preview Host `ann.penWidth/pathMode/ellipse` | Document props via `ResolveEraserDrawStyle` |
| Export Eraser Host style dual | Document product-read same helper |
| Selection freehand-eraser skip Host `ann.pathMode` | Document `style.pathMode` |

Host geometry remains Host vector (GDI layout sole). Host style recovery when Document item missing. Ellipse from PathMode 3.

## Product-read contract

1. `ScreenshotAnnotationEraserDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveEraserDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationEraserDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveEraserDrawStyle` — Document first, Host recovery
5. Preview + Export Eraser draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveEraserDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Eraser + selection pathMode
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Eraser
- `tests/test_annotation_document_dual_write_contract.cpp` — Eraser GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Eraser GDI style product-read | **on** |
| GDI product-read tools | **12** (Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text/Magnifier/Watermark/Mosaic/HighLight/Eraser) |
| Stage 2 code commits | **~89** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Eraser GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32..40 tool GDI product-read arc. **All tool GDI product-read complete.**

## NEXT

Host-vector delete plan (docs) under near-硬停 90 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked. No more 1-tool style slices under budget near hard stop.
