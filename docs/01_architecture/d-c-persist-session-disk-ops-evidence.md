# D-C-PERSIST — History Session Disk Ops Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-OWNER `946e6b3c`

## Purpose

Move History disk persistence out of Window methods into session free functions;
**delete** Window methods that only laundered session+state.

## Change

| Free function | Replaces Window method |
|---|---|
| `DashboardHistorySessionLoadDismissed` | `LoadDismissedBatchManifests` |
| `DashboardHistorySessionSaveDismissed` | `SaveDismissedBatchManifests` |
| `DashboardHistorySessionDismissKeys` | `DismissBatchManifestKeys` |
| `DashboardHistorySessionSaveItems` | `SaveHistory` (disk part) |
| `DashboardHistorySessionLoadItems` | `LoadHistory` (disk part) |
| `DashboardHistorySessionSyncSelection` | `SyncHistoryModelMirror` |
| `DashboardHistorySessionSaveItemToDefaultFile` | body of static `SaveToHistoryFile` |

### Deleted Window methods (decl + def)

- `LoadDismissedBatchManifests`
- `SaveDismissedBatchManifests`
- `DismissBatchManifestKeys`
- `SaveHistory`
- `SyncHistoryModelMirror`

### Retained thin Host methods

- `LoadHistory` — UI clear (ranges/buttons) + session load/sync + `ApplyFilter`
- `SaveToHistoryFile` — one-line forward to `DashboardHistorySessionSaveItemToDefaultFile`

## Semantics

- Dismiss merge + rollback on save fail preserved.
- SaveItems respects history persistence suspended flag.
- SyncSelection re-resolves stable source key (D-C-S1).
- Blocks edit paths sync selection on save success/fail (parity with old SaveHistory).

## KPI

| Metric | Before | After |
|---|---:|---:|
| History.cpp Window methods | ~30 (post-OWNER list) / ~35 pre-PERSIST board | **30** listed (`rg OcrDashboardWindow::`) |
| Deleted methods this slice | — | **5** |
| `DashboardHistory.cpp` physical | 2354 (pre-OWNER board) | **2225** |
| hermetic | 53/53 | **53/53** |

## Verdict

**D-C-PERSIST done** for load/save/dismiss disk ownership.  
D-C still **PARTIAL** until PROJECTION + delete-path thinning + package review.

## NEXT

1. **D-C-PROJECTION**: move non-template ResultProjection into `.cpp`; thin GetCurrentResult*.
2. Optional: further delete UI orchestration from History.cpp (Delete/Clear still Window).
3. Independent D-C package review before D-D.
