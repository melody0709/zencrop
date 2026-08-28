# S-E-44 evidence: Preview/hit-test geometry product-read (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-44  
Prior: S-E-43 `941c9dbb`

## Intent

**Ownership domain (single slice):** Preview + hit-test **geometry layout product-read** from Document by stable id (Phase A deepen). Non-live items prefer Document layout; selected mid-drag uses `preferHostLive` (Host geometry sole until CommitModify). Net-delete Host geometry dual authority at preview draw + selected hit-test paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview draw loop Host `ann.start/end/points/angle` | `projectAnn` → Document layout (preferHostLive mid-drag) |
| Preview HighLight Host geometry | `projectAnn` |
| Selected hit-test Host geometry | `ResolveGeometryLayout(preferHostLive)` |

Bulk `ScreenshotAnnotationHitTestLocal(vector)` residual (Phase B Document-order). Live drag Host residual intentional.

## Product-read contract

1. Reuse S-E-43 `ResolveGeometryLayout` / `WithResolvedGeometry`
2. Preview `projectAnn(ann, isSelected)` — preferHostLive when selected + move/resize/rotate
3. Hit-test selected ann — same preferHostLive rule
4. Export already Document-first (S-E-43)

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — projectAnn + draw loop + HighLight
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — selected hit-test geometry product-read

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Geometry product-read (export) | **on** (S-E-43) |
| Geometry product-read (preview/hit-test) | **on** |
| Host-vector exit Phase A | **ON** (export + preview + selected hit-test) |
| Stage 2 code commits | **~91** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: preview/hit-test geometry product-read + product paths. Not helper-only (product draw/hit-test sites switched). Complements S-E-43 export geometry product-read. Phase A geometry product-read complete for export/preview/selected-hit-test.

## NEXT

Phase B Document-order iterate (render/export/hit-test loops over Document, not Host vector) under ADR-003. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
