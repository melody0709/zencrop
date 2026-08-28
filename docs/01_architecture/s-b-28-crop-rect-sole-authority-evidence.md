# S-B-28 evidence: cropRect sole authority (OWN-92 complete)

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-28  
Prior: S-B-27 `4c6cc2f4`

## Intent

Delete dual-write Host field for **cropRect** ownership domain — final residual dual-write field from OWN-92 crop geometry cluster. Sole store is `m_editorState` (`cropRect*`). Mutation sites call pure `ScreenshotEditorSyncCropRect`; product reads use pure `ScreenshotEditorCropRect` (named locals for PtInRect/IntersectRect/MonitorFromRect); residual `SyncScreenshotCropGeometryMirror` deleted entirely.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_cropRect` | pure `ScreenshotEditorCropRect*` / SyncCropRect |
| `SyncScreenshotCropGeometryMirror` | deleted (OWN-92 complete) |

## Touch paths

- `src/window/OverlayWindow.h` — dual field + Sync decl deleted (**240→237** phys)
- `src/window/OverlayWindow.cpp` — ClampCropRect / ResizeCropRectByWheel / adjust drag / keyboard nudge / seed assigns → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.{Surface,AnnotationEdit,AnnotationHitTest,AnnotationRender,ToolbarRender,ToolbarInteraction}.inl` — product reads → pure
- `src/screenshot/OverlayWindowScreenshot.inl` — bare Sync seed removed
- `src/screenshot/editor/ScreenshotEditorState.h` — pure `ScreenshotEditorSyncCropRect` added; OWN-92 sole note

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `m_cropRect` (excluding HoverMagnifierWidget own member): **0**.  
Live product scan for `SyncScreenshotCropGeometryMirror` (excluding deletion comments): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **237** |
| cropRect dual field | **0** |
| OWN-92 dual fields | **0** (complete) |

## NEXT

S-B-29: next ownership domain among remaining residual Sync methods (SyncScreenshotHistoryFlags projection delete, SyncScreenshotAspectRatioFromCropRect method delete, SyncScreenshotEditorState residual if any Host dual remains) — one domain per slice. Or Stage 2 package exit check per GOAL §11 when S-B dual-write residual = 0 for ownership fields.
