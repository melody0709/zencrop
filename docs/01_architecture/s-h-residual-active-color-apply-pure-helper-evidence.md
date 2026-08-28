# S-H residual — active color apply pure helper; dual 4-line body delete

Date: 2026-07-23  
Package: Stage 2 **S-H Host residual**  
Prior: resize fixed-point pure helper `376cab93`  
Code HEAD: this commit

## Intent

Delete dual Host 4-line bodies that copy active color fields onto annotations.

| Before | After |
|---|---|
| 6× Host `colorIndex/hasCustomColor/customColor/colorAlpha` assign | pure `ScreenshotAnnotationApplyActiveColor` |
| LButtonDown ~661 | **~655** |
| AnnotationEdit dual color-apply residual | **0** |

## Ownership domain

Active color → annotation field mapping is pure editor→annotation projection.  
Host only seeds ann and calls pure apply.

## Landed

| Item | Path |
|---|---|
| Pure helper | `ScreenshotActiveColor.h` — `ScreenshotAnnotationApplyActiveColor` |
| Dual body delete | AnnotationEdit (4) + AnnotationRender (2) + Settings (1) |

## KPI

| Metric | Before | After |
|---|---:|---:|
| HandleScreenshotLButtonDown lines | ~661 | **~655** |
| Host dual active-color apply bodies | 6 | **0** |
| hermetic | 68/68 | **68/68** |

## Ban check

- Same commit: land pure helper + delete dual Host bodies
- Not helper-only: Host create/preview/style paths shortened
- Stage2 Gate still **NOT PASS** (God residual)

## Residual

1. LButtonDown still ~655 (tool create / text / serial / gesture)
2. DrawScreenshotToolbar ~2448 / ToolbarInteraction ~1728 / OverlayWindow ~2474
3. Full S-A pixel golden ABSENT

## NEXT

Continue S-H residual: seed-original-geometry composite / tool-create extract / ToolbarCommand short-forward.
