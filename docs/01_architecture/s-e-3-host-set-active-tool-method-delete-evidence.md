# S-E-3 evidence: Host SetActiveScreenshotTool method delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-3  
Prior: S-E-2 `39413fb9`

## Intent

**Ownership domain (single slice):** Host method `SetActiveScreenshotTool` dual-owns active-tool selection + history projection. Move sole path to pure `ScreenshotEditorSelectToolWithHistory`; rewrite all call sites; delete Host method decl+def.

## Deleted Host authority

| Legacy Host method | Sole pure API |
|---|---|
| `OverlayWindow::SetActiveScreenshotTool` | `ScreenshotEditorSelectToolWithHistory` |

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — pure `ScreenshotEditorSelectToolWithHistory`
- `src/window/OverlayWindow.h` — method decl deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — method def deleted; 3 call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — 2 call sites pure

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 59/59
```

Live product scan for `SetActiveScreenshotTool` (non-comment): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **59/59** |
| Host active-tool method | **0** |
| Stage 2 code commits | **~42** |

## Granularity note

One domain: Host active-tool method + all call sites + pure helper. Not method-delete / call-site two slices.

## NEXT

S-E-4: selected stable id dual-write / AnnotationDocument ownership / residual Host methods. **Must prefill domain list before src edits.**
