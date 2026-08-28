# S-B-29 evidence: SyncScreenshotHistoryFlags method delete

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-29  
Prior: S-B-28 `90923dc6`

## Intent

Delete residual Host method `SyncScreenshotHistoryFlags` (projection of Host-owned `m_annotationHistory` canUndo/canRedo into pure state). No dual-write Host fields remain for history flags — pure `undoAvailable`/`redoAvailable` are sole store; Host owns AnnotationHistory runtime collection. Call sites now inline pure `ScreenshotEditorSetHistoryAvailability`.

## Deleted Host authority

| Legacy method | Sole authority |
|---|---|
| `SyncScreenshotHistoryFlags` | pure `ScreenshotEditorSetHistoryAvailability` (inlined at call sites) |

## Touch paths

- `src/window/OverlayWindow.h` — Sync decl deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted; SetActiveScreenshotTool inlines projection
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — inlined projection
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — inlined projection

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `SyncScreenshotHistoryFlags` (excluding deletion comments): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **237** |
| residual dual-write Sync mirrors | HistoryFlags gone; AspectRatio + EditorState remain |

## NEXT

S-B-30: next residual Sync method delete among `SyncScreenshotAspectRatioFromCropRect` / `SyncScreenshotEditorState` — or Stage 2 package exit check per GOAL §11 when residual dual-write ownership fields = 0.
