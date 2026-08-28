# S-B-20 evidence: crop-drag session sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-20  
Prior: S-B-19 `83d6e2e3`

## Intent

Delete dual-write Host fields for **crop-drag session** ownership domain. Sole store is `m_editorState` (`isCropDragging` / `cropStart*` / `cropCurrent*` / `cropClickStart*` / `adjustActionOrdinal` / `lastSmartPoint*`). Product reads rewritten to pure helpers; every former `SyncScreenshotCropDragSessionMirror` call site now calls pure `ScreenshotEditorSyncCropDragSession` with overrides.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_isDragging` | `ScreenshotEditorIsCropDragging` / Sync |
| `m_startPoint` | pure `ScreenshotEditorCropStart*` |
| `m_currentPoint` | pure `ScreenshotEditorCropCurrent*` |
| `m_clickStartPoint` | pure `ScreenshotEditorCropClickStart*` |
| `m_adjustAction` | pure `ScreenshotEditorAdjustActionOrdinal` |
| `m_lastSmartPoint` | pure `ScreenshotEditorLastSmartPoint*` |
| `SyncScreenshotCropDragSessionMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — 6 fields + Sync decl deleted (**258→249** phys)
- `src/window/OverlayWindow.cpp` — all dual writes → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — `m_currentPoint` reads → pure POINT
- `src/screenshot/overlay/OverlayWindowScreenshot.Surface.inl` — drag/currentPoint reads → pure

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for deleted symbols (excluding HoverMagnifierWidget own members): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **249** |
| crop-drag dual fields | **0** |

## NEXT

S-B-21: next ownership domain among remaining real dual-write Sync clusters (crop geometry, screen hover geometry, SyncScreenshotEditorState residual, history flags, aspect ratio helper, etc.) — one domain per slice.
