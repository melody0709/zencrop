# S-H residual — geometry scratch thin setters; God function shorten

Date: 2026-07-23  
Package: Stage 2 **S-H Host residual**  
Prior: direction correction `4c6433c5`  
Code HEAD: this commit

## Intent

Shorten Host God function `HandleScreenshotLButtonDown` by deleting dual full-sync re-read bodies for annotation geometry scratch.

| Before | After |
|---|---|
| 42× `ScreenshotEditorSyncAnnotationGeometryScratch` with 22 args (most re-reads) | thin field patch setters |
| 7 pure no-op full re-syncs | **deleted** |
| LButtonDown ~1603 lines | **~747 lines** |
| AnnotationEdit.cpp ~2928 lines | **~2068 lines** (−1008) |

## Ownership domain

Geometry scratch mid-edit fields remain sole on `ScreenshotEditorState`.  
This knife deletes dual **full-field re-read write** call-site bodies (no-op / partial override dual verbosity), not a new authority.

## Landed

| Item | Path |
|---|---|
| Thin setters | `ScreenshotEditorState.h` — SetAnnotationStart/Current/StartCurrent/MoveAnchor/OriginalRect/Aux/Source/ResizeFixed/RoundedRadius/Angle/TextFontSize/RotateStartMouseAngle |
| Call-site collapse | `OverlayWindowScreenshot.AnnotationEdit.cpp` — 7 no-ops deleted; 35 converted; 0 full Sync remaining |
| Residual no-op | `OverlayWindow.cpp` ESC broken-line cancel — full re-sync no-op deleted |

## KPI

| Metric | Before | After |
|---|---:|---:|
| HandleScreenshotLButtonDown lines | ~1603 | **~747** |
| AnnotationEdit.cpp lines | ~2928 | **~2068** |
| SyncAnnotationGeometryScratch call sites | 42 | **0** |
| Screenshot family LOC | ~28902 | **~28029** (≤30640) |
| hermetic | 68/68 | **68/68** |

## Ban check

- Same commit: land thin setters + delete dual full-sync call sites
- Not helper-only: Host God function substantively shortened
- Net delete −861 LOC this slice (AnnotationEdit −1008 / setters +88)

## Residual (Stage2 Gate still NOT PASS)

1. LButtonDown still ~747 lines (tool create branches)
2. DrawScreenshotToolbar ~2448 / ToolbarInteraction ~1728 / OverlayWindow ~2498 / Messages ~2447
3. Full S-A pixel golden still ABSENT
4. Stage2 formal Gate still **NOT PASS** until more God function ownership cuts

## NEXT

Continue S-H residual: extract pure hit/create decision helpers from LButtonDown / ToolbarCommand / DrawToolbar; keep family LOC ≤30640; more God function shorten.
