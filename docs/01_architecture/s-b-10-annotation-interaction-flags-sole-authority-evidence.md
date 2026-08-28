# S-B-10 evidence: annotation interaction flags sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-10  
Prior: S-B-9 `c6a3983a`

## Intent

Delete dual-write Host fields for **annotation interaction flags** ownership domain. Sole store is `m_editorState` (`isDrawingAnnotation` / `isDrawingBrokenLinePath` / `isMovingAnnotation` / `isResizingAnnotation` / `isRotatingAnnotation` / `isHoldingRefresh`). Add pure setters; delete bulk Sync mirror and all Host dual fields.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_isDrawingScreenshotAnnotation` | `ScreenshotEditorSetDrawingAnnotation` / pure reader |
| `m_isDrawingScreenshotBrokenLinePath` | `ScreenshotEditorSetDrawingBrokenLinePath` |
| `m_isMovingScreenshotAnnotation` | `ScreenshotEditorSetMovingAnnotation` |
| `m_isResizingScreenshotAnnotation` | `ScreenshotEditorSetResizingAnnotation` |
| `m_isRotatingScreenshotAnnotation` | `ScreenshotEditorSetRotatingAnnotation` |
| `m_isHoldingScreenshotRefresh` | `ScreenshotEditorSetHoldingRefresh` |
| `SyncScreenshotAnnotationInteractionMirror` | deleted |

## Added pure setters

In `ScreenshotEditorState.h`:

- `ScreenshotEditorSetDrawingAnnotation`
- `ScreenshotEditorSetDrawingBrokenLinePath`
- `ScreenshotEditorSetMovingAnnotation`
- `ScreenshotEditorSetResizingAnnotation`
- `ScreenshotEditorSetRotatingAnnotation`
- `ScreenshotEditorSetHoldingRefresh`

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — pure setters
- `src/window/OverlayWindow.h` — fields + Sync decl deleted (**290→288** phys)
- `src/window/OverlayWindow.cpp`
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl`

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
| OverlayWindow.h phys | **288** |
| annotation interaction dual fields | **0** |

## NEXT

S-B-11: next ownership domain (hover toolbar chrome, toast, active handle, geometry scratch, crop/path/session dual-write, etc.) — one domain per slice.
