# S-H residual — brokenLine / highLight / magnifier style pure helpers; dual body delete

Date: 2026-07-23  
Package: Stage 2 **S-H Host residual**  
Prior: penWidth+watermark pure helpers `568fda9e`  
Code HEAD: this commit

## Intent

Delete dual Host style-assign bodies for broken-line, highlight, and magnifier tools.

| Before | After |
|---|---|
| 4× broken-line mode field assign | pure `ScreenshotAnnotationApplyBrokenLineStyle` |
| 3× highlight style field assign | pure `ScreenshotAnnotationApplyHighLightStyle` |
| 3× magnifier style field assign | pure `ScreenshotAnnotationApplyMagnifierStyle` |
| AnnotationEdit | **~1927 → ~1913** |

## Ownership domain

Tool-specific style → annotation field mapping is pure editor→annotation projection.  
Host only seeds ann geometry/points and calls pure apply.

## Landed

| Item | Path |
|---|---|
| Pure helpers | `ScreenshotActiveColor.h` — ApplyBrokenLineStyle / ApplyHighLightStyle / ApplyMagnifierStyle |
| Dual body delete | AnnotationEdit (create×2) + AnnotationRender (preview×3) + Settings (ApplyActive×3) |

## KPI

| Metric | Before | After |
|---|---:|---:|
| AnnotationEdit.cpp lines | ~1927 | **~1913** |
| AnnotationRender.cpp lines | ~1033 | **~1024** |
| Settings.cpp lines | ~1061 | **~1053** |
| Host dual brokenLine/highLight/magnifier style | multi | **0** (create/preview/ApplyActive) |
| hermetic | 68/68 | **68/68** |

## Ban check

- Same commit: land pure helpers + delete dual Host bodies
- Not helper-only: Host create/preview/style paths shortened
- Stage2 Gate still **NOT PASS** (God residual)

## Residual

1. LButtonDown still ~642 (tool create / text / serial / gesture)
2. DrawScreenshotToolbar ~2448 / ToolbarInteraction ~1728 / OverlayWindow ~2474
3. Full S-A pixel golden ABSENT

## NEXT

Continue S-H residual: seed-original-geometry composite / tool-create extract / ToolbarCommand short-forward.
