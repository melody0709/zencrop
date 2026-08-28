# S-E-33 evidence: Pencil/BrokenLine GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-33  
Prior: S-E-32 `3fbe5021`

## Intent

**Ownership domain (single slice):** Pencil/BrokenLine **GDI draw style product-read** from Document by stable id. Preview + Export Pencil/BrokenLine style props (penWidth/lineStyle/brokenLine*/color) prefer Document item; Host ann geometry (points/start/end) remains GDI layout. Net-delete Host ann style dual authority at Pencil/BrokenLine draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Pencil draw Host `ann.penWidth/lineStyle/color*` | Document props via `ResolvePencilBrokenLineDrawStyle` |
| BrokenLine draw Host `ann.penWidth/lineStyle/brokenLine*/color*` | Document props via `ResolvePencilBrokenLineDrawStyle` |
| Export Pencil/BrokenLine Host style dual | Document product-read same helper |

Host geometry layout (points/start/end) remains Host vector (GDI live-drag sole). Host style recovery when Document item missing.

## Product-read contract

1. `ScreenshotAnnotationPencilBrokenLineDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolvePencilBrokenLineDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationPencilBrokenLineDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolvePencilBrokenLineDrawStyle` — Document first, Host recovery
5. Preview + Export Pencil/BrokenLine draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolvePencilBrokenLineDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Pencil/BrokenLine
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Pencil/BrokenLine
- `tests/test_annotation_document_dual_write_contract.cpp` — GDI style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Pencil/BrokenLine GDI style product-read | **on** |
| Geometry/Arrow + Pencil/BrokenLine GDI product-read | **on** |
| Stage 2 code commits | **~81** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Pencil/BrokenLine GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Complements S-E-32 Geometry/Arrow GDI product-read.

## NEXT

More tool GDI product-read (Marker/Text/Serial/…) or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
