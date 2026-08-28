# S-B-27 evidence: adjust session sole authority

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-27  
Prior: S-B-26 `c57770a0`

## Intent

Delete dual-write Host fields for **adjust session** ownership domain (split from OWN-92 crop geometry cluster). Sole store is `m_editorState` (`adjustAnchor*` / `adjustStartRect*`). Mutation sites call pure `ScreenshotEditorSyncAdjustSession`; product reads use pure `ScreenshotEditorAdjustStartRect` / `ScreenshotEditorAdjustAnchor`; residual `SyncScreenshotCropGeometryMirror` re-reads pure adjust session.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_adjustAnchor` | pure `ScreenshotEditorAdjustAnchor*` / SyncAdjustSession |
| `m_adjustStartRect` | pure `ScreenshotEditorAdjustStartRect*` / SyncAdjustSession |

## Touch paths

- `src/window/OverlayWindow.h` — 2 dual fields deleted (**241→240** phys)
- `src/window/OverlayWindow.cpp` — LButton adjust start → pure Sync; MouseMove ApplyAdjust product reads → pure
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — residual Sync re-reads pure adjust
- `src/screenshot/editor/ScreenshotEditorState.h` — pure `ScreenshotEditorSyncAdjustSession` added

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for deleted dual symbols: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **240** |
| adjust dual fields | **0** |

## NEXT

S-B-28: next ownership domain among remaining real dual-write clusters (OWN-92 residual **cropRect**, history flags method delete, aspect ratio helper) — one domain per slice.
