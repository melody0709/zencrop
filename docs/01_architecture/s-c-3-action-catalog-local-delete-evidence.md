# S-C-3 evidence: ActionCatalog Host Local dual wrappers deleted

Date: 2026-07-22  
Package: Stage 2 S-C Action/Tool/Panel/Control classification  
Slice: S-C-3  
Prior: S-C-2 `3671bcb3`

## Intent

Delete residual Host **Local dual wrappers** for pure `ScreenshotActionCatalog` (migration-era aliases/functions in `OverlayWindowScreenshot.ActionCatalog.inl`). Product call sites rewritten to pure catalog APIs. ActionCatalog.inl reduced to pure header include only.

## Deleted Host authority

| Legacy Local dual wrapper | Sole authority |
|---|---|
| `ScreenshotToolbarIconForCommandLocal` | pure `ScreenshotToolbarIconForCommand` |
| `ScreenshotToolbarTitleForCommandLocal` | pure `ScreenshotToolbarTitleForCommand` |
| `ScreenshotBuildFunctionRowsLocal` | pure `ScreenshotBuildFunctionRows` |
| `ScreenshotJoinFunctionIdsLocal` | pure `ScreenshotJoinFunctionIds` |
| `ScreenshotFunctionVisibilityLocal` / `ActionRowLocal` / `ActionMetaLocal` aliases | pure types |
| `kScreenshotFunctionDefault*Local` | pure `kScreenshotFunctionDefault*` |
| unused Local Meta/Split/Contain/Append/Range helpers | deleted with dual layer |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ActionCatalog.inl` — Local dual layer deleted; pure include only
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — product → pure catalog
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — product → pure catalog
- `src/screenshot/editor/ScreenshotActionCatalog.h` — sole-authority note

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for ActionCatalog Local dual wrappers: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **237** |
| ActionCatalog Local dual wrappers | **0** |

## NEXT

S-C-4 / package residual: typed Action/Tool/Panel map deepen, remaining Host command switches that re-implement pure kind, or S-C package exit check per research §11.3. One ownership domain per slice.

**Pause after this slice** (user request).
