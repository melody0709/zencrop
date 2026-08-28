# D-C-OWNER — History Session Ownership Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-B confirmed `d-b-package-review-confirmed-2026-07-21.md`; product `d29dc638`

## Purpose

Close D-C ownership residual: Window declared **two** History owners
(`m_historyRepository` + `m_historyModel`). Introduce single session type.

## Change

| Item | Detail |
|---|---|
| `DashboardHistorySession.h` | Owns `DashboardHistoryRepository` + `DashboardHistoryModel`; `ForExeDirectory()` |
| `OcrDashboardWindow` | **Deletes** separate repo/model fields; holds `DashboardHistorySession m_history` only |
| Call sites | `m_historyModel` → `m_history.model`; `m_historyRepository` → `m_history.repository` (History.cpp, .inl, Tests.inl) |
| Includes | Window includes Session (pulls Model+Repository) |

## Deleted owner surface

- `DashboardHistoryRepository m_historyRepository{...}` field
- `DashboardHistoryModel m_historyModel` field

Window no longer has two independent History aggregate fields.

## Semantics

No intentional behavior change. Load/Save/Delete methods still Window methods
(next slice **D-C-PERSIST** moves those). Session is value-owned by Window (Host
holds facade; domain types co-located).

## KPI

| Metric | Before | After |
|---|---:|---:|
| Window History fields | 2 (repo+model) | **1 (session)** |
| hermetic | 53/53 | **53/53** |
| freeze Messages/Route | 2416 / 145 | unchanged |

## Verdict

**D-C-OWNER cutover done** for repository/model aggregation.  
D-C package still **PARTIAL** until PERSIST + PROJECTION result slices + package review.

## NEXT

1. **D-C-PERSIST**: move Load/Save/Dismiss/DeleteHistory* into session (or free ops on session); **delete** Window methods same commit.
2. **D-C-PROJECTION**: non-template ResultProjection into `.cpp`; thin GetCurrentResult*.
3. Independent D-C package review before D-D.
