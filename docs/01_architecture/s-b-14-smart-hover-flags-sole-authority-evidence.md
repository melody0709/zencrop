# S-B-14 evidence: smart-hover flags sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-14  
Prior: S-B-13 `07e7c3cf`

## Intent

Delete dual-write Host fields for **smart-hover flags** ownership domain. Sole store is `m_editorState` (`hasSmartRect` / `wheelSelectionLocked` / `needFullRedraw`). Add pure setters; delete Sync mirror and all Host dual fields.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_hasSmartRect` | `ScreenshotEditorSetHasSmartRect` / `ScreenshotEditorHasSmartRect` |
| `m_wheelSelectionLocked` | `ScreenshotEditorSetWheelSelectionLocked` / `ScreenshotEditorIsWheelSelectionLocked` |
| `m_needFullRedraw` | `ScreenshotEditorSetNeedFullRedraw` / `ScreenshotEditorNeedsFullRedraw` |
| `SyncScreenshotSmartHoverFlagsMirror` | deleted |

## Added pure setters

In `ScreenshotEditorState.h`:

- `ScreenshotEditorSetHasSmartRect`
- `ScreenshotEditorSetWheelSelectionLocked`
- `ScreenshotEditorSetNeedFullRedraw`

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — pure setters
- `src/window/OverlayWindow.h` — fields + Sync decl deleted (**282→281** phys)
- `src/window/OverlayWindow.cpp` — all dual writes rewritten
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for deleted symbols: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **281** |
| smart-hover dual fields | **0** |

## NEXT

S-B-15: next ownership domain among remaining real dual-write Sync clusters (path counts, crop/session geometry, annotation geometry scratch, last hover magnifier cache, hovered toolbar chrome, smart-detection request, etc.) — one domain per slice.
