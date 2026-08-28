# S-B-24 evidence: lastDrawn sole authority

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-24  
Prior: S-B-23 `bbc6e793`

## Intent

Delete dual-write Host fields for **lastDrawn geometry** ownership domain (split from OWN-92 crop geometry cluster). Sole store is `m_editorState` (`lastDrawnRect*` / `lastDrawnWasSmart`). Mutation sites call pure `ScreenshotEditorSyncLastDrawn`; residual `SyncScreenshotCropGeometryMirror` re-reads pure lastDrawn.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_lastDrawnRect` | pure `ScreenshotEditorLastDrawnRect*` / SyncLastDrawn |
| `m_lastDrawnWasSmart` | pure `ScreenshotEditorLastDrawnWasSmart` / SyncLastDrawn |

## Touch paths

- `src/window/OverlayWindow.h` — 2 dual fields deleted (**243→242** phys)
- `src/window/OverlayWindow.cpp` — lastDrawn mutations → pure SyncLastDrawn; product reads → pure
- `src/screenshot/overlay/OverlayWindowScreenshot.Surface.inl` — same
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — residual Sync re-reads pure lastDrawn
- `src/screenshot/editor/ScreenshotEditorState.h` — pure `ScreenshotEditorSyncLastDrawn` added

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
| OverlayWindow.h phys | **242** |
| lastDrawn dual fields | **0** |

## NEXT

S-B-25: next ownership domain among remaining real dual-write clusters (OWN-92 residual crop/smart/adjust/suppressed, history flags method delete, aspect ratio helper) — one domain per slice.
