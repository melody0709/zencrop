# S-B-21 evidence: screen-hover geometry sole authority

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B-21  
Prior: S-B-20 `09bbb989`

## Intent

Delete dual-write Host fields for **screen-hover geometry** ownership domain (OWN-93). Sole store is `m_editorState` (`screenRect*` / `targetRect*` / `hoveredRect*` / `pendingCropRect*` / `hasHoveredWindow`). Product reads rewritten to pure helpers; every former `SyncScreenshotScreenHoverGeometryMirror` call site now calls pure `ScreenshotEditorSyncScreenHoverGeometry` with overrides.

**HWND Host remains:** `m_hoveredWindow` stays Host runtime; pure state holds only `hasHoveredWindow` bool.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_screenRect` | pure `ScreenshotEditorScreenRect*` / Sync |
| `m_targetRect` | pure `ScreenshotEditorTargetRect*` / Sync |
| `m_hoveredRect` | pure `ScreenshotEditorHoveredRect*` / Sync |
| `m_pendingCropRect` | pure `ScreenshotEditorPendingCropRect*` / Sync |
| `SyncScreenshotScreenHoverGeometryMirror` | deleted |

## Pure helpers (no windows.h in pure header)

- `ScreenshotEditorRect` / `ScreenshotEditorPoint` POD views with optional Win32 conversion (`operator RECT` / `operator POINT` when `_WINDEF_` present)
- `ScreenshotEditorScreenRect` / `TargetRect` / `HoveredRect` / `PendingCropRect` return pure POD
- Component readers already existed (OWN-93)

## Touch paths

- `src/window/OverlayWindow.h` — 4 RECT fields + Sync decl deleted (**249→244** phys)
- `src/window/OverlayWindow.cpp` — dual writes → pure Sync; product reads → pure helpers
- `src/screenshot/OverlayWindowScreenshot.inl` — screenshot-mode ctor seed → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.Surface.inl` / `Export.inl` / `ToolbarInteraction.inl` — product reads
- `src/screenshot/editor/ScreenshotEditorState.h` — pure RECT/POINT POD views; OWN-93 sole-authority note

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for deleted symbols (excluding deletion comments): **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **244** |
| screen-hover dual RECT fields | **0** |
| `m_hoveredWindow` HWND Host | **kept** |

## NEXT

S-B-22: next ownership domain among remaining real dual-write Sync clusters (crop geometry OWN-92, SyncScreenshotEditorState residual, history flags, aspect ratio helper, etc.) — one domain per slice.
