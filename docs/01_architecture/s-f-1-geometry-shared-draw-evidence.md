# S-F-1 evidence: Geometry shared draw + dual preview/export body delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F Geometry-Arrow tool-group vertical (first slice)  
Slice: S-F-1  
Prior: ADR-002 `44b53473`

## Intent

**Ownership domain (single slice):** Extract sole free helper `ScreenshotDrawGeometryAnnotationLocal` and delete dual Geometry GDI+ draw bodies in live preview (`AnnotationRender.inl`) and export (`Export.inl`). First Geometry/Arrow vertical cutover under ADR-002 budget extension.

## Deleted dual authority

| Legacy dual body | Sole after |
|---|---|
| AnnotationRender.inl Geometry GDI+ path (~60 lines) | `ScreenshotDrawGeometryAnnotationLocal` |
| Export.inl Geometry GDI+ path (~55 lines) | same free helper |

Arrow already shared via `ScreenshotDrawArrowShapeLocal` (no dual body this slice).

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h` / `.cpp` — pure sole Geometry draw
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — dual body deleted; call sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — dual body deleted; call sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan: Geometry GDI+ path construction in Render/Export **0** (sole free helper).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Geometry preview/export dual draw bodies | **0** |
| Stage 2 code commits | **~55** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: shared Geometry draw + both dual body deletes. Not helper-only (dual product paths net-deleted).

## NEXT

Geometry/Arrow vertical continue: Arrow preview/export already shared; next create/edit Document sole for Geometry/Arrow group, or HighLight/next tool dual-draw collapse under 合域强制.
