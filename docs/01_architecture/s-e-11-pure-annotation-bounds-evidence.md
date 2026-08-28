# S-E-11 evidence: pure annotation bounds + Host method delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-11  
Prior: S-E-10 `e6d61619`

## Intent

**Ownership domain (single slice):** Extract pure free helper `ScreenshotAnnotationBoundsLocal(ann, watermarkCrop)` and delete Host dual-authority method `OverlayWindow::GetScreenshotAnnotationBounds`.

## Deleted Host dual authority

| Legacy Host method | Sole after |
|---|---|
| `OverlayWindow::GetScreenshotAnnotationBounds(ann)` | `ScreenshotAnnotationBoundsLocal(ann, watermarkCrop)` |

## Touch paths

- `src/screenshot/ScreenshotAnnotationHelpers.h` / `.cpp` — pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — Host def deleted; call pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — call pure
- `src/window/OverlayWindow.h` — declaration deleted

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan `GetScreenshotAnnotationBounds`: **0** (Host method gone).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Host GetScreenshotAnnotationBounds | **0** |
| Stage 2 code commits | **~50** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: pure free helper + Host method delete + all call sites. Not helper-only (Host method deleted).

## NEXT

S-E package-exit partial or Geometry/Arrow vertical under 合域强制. Hard stop 55 near.
