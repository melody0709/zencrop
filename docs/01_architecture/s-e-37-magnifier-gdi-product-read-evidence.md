# S-E-37 evidence: Magnifier GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-37  
Prior: S-E-36 `28baf06e`

## Intent

**Ownership domain (single slice):** Magnifier **GDI draw style product-read** from Document by stable id. Preview + Export Magnifier style props (penWidth/ellipse/roundedRadius/linkType/magnification/antiAlias/eraseMark/shadow/color) prefer Document item; Host ann geometry (dest/source rects/angle) remains GDI layout. Net-delete Host ann style dual authority at Magnifier draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Magnifier draw Host `ann.penWidth/ellipse/roundedRadius/magnifier*/color*` | Document props via `ResolveMagnifierDrawStyle` |
| Export Magnifier Host style dual | Document product-read same helper |

Host geometry layout (dest/source rects/angle) remains Host vector (GDI live-drag sole). Host style recovery when Document item missing. Ellipse from PathMode 3; role=Magnifier.

## Product-read contract

1. `ScreenshotAnnotationMagnifierDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveMagnifierDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationMagnifierDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveMagnifierDrawStyle` — Document first, Host recovery
5. Preview + Export Magnifier draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveMagnifierDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Magnifier
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Magnifier
- `tests/test_annotation_document_dual_write_contract.cpp` — Magnifier GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Magnifier GDI style product-read | **on** |
| GDI product-read tools | **8** (Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text/Magnifier) |
| Stage 2 code commits | **~85** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Magnifier GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32..36 tool GDI product-read arc.

## NEXT

More tool GDI product-read (Mosaic/HighLight/Watermark/Eraser) or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
