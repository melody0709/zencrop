# S-H residual — text-style apply pure helper; dual body delete

Date: 2026-07-23  
Package: Stage 2 **S-H Host residual**  
Prior: active-color apply pure helper `5221c190`  
Code HEAD: this commit

## Intent

Delete dual Host ~13-line bodies that copy text style fields onto annotations.

| Before | After |
|---|---|
| 4× Host textBold…textFontSizeF assign | pure `ScreenshotAnnotationApplyTextStyle` |
| LButtonDown ~655 | **~642** |
| AnnotationEdit dual text-style residual | **0** |

## Ownership domain

Text style → annotation field mapping is pure editor→annotation projection.  
Host only seeds ann and calls pure apply.

## Landed

| Item | Path |
|---|---|
| Pure helper | `ScreenshotActiveColor.h` — `ScreenshotAnnotationApplyTextStyle` (style + state overloads) |
| Dual body delete | AnnotationEdit (2) + AnnotationRender (1) + Settings (1) |

## KPI

| Metric | Before | After |
|---|---:|---:|
| HandleScreenshotLButtonDown lines | ~655 | **~642** |
| AnnotationEdit.cpp lines | ~1972 | **~1945** |
| Host dual text-style apply bodies | 4 | **0** |
| hermetic | 68/68 | **68/68** |

## Ban check

- Same commit: land pure helper + delete dual Host bodies
- Not helper-only: Host create/preview/style paths shortened
- Stage2 Gate still **NOT PASS** (God residual)

## Residual

1. LButtonDown still ~642 (tool create / text / serial / gesture)
2. DrawScreenshotToolbar ~2448 / ToolbarInteraction ~1728 / OverlayWindow ~2474
3. Full S-A pixel golden ABSENT

## NEXT

Continue S-H residual: seed-original-geometry composite / tool-create extract / ToolbarCommand short-forward.
