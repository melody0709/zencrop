# S-B-23 evidence: isScreenshotMode sole authority

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-23  
Prior: S-B-22 `7e372381`

## Intent

Delete dual-write Host field for **isScreenshotMode** ownership domain (split from OWN-92 crop geometry cluster). Sole store is `m_editorState.isScreenshotMode`. Screenshot-mode ctor seeds pure via `ScreenshotEditorSetIsScreenshotMode`; residual `SyncScreenshotCropGeometryMirror` re-reads pure flag (no Host dual field).

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_isScreenshotMode` | pure `ScreenshotEditorIsScreenshotMode` / `ScreenshotEditorSetIsScreenshotMode` |

## Touch paths

- `src/window/OverlayWindow.h` — dual field deleted; OWN-92 comment updated
- `src/screenshot/OverlayWindowScreenshot.inl` — screenshot ctor seeds pure `true`
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync re-reads pure flag
- `src/screenshot/editor/ScreenshotEditorState.h` — pure setter added

Non-screenshot OverlayWindow ctor leaves pure default `false` (correct).

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `m_isScreenshotMode`: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **243** |
| isScreenshotMode dual field | **0** |

## NEXT

S-B-24: next ownership domain among remaining real dual-write clusters (OWN-92 residual crop/smart/adjust/lastDrawn/suppressed, history flags method delete, aspect ratio helper) — one domain per slice.
