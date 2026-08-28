# S-B-17 evidence: last hover-magnifier cache sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-17  
Prior: S-B-16 `55953527`

## Intent

Delete dual-write Host fields for **last hover-magnifier refresh cache** ownership domain. Sole store is `m_editorState` (`lastHoverMagnifierPoint*` / `lastHoverMagnifierRect*` / `lastHoverMagnifierUpdateTick`). Delete Sync mirror. Reads already used pure helpers for throttle.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_lastHoverMagnifierUpdateTick` | pure `ScreenshotEditorLastHoverMagnifierUpdateTick` + `ScreenshotEditorSyncLastHoverMagnifierCache` |
| `m_lastHoverMagnifierPoint` | pure point readers |
| `m_lastHoverMagnifierActiveRect` | pure rect readers |
| `SyncScreenshotLastHoverMagnifierCacheMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — fields + Sync decl deleted (**276→272** phys)
- `src/window/OverlayWindow.cpp` — reset/update/cache writes → pure Sync
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
| OverlayWindow.h phys | **272** |
| last hover-magnifier dual fields | **0** |

## NEXT

S-B-18: next ownership domain among remaining real dual-write Sync clusters (path counts, crop/session geometry, annotation geometry scratch, crop drag session, screen hover geometry, etc.) — one domain per slice.
