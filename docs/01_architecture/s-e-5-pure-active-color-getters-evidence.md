# S-E-5 evidence: pure active color getters (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-5  
Prior: S-E residual `117cc97a`

## Intent

**Ownership domain (single slice):** Host `GetActiveScreenshotColor` + `GetActiveScreenshotColorIndex` dual-own pure color-resolution authority (pure state reads only). Move both to pure `ScreenshotActiveColor.h`; Host call sites use pure sole APIs; Host methods deleted.

## Deleted Host authority

| Legacy Host method | Sole pure API |
|---|---|
| `GetActiveScreenshotColorIndex` | `ScreenshotEditorActiveColorIndex` |
| `GetActiveScreenshotColor` | `ScreenshotEditorActiveColor` |

`SetActiveScreenshotColorIndex` / `SetActiveScreenshotCustomColor` remain Host (ApplyStyle side-effects).

## Touch paths

- `src/screenshot/editor/ScreenshotActiveColor.h` — **new** pure sole getters
- `src/screenshot/OverlayWindowScreenshot.inl` — include pure header
- `src/window/OverlayWindow.h` — 2 method decls deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — method defs deleted; call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — call sites pure

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 59/59
```

Live product scan for Host `GetActiveScreenshotColor` / `GetActiveScreenshotColorIndex` (non-comment): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **59/59** |
| Host active-color getters | **0** |
| Stage 2 code commits | **~44** (near 警戒 45；合域强制) |

## Granularity note

One domain: 2 related pure getters + Host dual delete + all call sites. Not two 1-getter slices.

## NEXT

S-E residual recheck / Document dual-write seed / package exit check. Stage2 near 警戒 — **合域强制**. **Must prefill domain list before src edits.**
