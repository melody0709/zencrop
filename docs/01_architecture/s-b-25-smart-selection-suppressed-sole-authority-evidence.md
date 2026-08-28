# S-B-25 evidence: smartSelectionSuppressed sole authority

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-25  
Prior: S-B-24 `bb89aa07`

## Intent

Delete dual-write Host field for **smart-selection suppressed point** ownership domain (split from OWN-92 crop geometry cluster). Sole store is `m_editorState` (`smartSelectionSuppressedX/Y`). Mutation sites call pure `ScreenshotEditorSyncSmartSelectionSuppressed`; residual `SyncScreenshotCropGeometryMirror` re-reads pure suppressed.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_smartSelectionSuppressedPoint` | pure `ScreenshotEditorSmartSelectionSuppressed*` / SyncSmartSelectionSuppressed |

## Touch paths

- `src/window/OverlayWindow.h` — dual field deleted (**242→241** phys)
- `src/window/OverlayWindow.cpp` — 2 mutation sites → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — residual Sync re-reads pure suppressed
- `src/screenshot/editor/ScreenshotEditorState.h` — pure `ScreenshotEditorSyncSmartSelectionSuppressed` added

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `m_smartSelectionSuppressedPoint`: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **241** |
| smartSelectionSuppressed dual field | **0** |

## NEXT

S-B-26: next ownership domain among remaining real dual-write clusters (OWN-92 residual crop/smart/adjust, history flags method delete, aspect ratio helper) — one domain per slice.
