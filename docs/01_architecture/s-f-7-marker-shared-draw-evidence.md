# S-F-7 evidence: Marker shared draw + dual preview/export body delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-7  
Prior: S-F-6 `340f7907`

## Intent

**Ownership domain (single slice):** Extract sole free helper `ScreenshotDrawMarkerAnnotationLocal` (mask composite + pathMode 2 rect + freehand path) and delete dual Marker draw bodies in live preview (`AnnotationRender.inl`) and export (`Export.inl`).

## Deleted dual authority

| Legacy dual | Sole after |
|---|---|
| Render Marker compositeMarkerMask + pathMode 2/path bodies | `ScreenshotDrawMarkerAnnotationLocal` |
| Export Marker compositeMarkerMask + pathMode 2/path bodies | same free helper |

Helper lives in `ScreenshotImageUtils` (already owns `ScreenshotCompositeMarkerMaskLocal`).

## Touch paths

- `src/screenshot/ScreenshotImageUtils.h` / `.cpp` — pure sole Marker draw
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
| Marker preview/export dual draw bodies | **0** |
| Stage 2 code commits | **~61** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: Marker shared draw + both dual body deletes. Not helper-only.

## NEXT

Continue dual-draw collapse (HighLight / Mosaic residual) under ADR-002. 合域强制.
