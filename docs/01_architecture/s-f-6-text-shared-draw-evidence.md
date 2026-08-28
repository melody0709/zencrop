# S-F-6 evidence: Text non-edit shared draw + dual body delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-6  
Prior: S-F-5 `ee454bba`

## Intent

**Ownership domain (single slice):** Extract sole free helper `ScreenshotDrawTextAnnotationLocal` (background + outline + main text) and delete dual non-edit Text draw bodies in export (`Export.inl`) and live preview (`AnnotationRender.inl`). Editing selection/caret remain preview-only Host extras.

## Deleted dual authority

| Legacy dual | Sole after |
|---|---|
| Export `drawText` full GDI body | `ScreenshotDrawTextAnnotationLocal` |
| Render non-edit core of `drawText` | same free helper |
| Render editing selection/caret | **kept Host** (preview-only) |

Measure logic inlined in free helper (avoid Geometry.cpp link dep for annotation unit tests).

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h` / `.cpp` — pure sole Text draw
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — non-edit dual body deleted; editing extras kept
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — dual body deleted; call sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Text non-edit preview/export dual bodies | **0** |
| Stage 2 code commits | **~60** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: Text non-edit shared draw + both dual body deletes. Editing overlays intentionally Host-kept (preview side-effects). Not helper-only.

## NEXT

Continue dual-draw collapse (Marker / HighLight) under ADR-002. 合域强制.
