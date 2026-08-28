# S-C-1 evidence: Host command classifier methods deleted

Date: 2026-07-22  
Package: Stage 2 S-C Action/Tool/Panel/Control classification  
Slice: S-C-1  
Prior: S-B residual inventory `e4f42130`

## Intent

Delete residual Host methods that dual-owned command taxonomy classification. Pure classifiers already existed (`ScreenshotIsDrawingToolCommand`, `ScreenshotCommandIsSliderControl`, `ScreenshotCommandIsColorPickerDrag`); Host thin wrappers were the last dual authority for "is drawing tool / slider / color-picker drag". Product call sites rewritten to pure sole classifiers.

## Deleted Host authority

| Legacy method | Sole authority |
|---|---|
| `IsScreenshotToolCommand` | pure `ScreenshotIsDrawingToolCommand` |
| `IsScreenshotSliderCommand` | pure `ScreenshotCommandIsSliderControl` |
| `IsScreenshotColorPickerDragCommand` | pure `ScreenshotCommandIsColorPickerDrag` |

## Touch paths

- `src/window/OverlayWindow.h` — 3 method decls deleted
- `src/screenshot/OverlayWindowScreenshot.inl` — 2 method defs deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — 1 method def deleted; call sites → pure
- `src/screenshot/overlay/OverlayWindowScreenshot.{AnnotationEdit,ToolbarRender,ToolbarInteraction}.inl` — call sites → pure
- `src/screenshot/editor/ScreenshotToolbarCommandGroups.h` / `ScreenshotCommandKind.h` — sole-authority notes

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for deleted Host methods (excluding deletion comments): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **237** |
| Host command classifier methods | **0** |

## NEXT

S-C-2: deepen command taxonomy — exhaustiveness tests for all ScreenshotToolbarCommand values, reduce remaining Host integer-range / switch guessing if any, map Action/Tool/Panel/Control types per research §11.3. One ownership domain per slice.
