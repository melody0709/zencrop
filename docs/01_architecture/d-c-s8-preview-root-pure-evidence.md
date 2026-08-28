# D-C-S8 — Pure Preview Truncate + Output-Root-In-Use Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S7 `ba508375` (selected history indices)

## Purpose

Move two residual pure decision bodies out of Window methods:

1. History edit preview truncation (line + char caps)  
2. Batch output-root path equality / in-use check

## Change

| Item | Detail |
|---|---|
| `DashboardHistoryBuildPreviewText` | Pure: normalize + line/char truncate; Host only measures edit metrics for visual clamp |
| `DashboardHistoryCacheSamePath` | Pure path equality after canonicalize/lower |
| `DashboardHistoryCacheOutputRootInUse` | Pure: match outputRoot against candidate roots |
| `BuildPreviewText` (Window) | Thin: metrics → pure truncate |
| `IsBatchOutputRootInUse` (Window) | Thin: collect roots from queues → pure match |
| `SameDashboardPath` static | Thin wrapper → Cache SamePath |
| Contracts | Model preview truncate; Cache SamePath/OutputRootInUse |

### Semantics preserved

1. Visual-width clamp still Host-owned (HWND font metrics).
2. Truncate trailing line breaks when truncated (WideTrimTrailingLineBreaks).
3. Empty outputRoot → not in use.
4. Path equality case/normalize same as legacy CanonicalizePath path.

### Not claimed

- Window method count drop (methods remain as thin Host facades).
- D-C package exit (GetCurrentResult* still Window).

## KPI

| Metric | Before S8 | After S8 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,404 | **2,358** |
| Window methods in History.cpp | ~35 | **~35** (thinned) |
| Messages / Route / State / Editor nonblank | 2416 / 145 / 1367 / 1467 | unchanged |
| hermetic | 52/52 | **52/52** |

## Verdict

**D-C still PARTIAL.** Pure decision bodies extracted; Host keeps HWND/queue collection.

## NEXT

1. D-C-S9: GetCurrentResult* orchestration / load-save / delete path thinning.
2. Independent direction review still recommended (S2–S8 now 7 code commits).
