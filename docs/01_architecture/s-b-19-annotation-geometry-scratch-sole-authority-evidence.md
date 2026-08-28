# S-B-19 evidence: annotation geometry scratch sole authority

Date: 2026-07-23  
Package: Stage 2 S-B state aggregation  
Slice: S-B-19  
Prior: S-B-18 `3c214ea7`

## Intent

Delete dual-write Host fields for **annotation geometry scratch** ownership domain (draw/move/resize/rotate scratch points + originals + rotate-start mouse angle). Sole store is `m_editorState` annotation geometry fields. Product already used pure readers; Host fields were write-only dual mirrors. Every former `SyncScreenshotAnnotationGeometryScratchMirror` call site now calls pure `ScreenshotEditorSyncAnnotationGeometryScratch` with overrides for mutated values.

## Deleted Host authority

| Legacy field / method | Sole authority |
|---|---|
| `m_screenshotAnnotationStart` / `Current` | pure `ScreenshotEditorAnnotationStart/Current*` |
| `m_screenshotAnnotationMoveAnchor` | pure `ScreenshotEditorAnnotationMoveAnchor*` |
| `m_screenshotAnnotationOriginalStart/End/Aux/Source*` | pure original readers |
| `m_screenshotAnnotationOriginalRoundedRadius/Angle/TextFontSize` | pure original scalar readers |
| `m_screenshotAnnotationResizeFixedPoint` | pure `ScreenshotEditorAnnotationResizeFixed*` |
| `m_screenshotAnnotationRotateStartMouseAngle` | pure `ScreenshotEditorAnnotationRotateStartMouseAngle` |
| `SyncScreenshotAnnotationGeometryScratchMirror` | deleted |

## Touch paths

- `src/window/OverlayWindow.h` — geometry scratch fields + Sync decl deleted (**271→258** phys)
- `src/window/OverlayWindow.cpp` — clear path → pure Sync
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Sync def deleted
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — all dual writes → pure Sync with overrides

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
| OverlayWindow.h phys | **258** |
| geometry scratch dual fields | **0** |

## NEXT

S-B-20: next ownership domain among remaining real dual-write Sync clusters (crop drag session, crop geometry, screen hover geometry, SyncScreenshotEditorState residual, etc.) — one domain per slice.
