# S-B-31 evidence: SyncScreenshotEditorState method delete

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-31  
Prior: S-B-30 `a4533c26`

## Intent

Delete residual Host method `SyncScreenshotEditorState` (projection of Host-owned annotation vector size + history into pure state, re-clamp selection). No dual-write Host fields remain for tool/selection (S-B-22). Call sites inline pure projection helpers.

## Deleted Host authority

| Legacy method | Sole authority |
|---|---|
| `SyncScreenshotEditorState` | pure `ScreenshotEditorSetAnnotationCount` + `ScreenshotEditorSelectAnnotation` + `ScreenshotEditorSetHistoryAvailability` (inlined) |

## Touch paths

- `src/window/OverlayWindow.h` — Sync decl deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted; LoadScreenshotToolSettings inlines projection
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — 4 call sites inlined

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `SyncScreenshot*` (excluding deletion comments): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **237** |
| residual Host dual-write Sync methods | **0** |

## S-B dual-write Sync status

All former dual-write Sync mirrors deleted (S-B-7..31). Host still owns runtime collections (`m_screenshotAnnotations`, `m_annotationHistory`, path point vectors, HWND, GDI resources) — projected into pure state at mutation sites.

## NEXT

S-B package residual inventory: remaining Host dual-write *fields* (if any) vs Host runtime ownership. Stage 2 package exit check per GOAL §11 when S-B ownership dual-write fields = 0 and Gate criteria met (Annotation single runtime authority; no production class-method `.inl`; etc.). Next packages: S-C Command 分类 · S-D AnnotationValue · S-E/F Document+Renderer · S-G Toolbar · S-H Host/TU.
