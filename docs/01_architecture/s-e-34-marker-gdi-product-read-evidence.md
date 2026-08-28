# S-E-34 evidence: Marker GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-34  
Prior: S-E-33 `e1f20673`

## Intent

**Ownership domain (single slice):** Marker **GDI draw style product-read** from Document by stable id. Preview + Export Marker style props (penWidth/pathMode/markerBlendMode/color) prefer Document item; Host ann geometry (points/start/end/angle) remains GDI layout. Net-delete Host ann style dual authority at Marker draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Marker draw Host `ann.penWidth/pathMode/markerBlendMode/color*` | Document props via `ResolveMarkerDrawStyle` |
| Export Marker Host style dual | Document product-read same helper |

Host geometry layout (points/start/end/angle) remains Host vector (GDI live-drag sole). Host style recovery when Document item missing.

## Product-read contract

1. `ScreenshotAnnotationMarkerDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveMarkerDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationMarkerDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveMarkerDrawStyle` — Document first, Host recovery
5. Preview + Export Marker draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveMarkerDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Marker
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Marker
- `tests/test_annotation_document_dual_write_contract.cpp` — Marker GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Marker GDI style product-read | **on** |
| Geometry/Arrow + Pencil/BrokenLine + Marker GDI product-read | **on** |
| Stage 2 code commits | **~82** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Marker GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32/33 Geometry/Arrow/Pencil/BrokenLine GDI product-read.

## NEXT

More tool GDI product-read (Text/Serial/Mosaic/…) or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
