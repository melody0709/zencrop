# S-E-40 evidence: HighLight GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-40  
Prior: S-E-39 `41b18611`

## Intent

**Ownership domain (single slice):** HighLight **GDI draw style product-read** from Document by stable id. Preview + Export HighLight style props (penWidth/opacity/stroke/strokeColor/ellipse) prefer Document item; Host geometry (start/end/angle) remains GDI layout. Net-delete Host ann style dual authority at HighLight draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| HighLight preview Host `ann.highLightStrokeColor/penWidth/highLightOpacity/highLightStroke/ellipse` | Document props via `ResolveHighLightDrawStyle` |
| Export HighLight Host style dual | Document product-read same helper |

Host geometry remains Host vector (GDI layout sole). Host style recovery when Document item missing. Role=HighLight. Ellipse from PathMode 3. penWidth prefers HighLightStrokeWidth then PenWidth.

## Product-read contract

1. `ScreenshotAnnotationHighLightDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveHighLightDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationHighLightDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveHighLightDrawStyle` — Document first, Host recovery
5. Preview + Export HighLight draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveHighLightDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview HighLight
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export HighLight
- `tests/test_annotation_document_dual_write_contract.cpp` — HighLight GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| HighLight GDI style product-read | **on** |
| GDI product-read tools | **11** (Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text/Magnifier/Watermark/Mosaic/HighLight) |
| Stage 2 code commits | **~88** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: HighLight GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32..39 tool GDI product-read arc.

## NEXT

Eraser GDI product-read or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked. Near 硬停 90 — high-value only.
