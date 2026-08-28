# S-E-35 evidence: Serial GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-35  
Prior: S-E-34 `1bbd6817`

## Intent

**Ownership domain (single slice):** Serial **GDI draw style product-read** from Document by stable id. Preview + Export Serial style props (serialNumber/serialType/color) prefer Document item; Host ann geometry (rect/angle) remains GDI layout. Net-delete Host ann style dual authority at Serial draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Serial draw Host `ann.serialNumber/arrowShape/color*` | Document props via `ResolveSerialDrawStyle` |
| Export Serial Host style dual | Document product-read same helper |

Host geometry layout (rect/angle) remains Host vector (GDI live-drag sole). Host style recovery when Document item missing. Serial type stored in LineShape (legacy arrowShape).

## Product-read contract

1. `ScreenshotAnnotationSerialDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveSerialDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationSerialDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveSerialDrawStyle` — Document first, Host recovery
5. Preview + Export Serial draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveSerialDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Serial
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Serial
- `tests/test_annotation_document_dual_write_contract.cpp` — Serial GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Serial GDI style product-read | **on** |
| Geometry/Arrow + Pencil/BrokenLine + Marker + Serial GDI product-read | **on** |
| Stage 2 code commits | **~83** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Serial GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32..34 tool GDI product-read arc.

## NEXT

More tool GDI product-read (Text/Mosaic/HighLight/…) or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
