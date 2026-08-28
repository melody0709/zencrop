# S-B-9 evidence: slider / color-picker drag sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-9  
Prior: S-B-8 `7c2490fb`

## Intent

Delete dual-write Host fields for **toolbar drag interaction** ownership domain (slider + color-picker). Sole store is `m_editorState` (`isDraggingSlider` / `draggingSlider` / slider LTRB / `isDraggingColorPicker` / `draggingColorPicker` / color-picker LTRB). Delete corresponding Sync methods and all call sites.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_isDraggingScreenshotSlider` | `m_editorState.isDraggingSlider` via `ScreenshotEditorSyncSliderDrag` / `ScreenshotEditorIsDraggingSlider` |
| `m_draggingScreenshotSlider` | `m_editorState.draggingSlider` |
| `m_screenshotSliderDragRect` | `m_editorState.sliderDrag*` |
| `m_isDraggingScreenshotColorPicker` | `m_editorState.isDraggingColorPicker` via `ScreenshotEditorSyncColorPickerDrag` / pure readers |
| `m_draggingScreenshotColorPicker` | `m_editorState.draggingColorPicker` |
| `m_screenshotColorPickerDragRect` | `m_editorState.colorPickerDrag*` |
| `SyncScreenshotSliderDragMirror` | deleted |
| `SyncScreenshotColorPickerDragMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — fields + Sync decls deleted (**297→290** phys)
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync defs deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — LButtonDown/Up rewritten to pure Sync

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
| OverlayWindow.h phys | **290** |
| slider/color-picker drag dual fields | **0** |

## NEXT

S-B-10: next ownership domain among remaining real dual-write Sync clusters (hover toolbar chrome, annotation interaction flags, toast, geometry/crop, path counts, etc.) — one domain per slice.
