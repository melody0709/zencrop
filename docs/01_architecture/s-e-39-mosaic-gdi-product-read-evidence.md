# S-E-39 evidence: Mosaic GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-39  
Prior: S-E-38 `fa4fdd3e`

## Intent

**Ownership domain (single slice):** Mosaic **GDI draw style product-read** from Document by stable id. Preview + Export Mosaic style props (penWidth/pathMode/mosaicMode) prefer Document item for ToolMosaic + ToolAutoMosaic; Host geometry (start/end/points/angle) remains GDI layout. Net-delete Host ann style dual authority at Mosaic draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Mosaic preview Host `ann.penWidth/pathMode/mosaicMode` | Document props via `ResolveMosaicDrawStyle` |
| Export early axis-aligned Mosaic Host style dual | Document product-read same helper |
| Export rotated Mosaic Host style dual | Document product-read same helper |
| Export pathMode==1 Mosaic Host style dual | Document product-read same helper |
| Export pathMode route Host `ann.pathMode` | Document `style.pathMode` |

Host geometry remains Host vector (GDI layout sole). Host style recovery when Document item missing. Covers ToolMosaic + ToolAutoMosaic.

## Product-read contract

1. `ScreenshotAnnotationMosaicDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveMosaicDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationMosaicDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveMosaicDrawStyle` — Document first, Host recovery
5. Preview + Export Mosaic draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveMosaicDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Mosaic
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Mosaic (3 paths + route)
- `tests/test_annotation_document_dual_write_contract.cpp` — Mosaic GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Mosaic GDI style product-read | **on** |
| GDI product-read tools | **10** (Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text/Magnifier/Watermark/Mosaic) |
| Stage 2 code commits | **~87** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Mosaic GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32..38 tool GDI product-read arc.

## NEXT

More tool GDI product-read (HighLight/Eraser) or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
