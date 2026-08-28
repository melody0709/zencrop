# D-C-S7 — Pure Selected History Indices Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S6 `cc978899` (visible-pos + artifact options)

## Purpose

Close residual Window method that only resolved multi-select source keys
to history indices with single-selection fallback:

`OcrDashboardWindow::GetSelectedSourceHistoryIndices`

## Change

| Item | Detail |
|---|---|
| `DashboardHistorySelectedIndices` | Pure: keys → indices via `DashboardHistoryIndexFromSourceKey`; empty keys fall back to single index; sort+unique |
| Deleted Window method | `GetSelectedSourceHistoryIndices` |
| Call sites | History.cpp / SourceRail / Tests.inl → pure helper |
| Model contract | Multi-select / fallback / empty / oob coverage |

### Semantics preserved

1. Multi-select keys resolve via stable-key/sourceId identity.
2. Empty multi-select + valid single index → that one index.
3. Empty multi-select + invalid fallback → empty vector.
4. Result sorted unique.

### Not claimed

- D-C package exit (~35 Window methods remain; GetCurrentResult* still Window).
- SourceRail multi-select ownership (D-F).

## KPI

| Metric | Before S7 | After S7 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,417 | **2,404** |
| Window methods in History.cpp | ~36 | **~35** |
| Messages / Route / State / Editor nonblank | 2416 / 145 / 1367 / 1467 | unchanged |
| hermetic | 52/52 | **52/52** |

## Verdict

**D-C still PARTIAL.** One more Host method deleted.

## Direction review

Cumulative code commits after Stage1 reopen (S2–S7) = **6**.  
EXECUTION rule: direction review every 6 code commits or at D-C exit (whichever first).  
**Stop for independent D-C direction review before S8.**

## NEXT

1. Independent direction review of D-C S1–S7 against §12.3.
2. Then D-C-S8: GetCurrentResult* / load-save / delete path thinning.
