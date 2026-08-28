# S-E-43 evidence: Geometry product-read Phase A1 (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-43  
Prior: ADR-003 `cb2ec701` / S-E-42 `4b70eef2`

## Intent

**Ownership domain (single slice):** Geometry **layout product-read** from Document by stable id (Phase A1 Host-vector exit). Export geometry (start/end/points/angle/pathMode/ellipse) prefer Document item; Host geometry remains live-drag sole until CommitModify. Net-delete Host geometry dual authority at Export paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Export `relativeRect` Host `ann.start/end` | Document via `ResolveGeometryLayout` |
| Export main draw loop Host geometry | `projectAnn` → Document layout |
| Export Mosaic early/rotated/path Host geometry | `projectAnn` |
| Export Eraser Host geometry | `projectAnn` |
| Export HighLight Host geometry | `projectAnn` |

Live drag / preview / hit-test Host geometry residual (Document stale mid-drag). `preferHostLive=true` for future live paths.

## Product-read contract

1. `ScreenshotAnnotationGeometryLayout` — pure layout bag
2. `ScreenshotAnnotationParsePathPoints` — pure PathPoints parse
3. `ScreenshotAnnotationDocumentResolveGeometryLayout` — Document findById → start/end/points/angle/pathMode/ellipse
4. `ScreenshotAnnotationGeometryLayoutFromHost` — Host recovery
5. `ScreenshotAnnotationResolveGeometryLayout(preferHostLive)` — Document first unless live
6. `ScreenshotAnnotationWithResolvedGeometry` — project layout onto Host ann copy
7. Export uses projectAnn / ResolveGeometryLayout

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — GeometryLayout pure sole + PathPoints parse + WideStringUtils include
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — projectAnn + all export loops
- `tests/test_annotation_document_dual_write_contract.cpp` — Geometry layout product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Geometry product-read (export) | **on** |
| GDI product-read tools | **12/12** |
| Host-vector exit Phase A1 | **partial ON** (export) |
| Stage 2 code commits | **~90** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: Geometry layout product-read + Export product paths + tests. Not helper-only (export sites switched). Live drag Host residual intentional (Document stale mid-drag). Complements S-E-42 plan Phase A1.

## NEXT

Phase A deepen: preview/hit-test geometry product-read (non-live items) or Phase B Document-order iterate. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
