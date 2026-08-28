# S-H residual — resize fixed-point pure helper; dual switch delete

Date: 2026-07-23  
Package: Stage 2 **S-H Host residual**  
Prior: geometry scratch thin setters `71eba9e5`  
Code HEAD: this commit

## Intent

Delete dual Host switch bodies that map resize-handle → opposite fixed point.

| Before | After |
|---|---|
| 2× ~45-line switch (selected-handle + hit-start magnifier) | pure `ScreenshotAnnotationResizeFixedPointLocal` |
| LButtonDown ~747 lines | **~661 lines** |
| AnnotationEdit dual switch residual | **0** |

## Ownership domain

Resize fixed-point math is pure geometry (handle + RECT → POINT).  
Host only applies result via thin `ScreenshotEditorSetAnnotationResizeFixed`.

## Landed

| Item | Path |
|---|---|
| Pure helper | `ScreenshotAnnotationHelpers.h/.cpp` — `ScreenshotAnnotationResizeFixedPointLocal` |
| Dual switch delete | `OverlayWindowScreenshot.AnnotationEdit.cpp` — selected-handle path + hit-start magnifier path |

## KPI

| Metric | Before | After |
|---|---:|---:|
| HandleScreenshotLButtonDown lines | ~747 | **~661** |
| Host dual resize-fixed switch bodies | 2 | **0** |
| hermetic | 68/68 | **68/68** |

## Ban check

- Same commit: land pure helper + delete dual Host switch bodies
- Not helper-only: Host God function substantively shortened (−86 LButtonDown)
- Stage2 Gate still **NOT PASS** (God residual)

## Residual

1. LButtonDown still ~661 (tool create / text / serial branches)
2. DrawScreenshotToolbar ~2448 / ToolbarInteraction ~1728 / OverlayWindow ~2474
3. Full S-A pixel golden ABSENT

## NEXT

Continue S-H residual: seed-original-geometry composite / tool-create extract / ToolbarCommand short-forward.
