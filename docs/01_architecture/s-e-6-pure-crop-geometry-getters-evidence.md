# S-E-6 evidence: pure crop geometry getters (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-6  
Prior: S-E-5 `32f47c8c`

## Intent

**Ownership domain (single slice):** Host `GetCropRect` + `GetCropBounds` dual-own pure crop geometry authority (pure state reads only). Move both to pure `ScreenshotEditorCropDragRect` / `ScreenshotEditorCropBounds`; Host call sites use pure sole APIs; Host methods deleted.

## Deleted Host authority

| Legacy Host method | Sole pure API |
|---|---|
| `GetCropRect` (cropStart/cropCurrent normalize) | `ScreenshotEditorCropDragRect` |
| `GetCropBounds` (screen vs hovered by mode) | `ScreenshotEditorCropBounds` |

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — pure crop drag rect + crop bounds
- `src/window/OverlayWindow.h` — 2 method decls deleted
- `src/window/OverlayWindow.cpp` — method defs deleted; call sites pure
- `src/screenshot/overlay/OverlayWindowScreenshot.Surface.inl` — call site pure

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 59/59
```

Live product scan for Host `GetCropRect` / `GetCropBounds` (non-comment): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **59/59** |
| Host crop geometry getters | **0** |
| Stage 2 code commits | **~45** (**警戒**; 合域强制; 硬停 55) |

## Granularity note

One domain: 2 related pure crop getters + Host dual delete + all call sites. Not two 1-getter slices.

## NEXT

S-E residual recheck at 警戒: Document dual-write is multi-slice vertical work — prefer residual inventory docs + package status, or one large domain only. **Must prefill domain list before src edits.** Stage2 at 警戒 — no 1-field slices; merge domains.
