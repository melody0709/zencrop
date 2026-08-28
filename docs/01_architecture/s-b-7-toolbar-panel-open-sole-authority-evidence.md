# S-B-7 evidence: toolbar panel-open sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-7  
Prior: S-B-CLEANUP `084c15dd`

## Intent

Delete dual-write Host fields for **toolbar panel-open** ownership domain. Sole store is `m_editorState` (`morePanelOpen` / `openToolGroup` / `openTertiary`). Delete corresponding Sync methods and all call sites. Net-delete fields + Sync (not helper-only).

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_screenshotMoreOpen` | `m_editorState.morePanelOpen` via `ScreenshotEditorSyncMorePanelOpen` / `ScreenshotEditorIsMorePanelOpen` |
| `m_openScreenshotToolGroup` | `m_editorState.openToolGroup` via `ScreenshotEditorSyncOpenToolbarPanels` / `ScreenshotEditorOpenToolGroup` / `IsOpenToolGroup` |
| `m_openScreenshotTertiary` | `m_editorState.openTertiary` via same Sync + `OpenTertiary` / `IsOpenTertiary` |
| `SyncScreenshotMorePanelOpenMirror` | deleted |
| `SyncScreenshotOpenToolbarPanelsMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — field + Sync decls deleted (**306→303** phys)
- `src/window/OverlayWindow.cpp` — tertiary writes → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync defs deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — all dual writes rewritten

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
| OverlayWindow.h phys | **303** |
| panel-open dual fields | **0** |
| no-op Sync residual | **0** (CLEANUP) |

## NEXT

S-B-8: next ownership domain among remaining real dual-write Sync clusters (e.g. text-edit caret/index, slider/color-picker drag, annotation interaction flags) — one domain per slice.
