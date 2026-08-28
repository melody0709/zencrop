# D-C-S6 — Pure Visible Position + Artifact Options Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S5 `5df3f52f` (thin history read facades deleted)

## Purpose

Close residual thin Window methods that only laundered pure state:

1. `GetSourceListPositionForHistoryIndex` — find history index in visible list  
2. `OutputArtifactDefaultsForRead` — map `DashboardState` artifact defaults → product options

## Change

| Item | Detail |
|---|---|
| `DashboardStateVisibleHistoryPosition` | Pure: position of historyIndex in `visibleHistoryIndices` (-1 if hidden) |
| `DashboardStateOcrOutputArtifactOptions` | Pure: state → `OcrOutputArtifactOptions` + `NormalizeOcrOutputArtifactOptions` |
| Deleted Window methods | `GetSourceListPositionForHistoryIndex`, `OutputArtifactDefaultsForRead` |
| Call sites | History.cpp / SourceRail / Batch / Import / StateAndHelpers → pure helpers |
| State contract | Visible position + product artifact options coverage |

### Semantics preserved

1. Missing visible index → -1.
2. Artifact field mapping + normalize identical to former Window method.
3. No HWND / repository in pure path.

### Not claimed

- D-C package exit (~36 Window methods remain; GetCurrentResult* still Window).
- DashboardState size reduction (D-D owns split; this growth is ownership API).

## KPI

| Metric | Before S6 | After S6 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,440 | **2,417** |
| Window methods in History.cpp | ~38 | **~36** |
| `OcrDashboardWindow.h` physical | 903 | **899** |
| `DashboardState.h` nonblank | 1,335 | **1,367** (ownership API) |
| Messages / Route / Editor nonblank | 2416 / 145 / 1467 | unchanged |
| hermetic | 52/52 | **52/52** |

## Verdict

**D-C still PARTIAL.** Two more Host methods deleted; pure state boundary expanded.

## NEXT

1. D-C-S7: further Window method reduction (GetCurrentResult* / load-save / delete).
2. Independent review only at D-C strict package exit.
