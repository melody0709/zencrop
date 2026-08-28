# D-D-4 — updatingSourceList Sole Authority Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-D-3 `575f2a88`

## Purpose

Close dual-write for SourceRail list-update reentrancy flag:

- `DashboardState.updatingSourceList` sole authority
- Delete Window `m_updatingSourceList`

## Change

| Item | Detail |
|---|---|
| `OcrDashboardWindow.h` | Delete `m_updatingSourceList` |
| `DashboardHistory.cpp` | RebuildSourceList / SyncSourceListSelectionToActive write only State |

## Semantics

No intentional product behavior change. Reads already used `DashboardStateIsUpdatingSourceList`; Window field was write-only mirror.

## Ownership

| Before | After |
|---|---|
| Window bool + State dual-write | State sole authority |
| `m_updatingSourceList` | **deleted** |

## Ban check

- No State algorithm growth
- Net-delete Window field
- Not helper-only

## KPI

| Metric | After |
|---|---:|
| hermetic | **54/54** |

## Verdict

**D-D-4 done.**

## NEXT

1. Expand Controller selection Commands (SelectHistoryIndex was unhandled stub)
2. Assess D-D package exit vs residual Host chrome / other-package collections
3. Then D-E
