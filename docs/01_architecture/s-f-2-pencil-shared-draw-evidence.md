# S-F-2 evidence: Pencil shared draw + dual preview/export body delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-2  
Prior: S-F-1 `8b57418a`

## Intent

**Ownership domain (single slice):** Extract sole free helper `ScreenshotDrawPencilStrokeLocal` (Chaikin refine + GDI+ stroke) and delete dual Pencil draw bodies in live preview (`AnnotationRender.inl`) and export (`Export.inl`).

## Deleted dual authority

| Legacy dual body | Sole after |
|---|---|
| AnnotationRender.inl Pencil Chaikin+GDI+ path | `ScreenshotDrawPencilStrokeLocal` |
| Export.inl Pencil Chaikin+GDI+ path | same free helper |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h` / `.cpp` — pure sole Pencil draw
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — dual body deleted; call sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — dual body deleted; call sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan: Pencil Chaikin dual bodies in Render/Export **0** (sole free helper).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Pencil preview/export dual draw bodies | **0** |
| Stage 2 code commits | **~56** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: shared Pencil draw + both dual body deletes. Not helper-only.

## NEXT

Continue dual-draw collapse (Marker/HighLight/Serial/Text) under ADR-002 tool-group vertical; or Geometry/Arrow create Document sole deepen. 合域强制.
