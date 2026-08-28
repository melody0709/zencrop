# S-E-32 evidence: Geometry/Arrow GDI product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-32  
Prior: residual inventory `0c414108` / S-E-31 `4e169014`

## Intent

**Ownership domain (single slice):** Geometry/Arrow **GDI draw style product-read** from Document by stable id. Preview + Export Geometry/Arrow style props (penWidth/lineStyle/ellipse/filling/roundedRadius/arrowShape/color) prefer Document item; Host ann geometry (start/end/points/angle) remains GDI layout (live-drag sole). Net-delete Host ann style dual authority at Geometry/Arrow draw paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Geometry draw Host `ann.penWidth/lineStyle/ellipse/filling/roundedRadius/color*` | Document props via `ResolveGeometryArrowDrawStyle` |
| Arrow draw Host `ann.penWidth/lineStyle/arrowShape/color*` | Document props via `ResolveGeometryArrowDrawStyle` |
| Export Geometry/Arrow Host style dual | Document product-read same helper |

Host geometry layout (start/end/points/angle) remains Host vector (GDI live-drag sole). Host style recovery when Document item missing.

## Product-read / create contract

1. `ScreenshotAnnotationGeometryArrowDrawStyle` — pure style bag
2. `ScreenshotAnnotationDocumentResolveGeometryArrowDrawStyle` — Document findById → style props
3. `ScreenshotAnnotationGeometryArrowDrawStyleFromHost` — Host recovery
4. `ScreenshotAnnotationResolveGeometryArrowDrawStyle` — Document first, Host recovery
5. Preview + Export Geometry/Arrow draw use Resolve helper

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveGeometryArrowDrawStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — preview Geometry/Arrow
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — export Geometry/Arrow
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
| Geometry/Arrow GDI style product-read | **on** |
| Stage 2 code commits | **~80** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: Geometry/Arrow GDI draw style product-read + preview/export product paths + tests. Not helper-only (product draw sites switched). Host geometry layout remains (blocked on Host vector delete). First GDI product-read ownership vertical step.

## NEXT

More tool GDI product-read or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
