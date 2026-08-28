# S-E-12 evidence: pure outside-adjust + Host method delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-12  
Prior: S-E-11 `ac3b58e3`

## Intent

**Ownership domain (single slice):** Extract pure free helper `ScreenshotAnnotationOutsideAdjustActionLocal(ann, pt, fallbackPenWidth)` and delete Host dual-authority method `OverlayWindow::GetScreenshotAnnotationOutsideAdjustAction`.

## Deleted Host dual authority

| Legacy Host method | Sole after |
|---|---|
| `OverlayWindow::GetScreenshotAnnotationOutsideAdjustAction(pt, index)` | `ScreenshotAnnotationOutsideAdjustActionLocal(ann, pt, penWidth)` |

## Touch paths

- `src/screenshot/ScreenshotAnnotationHelpers.h` / `.cpp` — pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — Host def deleted; call pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — call pure
- `src/window/OverlayWindow.h` — declaration deleted

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan `GetScreenshotAnnotationOutsideAdjustAction`: **0** (Host method gone).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Host GetScreenshotAnnotationOutsideAdjustAction | **0** |
| Stage 2 code commits | **~51** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: pure free helper + Host method delete + all call sites. Not helper-only.

## NEXT

S-E package-exit partial or Geometry/Arrow vertical under 合域强制. Hard stop 55 near (~4 left).
