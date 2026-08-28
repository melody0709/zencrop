# S-B-CLEANUP evidence: delete no-op Screenshot Sync mirrors

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-CLEANUP  
Product HEAD prior: Stage1 Gate `3f55a6a3` / S-B-6 pause `16580b6a`

## Intent

After Stage1 Gate PASS, remove **no-op** `SyncScreenshot*Mirror` methods left by S-B-1..6 field cutovers (ADR-001: ban no-op Sync). Net-delete declarations, definitions, and all call sites.

## Deleted (no-op only)

| Method | Origin slice |
|---|---|
| `SyncScreenshotToolStyleMirror` | S-B-1 |
| `SyncScreenshotToolModesMirror` | S-B-2 |
| `SyncScreenshotTextStyleMirror` | S-B-3 |
| `SyncScreenshotWatermarkStyleMirror` | S-B-4 |
| `SyncScreenshotHighLightStyleMirror` | S-B-4 |
| `SyncScreenshotMagnifierStyleMirror` | S-B-4 |
| `SyncScreenshotToolGroupMemoryMirror` | S-B-6 |
| `SyncScreenshotPostProcessStyleMirror` | S-B-5 |
| `SyncScreenshotEffectStyleMirror` | S-B-5 |
| `SyncScreenshotCropPrefsMirror` | S-B-5 |
| `SyncScreenshotHoverMagnifierPrefsMirror` | S-B-5 |
| `SyncScreenshotChromeTogglesMirror` | S-B-6 |
| `SyncScreenshotColorPickerStateMirror` | S-B-6 |
| `SyncScreenshotColorIndicesMirror` | S-B-6 |
| `SyncScreenshotFunctionAreaPrefsMirror` | S-B-6 |
| `SyncScreenshotSpecializedStylesMirror` | convenience → no-ops |
| `SyncScreenshotRemainingPrefsMirror` | convenience → no-ops |

## Touch paths

- `src/window/OverlayWindow.h` — 17 decls removed  
- `src/window/OverlayWindow.cpp` — call sites  
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — defs + call sites  
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl`  
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl`  
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl`

## Not deleted (still real dual-write / Host runtime Sync)

Remaining `SyncScreenshot*` that still write pure `m_editorState` from legacy Host fields (OWN-88+ geometry/drag/toolbar/text/etc.) — deferred to **S-B-7+** ownership cutovers.

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| no-op SyncScreenshot* residual | **0** |
| remaining real SyncScreenshot* defs (Settings.inl) | **24** |
| OverlayWindow.h phys | **306** |

## NEXT

S-B-7: next ownership domain cutover (delete remaining dual-write Host fields + real Sync for one cluster), not helper-only.
