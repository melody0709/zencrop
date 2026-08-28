# S-H residual — penWidth-by-tool + watermark-style pure helpers; dual body delete

Date: 2026-07-23  
Package: Stage 2 **S-H Host residual**  
Prior: text-style apply pure helper `5a9ac38e`  
Code HEAD: this commit

## Intent

Delete dual Host penWidth ternary / per-tool assign bodies and dual watermark-style assign bodies.

| Before | After |
|---|---|
| 2× ~8-line penWidth ternary (create + preview) | pure `ScreenshotEditorPenWidthForTool` |
| Settings per-tool penWidth field reads | sole via pure map |
| 2× ~12-line watermark style assign | pure `ScreenshotAnnotationApplyWatermarkStyle` |
| AnnotationEdit | **~1945 → ~1927** |

## Ownership domain

Tool → pen width map and watermark style → annotation field mapping are pure editor→annotation projection.

## Landed

| Item | Path |
|---|---|
| Pure helpers | `ScreenshotActiveColor.h` — `ScreenshotEditorPenWidthForTool` + `ScreenshotAnnotationApplyWatermarkStyle` |
| Dual body delete | AnnotationEdit (create/watermark/serial/brokenLine) + AnnotationRender (preview) + Settings (ApplyActive) |

## KPI

| Metric | Before | After |
|---|---:|---:|
| AnnotationEdit.cpp lines | ~1945 | **~1927** |
| AnnotationRender.cpp lines | ~1038 | **~1033** |
| Settings.cpp lines | ~1068 | **~1061** |
| Host dual penWidth ternary / per-tool assign | multi | **0** (create/preview/ApplyActive) |
| Host dual watermark-style assign | 2 | **0** |
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
