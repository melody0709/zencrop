# S-B-12 evidence: tool-settings dirty sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-12  
Prior: S-B-11 `8f05fbcd`

## Intent

Delete dual-write Host field for **tool-settings dirty** ownership domain. Sole store is `m_editorState.toolSettingsDirty`. Delete Sync mirror. Keep Host methods `MarkScreenshotToolSettingsDirty` / `FlushScreenshotToolSettingsIfDirty` as thin adapters over pure state.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_screenshotToolSettingsDirty` | `m_editorState.toolSettingsDirty` via `ScreenshotEditorSyncToolSettingsDirty` / `ScreenshotEditorIsToolSettingsDirty` |
| `SyncScreenshotToolSettingsDirtyMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — field + Sync decl deleted (**284→283** phys)
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Mark/Flush/load/save rewrite; Sync def deleted

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
| OverlayWindow.h phys | **283** |
| toolSettingsDirty dual field | **0** |

## NEXT

S-B-13: next ownership domain among remaining real dual-write Sync clusters (toolbar rect, path counts, smart-hover flags, crop/session geometry, annotation geometry scratch, last hover magnifier cache, hovered toolbar chrome, etc.) — one domain per slice.
