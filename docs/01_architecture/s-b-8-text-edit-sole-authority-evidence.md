# S-B-8 evidence: text-edit sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-8  
Prior: S-B-7 `83375675`

## Intent

Delete dual-write Host fields for **text-edit** ownership domain. Sole store is `m_editorState` (`textCaretIndex` / `textSelectionAnchor` / `editingTextIndex` / `pendingTextAnnotationCreateId`). Delete corresponding Sync methods and all call sites.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_screenshotTextCaretIndex` | `m_editorState.textCaretIndex` via `ScreenshotEditorSyncTextEditCaret` / `ScreenshotEditorTextCaretIndex` |
| `m_screenshotTextSelectionAnchor` | `m_editorState.textSelectionAnchor` via same Sync / `ScreenshotEditorTextSelectionAnchor` |
| `m_editingScreenshotTextIndex` | `m_editorState.editingTextIndex` via `ScreenshotEditorSyncTextEditingIndex` / pure readers |
| `m_pendingTextAnnotationCreateId` | `m_editorState.pendingTextAnnotationCreateId` via `ScreenshotEditorSyncPendingTextAnnotationCreateId` |
| `SyncScreenshotTextEditCaretMirror` | deleted |
| `SyncScreenshotTextEditingIndexMirror` | deleted |
| `SyncScreenshotPendingTextAnnotationCreateIdMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — fields + Sync decls deleted (**303→297** phys)
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync defs deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — all dual writes rewritten
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
| OverlayWindow.h phys | **297** |
| text-edit dual fields | **0** |

## NEXT

S-B-9: next ownership domain (e.g. slider/color-picker drag, hover toolbar chrome, annotation interaction flags, toast) — one domain per slice.
