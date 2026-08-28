# D-F SourceRail — Package Exit Evidence

> **PARTIAL (2026-07-21):** retained as field-cutover evidence; the package-exit verdict is superseded because the original SourceRail component boundaries are incomplete. Do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: D-F-4 (this commit)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`  
Prior package: D-E exit @ `docs/01_architecture/d-e-package-exit-evidence.md`

## Verdict

**IMPLEMENTATION-SIDE COMPLETE — authorize D-F package exit +1 after this evidence is accepted.**

SourceRail dual-write authorities for PDF tree expand/pause keys and batch multi-selection deleted. Residual Host job collections (`m_batchTasks`, `m_activePdfJobs`) and HWND SourceRail layout (`m_sourceList`, scroll, hover) remain intentional Host lifecycle — not session dual-write prefs.

## Ownership cutovers (D-F-1..4)

| Slice | Legacy deleted | Sole authority |
|---|---|---|
| D-F-1 | Window owning PDF tree key vectors dual-copy | `DashboardState` PDF keys (Window aliases) |
| D-F-2 | Window owning batch selection dual-copy | `DashboardState` selectedBatchRows/anchor (aliases) |
| D-F-3 | PDF tree key Window aliases + `SyncPdfTreeKeysMirror` | direct `m_dashboardState.*Pdf*Keys` |
| D-F-4 | batch selection Window aliases + `SyncBatchSelectionMirror` | direct `m_dashboardState.selectedBatchRows` / `batchSelectionAnchor` |

## Acceptance checklist

| Item | Status | Notes |
|---|---|---|
| PDF expand/pause keys sole on state | **yes** | D-F-1/3 |
| Batch SourceRail multi-select sole on state | **yes** | D-F-2/4 |
| hermetic | **yes** | `ctest -L hermetic` **50/50** post D-F-4 |
| freeze heads | **yes** | Messages **3612** (net delete from 3660); Route 5060; State 1320 |

## Residual (not D-F package blockers)

- `m_batchTasks` / `m_activePdfJobs` / drop queue — Host job lifecycle collections
- `m_sourceSelection` / `m_sourceScrollY` / HWND SourceRail — Host layout/input
- Canvas mirrors — **D-G**
- Preview coordinator — **D-H**
- MessageRoute dead predicates — **D-I**

## Stage1 budget (post D-F-4)

- Stage1 code commits: **36** (past target ≤32; hard stop 55)
- ownership cutovers: **36**
- package exits after this evidence: **6/9** (D-A…D-F)

## NEXT

1. Accept this package evidence → EXECUTION package exit 6/9  
2. Open **D-G Canvas/Blocks** first vertical cutover (canvas view / hover)  
