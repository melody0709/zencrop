# S-B-11 evidence: hover toolbar / toast / active-handle sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-11  
Prior: S-B-10 `14889bf8`

## Intent

Delete dual-write Host fields for **OWN-86 chrome feedback** ownership domain: hover toolbar/side/tooltip, toast text/tick, active annotation handle/point index. Sole store is `m_editorState`. Delete corresponding Sync methods and all call sites. Rewrite product reads to pure helpers.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_hoveredScreenshotToolbarButton` | `ScreenshotEditorHoveredToolbarButton` / `ScreenshotEditorSyncHoverToolbar` |
| `m_hoveredSideButton` | `ScreenshotEditorHoveredSideButton` |
| `m_screenshotToolbarTooltipVisible` | `ScreenshotEditorIsToolbarTooltipVisible` |
| `m_toastText` / `m_toastStartTick` | `ScreenshotEditorSyncToast` / pure toast readers |
| `m_screenshotAnnotationActiveHandle` | `ScreenshotEditorActiveAnnotationHandle` / `ScreenshotEditorSyncActiveAnnotationHandle` |
| `m_screenshotAnnotationActivePointIndex` | `ScreenshotEditorActiveAnnotationPointIndex` |
| `SyncScreenshotHoverToolbarMirror` | deleted |
| `SyncScreenshotToastMirror` | deleted |
| `SyncScreenshotActiveAnnotationHandleMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — fields + Sync decls deleted (**288→284** phys)
- `src/window/OverlayWindow.cpp` — hover/toast writes → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync defs deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — active handle writes → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — side hover read → pure reader

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
| OverlayWindow.h phys | **284** |
| OWN-86 dual fields | **0** |

## NEXT

S-B-12: next ownership domain among remaining real dual-write Sync clusters (geometry scratch OWN-87, crop/path/session OWN-91/92/93, path counts, tool-settings dirty, toolbar rect, last hover magnifier cache, etc.) — one domain per slice.
