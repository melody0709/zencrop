# D-C-S4 — Pure Dismissal Build/Is; Delete Window Methods Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S3 `f1acbf5e` (ResultProjection pure builders)

## Purpose

Close research §12.3 residual: dismissal key build + IsDismissed lived as
`OcrDashboardWindow` methods even though pure Store helpers already existed for Build*.

Delete five Window methods; call sites use pure Store membership/legacy fallback.

## Change

| Item | Detail |
|---|---|
| `DashboardHistoryStore` | New: `IsDismissalKeyPresent`, `IsImageJobDismissed`, `IsPdfJobDismissed` (primary key + legacy path-wide base) |
| Deleted Window methods | `BuildBatchManifestDismissalKey`×3, `IsBatchManifestDismissed`×2 |
| Call sites | `DashboardHistory.cpp`, `OcrDashboardWindow.Batch.inl`, `OcrDashboardWindow.Tests.inl` → pure helpers |
| Kept Window | `DismissBatchManifestKeys` (state merge + repository save), Load/Save dismissed |
| Store contract | Extended hermetic coverage for IsDismissed primary + legacy |

### Semantics preserved

1. Primary dismissal key checked first; legacy `manifest:` base key still matches when primary differs.
2. Empty key → not dismissed.
3. Image job uses sourceInstanceId/createdAt/sourcePath identity; PDF uses createdAt/sourcePath.
4. History item path-wide key build still via `DashboardHistoryBuildHistoryItemDismissalKey`.

### Not claimed

- D-C package exit (~42 Window methods remain; GetCurrentResult* orchestration Window).
- Deletion of `DismissBatchManifestKeys` (persistence still Host).

## KPI

| Metric | Before S4 | After S4 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,516 | **2,459** |
| Window methods in History.cpp | ~47 | **~42** |
| `OcrDashboardWindow.h` physical | 926 | **907** |
| Messages / Route / State nonblank | 2416 / 145 / 1335 | unchanged |
| hermetic | 52/52 | **52/52** |

## Verdict

**D-C still PARTIAL.** Real Host method net-delete toward §12.3
("DashboardHistory.cpp 不再实现大量 OcrDashboardWindow::*").

## NEXT

1. D-C-S5: further Window method reduction (read facades / result orchestration / load-save).
2. Independent review only at D-C strict package exit.
