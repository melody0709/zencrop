# S-B-30 evidence: SyncScreenshotAspectRatioFromCropRect method delete

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-30  
Prior: S-B-29 `db8c0b87`

## Intent

Delete residual Host method `SyncScreenshotAspectRatioFromCropRect`. Logic already wrote only pure `m_editorState.cropPrefs.aspectRatio` (no dual Host field). Moved to pure `ScreenshotEditorSyncAspectRatioFromCropRect` on `ScreenshotEditorState.h`; call sites rewritten.

## Deleted Host authority

| Legacy method | Sole authority |
|---|---|
| `SyncScreenshotAspectRatioFromCropRect` | pure `ScreenshotEditorSyncAspectRatioFromCropRect` |

## Touch paths

- `src/window/OverlayWindow.h` — Sync decl deleted
- `src/window/OverlayWindow.cpp` — Sync def deleted; call sites → pure
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — call site → pure
- `src/screenshot/editor/ScreenshotEditorState.h` — pure helper added

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `SyncScreenshotAspectRatioFromCropRect` (excluding deletion comments): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **237** |
| residual Host Sync methods | `SyncScreenshotEditorState` only |

## NEXT

S-B-31: assess / delete residual `SyncScreenshotEditorState` if still dual-write, or Stage 2 package exit check per GOAL §11 (ownership dual-write fields = 0; residual projection Sync may remain for Host-owned collections).
