# S-E-38 evidence: Watermark GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-38  
Prior: S-E-37 `d889f6d9`

## Intent

**Ownership domain (single slice):** Watermark **GDI draw style product-read** from Document by stable id. Preview + Export Watermark style props (text/color/opacity/font/bold/italics/position/gap/angle) prefer Document item; Host crop geometry remains GDI layout. Net-delete Host ann style dual authority at Watermark draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Watermark draw Host `ann.text/watermarkColor/watermarkOpacity/watermarkFont*/textBold/textItalics/watermarkPosition/gap/angle` | Document props via `ResolveWatermarkDrawStyle` |
| Export Watermark Host style dual | Document product-read same helper |

Host crop geometry remains Host vector (GDI layout sole). Host style recovery when Document item missing. Role=Watermark.

## Product-read contract

1. `ScreenshotAnnotationWatermarkDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveWatermarkDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationWatermarkDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveWatermarkDrawStyle` — Document first, Host recovery
5. Preview + Export Watermark draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveWatermarkDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Watermark
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Watermark
- `tests/test_annotation_document_dual_write_contract.cpp` — Watermark GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Watermark GDI style product-read | **on** |
| GDI product-read tools | **9** (Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text/Magnifier/Watermark) |
| Stage 2 code commits | **~86** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Watermark GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32..37 tool GDI product-read arc.

## NEXT

More tool GDI product-read (Mosaic/HighLight/Eraser) or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
