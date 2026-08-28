# S-E-2 evidence: Host SetSelectedScreenshotAnnotationIndex method delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-2  
Prior: S-E-1 `87bb849d`

## Intent

**Ownership domain (single slice):** Host method `SetSelectedScreenshotAnnotationIndex` dual-owns selection projection (count + clamp + select). Move sole path to pure `ScreenshotEditorSetAnnotationCountAndSelect`; rewrite all call sites; delete Host method decl+def.

## Deleted Host authority

| Legacy Host method | Sole pure API |
|---|---|
| `OverlayWindow::SetSelectedScreenshotAnnotationIndex` | `ScreenshotEditorSetAnnotationCountAndSelect` |

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — pure `ScreenshotEditorSetAnnotationCountAndSelect`
- `src/window/OverlayWindow.h` — method decl deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — method def deleted; call site pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — 15 call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — call sites pure; projectMutation simplified

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 59/59
```

Live product scan for `SetSelectedScreenshotAnnotationIndex` (non-comment): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **59/59** |
| Host selection method | **0** |
| Stage 2 code commits | **~41** |

## Granularity note

One domain: Host selection method + all call sites + pure helper. Not method-delete / call-site two slices.

## NEXT

S-E-3: selected stable id dual-write / AnnotationDocument ownership seam; or residual Host vector dual. **Must prefill domain list before src edits.**
