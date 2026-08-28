# D-C-PROJECTION — ResultProjection Impl Into .cpp Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-PERSIST `b15605fd`

## Purpose

Close path-correction ban on header-only non-template algorithms in ResultProjection:
move implementations from `DashboardResultProjection.h` into `.cpp`.

## Change

| Item | Detail |
|---|---|
| `DashboardResultProjection.h` | Declarations only (**228→45** phys) |
| `DashboardResultProjection.cpp` | All builders/display/extract implementations (**215** phys) |
| CMake | Link `.cpp` into ZenCrop + `test_dashboard_result_projection_contract` |

Public API names unchanged. GetCurrentResult* still Host-orchestrated (selection + file IO);
builders already pure and now properly TU-owned.

## Semantics

No intentional product behavior change. Hermetic **53/53**.

## KPI

| Metric | Before | After |
|---|---:|---:|
| `DashboardResultProjection.h` physical | 228 | **45** |
| `DashboardResultProjection.cpp` | 0 | **215** |
| hermetic | 53/53 | **53/53** |

## Verdict

**D-C-PROJECTION done** for ResultProjection TU ownership.  
D-C package still needs independent review against §12.3 full exit (Window still has ~30 History methods for UI/delete/result orchestration).

## D-C three-slice status

| Slice | Status |
|---|---|
| D-C-OWNER | **done** (`946e6b3c`) |
| D-C-PERSIST | **done** (`b15605fd`) |
| D-C-PROJECTION | **done** (this) |

## NEXT

1. Independent D-C package review (§12.3) — residual Host methods may remain as Host adapters if review accepts.
2. On D-C confirmed → **D-D State/Controller**.
