# S-B-18 evidence: path point counts sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-18  
Prior: S-B-17 `71e015ea`

## Intent

Delete dual-write **Sync** for freehand/broken-line path point **counts**. Sole store for counts is `m_editorState` (`freehandPointCount` / `brokenLinePointCount`). Host still owns the live point vectors (`m_screenshotFreehandPoints` / `m_screenshotBrokenLinePoints`) as runtime collections; every vector mutation now calls pure `ScreenshotEditorSyncPathPointCounts` directly. Delete `SyncScreenshotPathPointCountsMirror` method.

## Deleted Host authority

| Legacy | Sole authority |
|---|---|
| `SyncScreenshotPathPointCountsMirror` | deleted; call sites → `ScreenshotEditorSyncPathPointCounts(m_editorState, freehand.size(), broken.size())` |
| dual-write count mirror path | pure readers already: `ScreenshotEditorFreehandPointCount` / `BrokenLinePointCount` / `Has*Points` |

## Retained Host runtime (not dual-write fields)

- `m_screenshotFreehandPoints` / `m_screenshotBrokenLinePoints` — actual point data for paint/commit (Host collections; not editor-state dual fields)

## Touch paths

- `src/window/OverlayWindow.h` — Sync decl deleted (**272→271** phys)
- `src/window/OverlayWindow.cpp`
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl`

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `SyncScreenshotPathPointCountsMirror`: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **271** |
| path-count Sync residual | **0** |

## NEXT

S-B-19: next ownership domain among remaining real dual-write Sync clusters (crop drag session, crop/screen geometry, annotation geometry scratch, etc.) — one domain per slice. Host point vectors may stay until annotation model cutover.
