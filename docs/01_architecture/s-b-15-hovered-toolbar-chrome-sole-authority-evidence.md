# S-B-15 evidence: hovered toolbar chrome sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-15  
Prior: S-B-14 `bb6e8fea`

## Intent

Delete dual-write Host fields for **hovered toolbar chrome** ownership domain (rect + label used for tooltip timing/hit-test). Sole store is `m_editorState` (`hoveredToolbarRect*` / `hoveredToolbarLabel`). Delete Sync mirror. Product reads use pure helpers.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_hoveredScreenshotToolbarRect` | pure `ScreenshotEditorHoveredToolbarRect*` + `ScreenshotEditorSyncHoveredToolbarChrome` |
| `m_hoveredScreenshotToolbarLabel` | `ScreenshotEditorHoveredToolbarLabel` / same Sync |
| `SyncScreenshotHoveredToolbarChromeMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — fields + Sync decl deleted (**281→279** phys)
- `src/window/OverlayWindow.cpp` — tooltip timer hit-test + hover update rewrite
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
| OverlayWindow.h phys | **279** |
| hovered toolbar chrome dual fields | **0** |

## NEXT

S-B-16: next ownership domain among remaining real dual-write Sync clusters (path counts, crop/session geometry, annotation geometry scratch, last hover magnifier cache, smart-detection request, etc.) — one domain per slice.
