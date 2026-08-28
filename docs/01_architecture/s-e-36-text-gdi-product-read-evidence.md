# S-E-36 evidence: Text GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-36  
Prior: residual inventory `bfce3065` / S-E-35 `4c1dee87`

## Intent

**Ownership domain (single slice):** Text **GDI draw style product-read** from Document by stable id. Preview + Export Text style props (font/bold/italics/background/outline/color) prefer Document item; Host ann geometry (rect/angle) + live text content remain Host. Net-delete Host ann style dual authority at Text draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Text draw Host `ann.textBold/textItalics/textFont*/textBackground*/textOutline*/color*` | Document props via `ResolveTextDrawStyle` |
| Export Text Host style dual | Document product-read same helper |

Host geometry layout (rect/angle) + live text content remain Host vector (GDI live-edit sole). Host style recovery when Document item missing.

## Product-read contract

1. `ScreenshotAnnotationTextDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveTextDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationTextDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveTextDrawStyle` — Document first, Host recovery
5. Preview + Export Text draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveTextDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Text
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Text
- `tests/test_annotation_document_dual_write_contract.cpp` — Text GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Text GDI style product-read | **on** |
| GDI product-read tools | **7** (Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text) |
| Stage 2 code commits | **~84** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Text GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32..35 tool GDI product-read arc.

## NEXT

More tool GDI product-read (Mosaic/HighLight/Magnifier/Watermark) or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
