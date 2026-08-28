# S-B-16 evidence: smart-detection request sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-16  
Prior: S-B-15 `c2541a89`

## Intent

Delete dual-write Host fields for **smart-detection request throttle cache** ownership domain. Sole store is `m_editorState` (`lastSmartDetectionRequestX/Y/Tick`). Delete Sync mirror. Reads already used pure helpers.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_lastSmartDetectionRequestPoint` | pure `ScreenshotEditorLastSmartDetectionRequestX/Y` + `ScreenshotEditorSyncSmartDetectionRequest` |
| `m_lastSmartDetectionRequestTick` | pure `ScreenshotEditorLastSmartDetectionRequestTick` |
| `SyncScreenshotSmartDetectionRequestMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — fields + Sync decl deleted (**279→276** phys)
- `src/window/OverlayWindow.cpp` — schedule/clear writes → pure Sync
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
| OverlayWindow.h phys | **276** |
| smart-detection request dual fields | **0** |

## NEXT

S-B-17: next ownership domain among remaining real dual-write Sync clusters (path counts, crop/session geometry, annotation geometry scratch, last hover magnifier cache, crop drag session, screen hover geometry, etc.) — one domain per slice.
