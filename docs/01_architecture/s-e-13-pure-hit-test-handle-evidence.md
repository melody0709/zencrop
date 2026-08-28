# S-E-13 evidence: pure hit-test handle + Host method delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-13  
Prior: S-E-12 `2c64ed95`

## Intent

**Ownership domain (single slice):** Extract pure free helper `ScreenshotAnnotationHitTestHandleLocal(ann, pt, fallbackPenWidth)` and delete Host dual-authority method `OverlayWindow::HitTestScreenshotAnnotationHandle`.

## Deleted Host dual authority

| Legacy Host method | Sole after |
|---|---|
| `OverlayWindow::HitTestScreenshotAnnotationHandle(pt, index)` | `ScreenshotAnnotationHitTestHandleLocal(ann, pt, penWidth)` |

## Touch paths

- `src/screenshot/ScreenshotAnnotationHelpers.h` / `.cpp` — pure sole (~180 lines moved)
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — Host def deleted; call pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — call pure
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — call pure
- `src/window/OverlayWindow.h` — declaration deleted
- package-exit partial evidence co-shipped

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan `HitTestScreenshotAnnotationHandle`: **0** (Host method gone; comments only).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Host HitTestScreenshotAnnotationHandle | **0** |
| Stage 2 code commits | **~52** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: pure free helper + Host method delete + all call sites. Not helper-only.

## NEXT

Continue residual pure Host hit-test deletes (HitTestScreenshotAnnotation / HitTestSelectedIntent) or Geometry/Arrow vertical under hard stop (~3 left). S-E package-exit partial documented.
