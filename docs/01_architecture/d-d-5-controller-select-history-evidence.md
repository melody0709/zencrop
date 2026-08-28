# D-D-5 — Controller SelectHistoryIndex Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-D-4 `2213d4e9`

## Purpose

Expand pure Controller for history index selection:

- `SelectHistoryIndex` / `ClearHistorySelection` Command kinds handled
- `SetSelectedHistoryIndex` Host becomes thin Controller adapter
- Selection mutation testable without HWND

## Change

| Item | Detail |
|---|---|
| `DashboardController.h/.cpp` | `ApplySelectHistoryIndex` / `ClearHistorySelection`; Dispatch non-const model |
| `DashboardHistory.cpp` | `SetSelectedHistoryIndex` → Controller only |
| `test_dashboard_controller_contract` | select / clear / oob coverage |

## Semantics

No intentional product behavior change:

- Index OOB still clears model + State key
- Stable key still write authority via `DashboardStateSelectHistoryBySourceKey`
- `SelectHistoryItem` Host still owns UI (canvas/preview/list)

## Ownership

| Before | After |
|---|---|
| Window inline model+State select | Controller pure select |
| Dispatch SelectHistoryIndex unhandled | **handled** |

## Ban check

- No State algorithm growth
- Controller still no HWND/HDC/HBITMAP
- Net thin Host method body (ownership of transition)

## KPI

| Metric | After |
|---|---:|
| hermetic | **54/54** |
| Controller selection Commands | **handled** |

## Verdict

**D-D-5 done.**

Not package exit: residual Host collections (batch queues → D-E; blocks → D-G; GDI/HWND chrome OK on Host).

## NEXT

1. Assess D-D package exit readiness vs §12.4
2. Write package-exit evidence if residual only Host chrome / other-package
3. Independent package review (cannot self-confirm)
4. Then D-E
