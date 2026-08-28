# S-B-13 evidence: toolbar rect sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-13  
Prior: S-B-12 `cf239f71`

## Intent

Delete dual-write Host field for **toolbar rect** ownership domain. Sole store is `m_editorState` (`toolbarRectLeft/Top/Right/Bottom`). Delete Sync mirror. DrawScreenshotToolbar writes pure Sync only.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_screenshotToolbarRect` | `ScreenshotEditorSyncToolbarRect` / pure `ScreenshotEditorToolbarRect*` readers |
| `SyncScreenshotToolbarRectMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — field + Sync decl deleted (**283→282** phys)
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — clear/set → pure Sync

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
| OverlayWindow.h phys | **282** |
| toolbar rect dual field | **0** |

## NEXT

S-B-14: next ownership domain among remaining real dual-write Sync clusters (path counts, smart-hover flags, crop/session geometry, annotation geometry scratch, last hover magnifier cache, hovered toolbar chrome, etc.) — one domain per slice.
