# S-E-15 evidence: pure annotation hit-test + Host method delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-15  
Prior: S-E-14 `5c356377`

## Intent

**Ownership domain (single slice):** Extract pure free helper `ScreenshotAnnotationHitTestLocal(annotations, pt, cropRect)` and delete Host dual-authority method `OverlayWindow::HitTestScreenshotAnnotation`.

## Deleted Host dual authority

| Legacy Host method | Sole after |
|---|---|
| `OverlayWindow::HitTestScreenshotAnnotation(pt)` | `ScreenshotAnnotationHitTestLocal(anns, pt, cropRect)` |

Mode gate (`IsScreenshotMode`) stays at call-site event handlers (already screenshot-mode context).

## Touch paths

- `src/screenshot/ScreenshotAnnotationHelpers.h` / `.cpp` — pure sole (~140 lines moved)
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — Host def deleted; call pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — call pure
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — call pure
- `src/window/OverlayWindow.h` — declaration deleted

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan `HitTestScreenshotAnnotation(`: **0** (Host method gone).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Host HitTestScreenshotAnnotation | **0** |
| Stage 2 code commits | **~54** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: pure free helper + Host method delete + all call sites. Not helper-only.

## NEXT

Stage2 hard stop 55 near (~1 left). Options: residual Host method delete (UpdateCursor — GDI SetCursor side-effect, keep Host) or Stage2 budget ADR / package-exit freeze. Geometry/Arrow vertical needs budget extension.
