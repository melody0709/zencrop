# D-D-3 — History Multi-Select Sole Authority Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-D-2 `a97e46f8`

## Purpose

Close dual-write for history SourceRail multi-select:

- `DashboardState.selectedSourceKey` / `selectedSourceKeys` / `selectedSourceAnchor` sole authority
- Delete Window `m_sourceSelection` (`DashboardSelectionState`)

## Change

| Item | Detail |
|---|---|
| `OcrDashboardWindow.h` | Delete `m_sourceSelection` |
| `DashboardState.h` | Add `DashboardStateSelectedSourceAnchor` pure getter (thin accessor only) |
| `DashboardHistory.cpp` | Sync/Select/Delete clear write only State |
| `OcrDashboardWindow.SourceRail.inl` | Set/Toggle/Activate multi-select write only State |
| `tests/support/OcrDashboardWindow.Tests.inl` | Reads via `DashboardStateSelectedSourceKey` |

## Semantics

No intentional product behavior change. Production already dual-wrote State; Window field was write/read mirror.

## Ownership

| Before | After |
|---|---|
| `m_sourceSelection` + State dual-write | State sole authority |
| Window `DashboardSelectionState` field | **deleted** |

## Ban check

- Only thin getter added to State (no algorithm dump)
- Net-delete Window owner field
- Not helper-only

## KPI

| Metric | After |
|---|---:|
| hermetic | **54/54** |
| `m_sourceSelection` | **deleted** |

## Verdict

**D-D-3 done** (history multi-select sole authority).

Not package exit: D-D still needs more business collections off Window (batch queues → D-E; blocks → D-G).

## NEXT

1. Expand Controller selection Commands if net-delete Host method
2. Or assess D-D package exit readiness vs residual Host chrome
3. Then D-E package
