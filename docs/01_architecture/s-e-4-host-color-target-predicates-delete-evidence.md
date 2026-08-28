# S-E-4 evidence: Host pure color-target predicates + MarkDirty delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-4  
Prior: S-E-3 `90d73a0b`

## Intent

**Ownership domain (single slice):** Host pure-predicate methods for color-target routing + MarkDirty dual-own pure authority. Move all six color-target predicates + MarkDirty to pure `ScreenshotEditorState` helpers; Host call sites use pure sole APIs; Host methods deleted.

## Deleted Host authority

| Legacy Host method | Sole pure API |
|---|---|
| `IsTextOutlineColorTargetActive` | `ScreenshotEditorIsTextOutlineColorTargetActive` |
| `IsTextBackgroundColorTargetActive` | `ScreenshotEditorIsTextBackgroundColorTargetActive` |
| `IsTextStyleColorTargetActive` | `ScreenshotEditorIsTextStyleColorTargetActive` |
| `IsHighLightStrokeColorTargetActive` | `ScreenshotEditorIsHighLightStrokeColorTargetActive` |
| `IsWatermarkColorTargetActive` | `ScreenshotEditorIsWatermarkColorTargetActive` |
| `IsIndependentScreenshotColorTargetActive` | `ScreenshotEditorIsIndependentColorTargetActive` |
| `MarkScreenshotToolSettingsDirty` | `ScreenshotEditorSyncToolSettingsDirty(state, true)` |

`FlushScreenshotToolSettingsIfDirty` remains Host (Save side-effect).

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — pure color-target predicates
- `src/window/OverlayWindow.h` — 7 method decls deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — method defs deleted; call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — MarkDirty pure
- `src/window/OverlayWindow.cpp` — MarkDirty pure

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 59/59
```

Live product scan for Host `Is*ColorTargetActive` / `MarkScreenshotToolSettingsDirty` (non-comment): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **59/59** |
| Host pure-predicate methods | **0** (was 7) |
| Stage 2 code commits | **~43** |

## Granularity note

One domain: 6 color-target predicates + MarkDirty + call sites + pure helpers. Not seven 1-method slices.

## NEXT

S-E residual inventory / Document ownership / stable-id dual-write. **Must prefill domain list before src edits.**
