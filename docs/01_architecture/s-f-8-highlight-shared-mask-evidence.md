# S-F-8 evidence: HighLight shared mask + dual preview/export body delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-8  
Prior: S-F-7 `ddd26f33`

## Intent

**Ownership domain (single slice):** Extract sole free helper `ScreenshotDrawHighLightMaskLocal` + shared `ScreenshotHighLightRenderInfo` and delete dual HighLight full-screen mask/stroke bodies in live preview (`AnnotationRender.inl`) and export (`Export.inl`).

## Deleted dual authority

| Legacy dual | Sole after |
|---|---|
| Render HighLight darken-pixel mask + stroke loop | `ScreenshotDrawHighLightMaskLocal` |
| Export HighLight darken-pixel mask + stroke loop | same free helper |

Unrotate math inlined in free helper (no Geometry.cpp link dep for annotation unit tests).

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h` / `.cpp` — pure sole HighLight mask + stroke
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — dual body deleted; call sole
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
| HighLight preview/export dual mask bodies | **0** |
| Stage 2 code commits | **~62** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: HighLight shared mask/stroke + both dual body deletes. Not helper-only.

## NEXT

Residual dual-draw inventory (Mosaic preview vs export still dual; Magnifier already shared via `ScreenshotDrawMagnifierLocal`). Then Geometry/Arrow Document product-read / tool-group vertical deepen under ADR-002. 合域强制.
