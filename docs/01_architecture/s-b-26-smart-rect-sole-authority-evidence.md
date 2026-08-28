# S-B-26 evidence: smartRect sole authority

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-26  
Prior: S-B-25 `156b0555`

## Intent

Delete dual-write Host field for **smartRect** ownership domain (split from OWN-92 crop geometry cluster). Sole store is `m_editorState` (`smartRect*`). Mutation sites call pure `ScreenshotEditorSyncSmartRect`; product reads use pure `ScreenshotEditorSmartRect` (named locals for PtInRect/EqualRect); residual `SyncScreenshotCropGeometryMirror` re-reads pure smartRect.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_smartRect` | pure `ScreenshotEditorSmartRect*` / SyncSmartRect |

## Touch paths

- `src/window/OverlayWindow.h` — dual field deleted
- `src/window/OverlayWindow.cpp` — mutations → pure Sync; product reads → pure
- `src/screenshot/overlay/OverlayWindowScreenshot.Surface.inl` — drawRect product read
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — residual Sync re-reads pure smartRect
- `src/screenshot/editor/ScreenshotEditorState.h` — pure `ScreenshotEditorSyncSmartRect` added

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for `m_smartRect`: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **241** |
| smartRect dual field | **0** |

## NEXT

S-B-27: next ownership domain among remaining real dual-write clusters (OWN-92 residual cropRect / adjust*, history flags method delete, aspect ratio helper) — one domain per slice.
