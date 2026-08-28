# D-D-6 — hoveredActionBtn Sole Authority Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-D-5 `36726579`

## Purpose

Close dual-write for history action-button hover:

- `DashboardState.hoveredActionBtn` sole authority
- Delete Window `m_hoveredActionBtn`

## Change

| Item | Detail |
|---|---|
| `OcrDashboardWindow.h` | Delete `m_hoveredActionBtn` |
| `OcrDashboardWindow.Messages.inl` | WM_MOUSEMOVE / LEAVE write only State |
| `DashboardHistory.cpp` | Clear paths write only State |

## Semantics

No intentional product behavior change. Paint/cursor already read `DashboardStateHoveredActionBtn`.

## Ownership

| Before | After |
|---|---|
| Window int + State dual-write | State sole authority |
| `m_hoveredActionBtn` | **deleted** |

## Ban check

- No State algorithm growth
- Net-delete Window field
- Not helper-only

## KPI

| Metric | After |
|---|---:|
| hermetic | **54/54** |

## Verdict

**D-D-6 done.**

## NEXT

1. D-D package-exit evidence vs §12.4
2. Independent package review (no self-confirm)
3. Then D-E Batch
