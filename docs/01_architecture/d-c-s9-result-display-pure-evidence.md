# D-C-S9 — History-Item Display + Content-JSON Markdown Pure Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S8 `0c8c4f8b` (preview truncate + output-root-in-use)

## Purpose

Move residual pure decision bodies from `GetCurrentResultText` /
`GetCurrentPreviewSourceMarkdown` into ResultProjection:

1. History-item Text/Json/Source/Preview display (+ DurableOutputLink missing msg)  
2. Content-JSON `"markdown"` field extract/unescape

## Change

| Item | Detail |
|---|---|
| `DashboardResultProjectionHistoryItemDisplayText` | Pure mode switch for history item; Host supplies i18n missing-output string |
| `DashboardResultProjectionTryExtractMarkdownFromContentJson` | Pure WideExtractJsonField + WideUnescapeJsonString |
| `GetCurrentResultText` history branch | Thin: call pure display helper |
| `GetCurrentPreviewSourceMarkdown` | Thin: pure markdown extract after file read |
| Contract | Mode display + extract hit/miss + durable missing |

### Semantics preserved

1. DurableOutputLink + empty text → Host-provided missing message (zh/en).
2. Text mode strips markdown; Source/Preview keep normalized markdown body.
3. Json mode still builds history item JSON via existing pure builder.
4. Content JSON without `"markdown"` key → extract returns false (fall through).

### Not claimed

- Image-task / PDF-job GetCurrentResult* orchestration still Window (selection + file IO).
- Window method count drop (methods remain; bodies thinned).
- D-C package exit.

## KPI

| Metric | Before S9 | After S9 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,358 | **2,354** |
| Window methods in History.cpp | ~35 | **~35** |
| `DashboardResultProjection.h` nonblank | 172 | **211** |
| Messages / Route / State / Editor nonblank | 2416 / 145 / 1367 / 1467 | unchanged |
| hermetic | 52/52 | **52/52** |

## Verdict

**D-C still PARTIAL.** History result display pure; image/PDF selection+IO orchestration residual.

## NEXT (paused — user request)

1. Resume: D-C-S10 image/PDF GetCurrentResult* pure mode helpers with file-read callbacks, or load-save/delete thinning.
2. Independent D-C direction review still recommended (S2–S9 = 8 code commits).
