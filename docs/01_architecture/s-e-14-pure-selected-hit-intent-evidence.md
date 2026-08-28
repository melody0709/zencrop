# S-E-14 evidence: pure selected hit-intent + Host method delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-14  
Prior: S-E-13 `eb705d3a`

## Intent

**Ownership domain (single slice):** Move `ScreenshotAnnotationHitIntent` to free pure type; extract pure free helper `ScreenshotAnnotationHitTestSelectedIntentLocal(ann, pt, fallbackPenWidth)`; delete Host dual-authority method `OverlayWindow::HitTestSelectedScreenshotAnnotationIntent` and nested private struct.

## Deleted Host dual authority

| Legacy Host | Sole after |
|---|---|
| nested `OverlayWindow::ScreenshotAnnotationHitIntent` | free `ScreenshotAnnotationHitIntent` in Helpers |
| `OverlayWindow::HitTestSelectedScreenshotAnnotationIntent(pt)` | `ScreenshotAnnotationHitTestSelectedIntentLocal(ann, pt, penWidth)` |
| AnnotationEdit re-check rotateOuter/outsideAction after intent | deleted as redundant (covered by pure intent) |

## Touch paths

- `src/screenshot/ScreenshotAnnotationHelpers.h` / `.cpp` — free type + pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — Host def deleted; call pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — call pure; delete redundant re-check
- `src/window/OverlayWindow.h` — nested struct + method declaration deleted

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan `HitTestSelectedScreenshotAnnotationIntent`: **0** (Host method gone; comments only).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Host HitTestSelectedScreenshotAnnotationIntent | **0** |
| Stage 2 code commits | **~53** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: free type + pure free helper + Host method/struct delete + all call sites + redundant re-check delete. Not helper-only.

## NEXT

Residual pure hit-test: `HitTestScreenshotAnnotation` (index loop over Host vector — harder; needs Document product-read or span of legacy anns). Or Geometry/Arrow vertical. Hard stop 55 near (~2 left).
