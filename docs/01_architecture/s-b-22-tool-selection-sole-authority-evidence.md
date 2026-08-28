# S-B-22 evidence: tool + selection sole authority

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-22  
Prior: S-B-21 `c515d1a7`

## Intent

Delete dual-write Host fields for **active tool + annotation selection** ownership domain. Sole store is `m_editorState` (`activeTool` / `selectedAnnotationIndex`). Product reads already used pure helpers; Host facades no longer dual-write legacy fields.

**Note:** Full OWN-92 crop geometry cutover aborted mid-slice (Sync nested inside multi-arg Apply* call sites — same corruption mode as prior attempt). Restored to S-B-21 clean, pivoted to this smaller real dual-write domain.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_activeScreenshotTool` | pure `ScreenshotEditorActiveTool` / `ScreenshotEditorSelectTool` |
| `m_selectedScreenshotAnnotationIndex` | pure `ScreenshotEditorSelectedAnnotationIndex` / `ScreenshotEditorSelectAnnotation` |

## Rewritten Host facades (kept, no dual-write)

| Method | After |
|---|---|
| `SetActiveScreenshotTool` | pure SelectTool + history projection only |
| `SetSelectedScreenshotAnnotationIndex` | SetAnnotationCount + clamp + SelectAnnotation |
| `SyncScreenshotEditorState` | project Host vector size + history; re-clamp selection (no tool/selection dual-write) |

Host still owns `m_screenshotAnnotations` vector and `m_annotationHistory` (runtime collections).

## Touch paths

- `src/window/OverlayWindow.h` — 2 dual fields deleted (**244→243** phys)
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Set*/Sync rewritten

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
| OverlayWindow.h phys | **243** |
| tool/selection dual fields | **0** |

## NEXT

S-B-23: next ownership domain among remaining real dual-write Sync clusters (crop geometry OWN-92 carefully with local-RECT pattern, history flags method delete, aspect ratio helper, SyncScreenshotEditorState residual if any Host dual remains) — one domain per slice.
