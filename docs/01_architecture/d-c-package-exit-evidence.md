# D-C History — Package Exit Evidence

> **PARTIAL (2026-07-21):** retained as field-cutover evidence; the package-exit verdict is superseded because the original component/ownership acceptance is incomplete. Do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: D-C-9 (this commit)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`  
Prior package: D-B exit @ `docs/01_architecture/d-b-package-exit-evidence.md`

## Verdict

**IMPLEMENTATION-SIDE COMPLETE — authorize D-C package exit +1 after this evidence is accepted.**

History session dual-write authorities deleted. Residual Host paint caches and a selection-clamp helper remain intentional Host boundary, not dual-write session prefs/items.

## Ownership cutovers (D-C-1..9)

| Slice | Legacy deleted | Sole authority |
|---|---|---|
| D-C-1 | `m_filterText` | `DashboardState.filterText` |
| D-C-2 | `m_historyPersistenceSuspended`, `m_dismissedManifestPersistenceSuspended` | `DashboardStateApplyPersistenceFlags` |
| D-C-3 | `m_selectedHistoryIndex` | `DashboardState.selectedHistoryIndex` |
| D-C-4 | `m_expandedHistoryIndex` | `DashboardState.expandedHistoryIndex` |
| D-C-5 | `m_dismissedBatchManifestKeys`, `SyncDismissedBatchManifestKeysMirror` | `DashboardState` dismissed keys APIs |
| D-C-6 | model→state persistence dual-write; Mirrors sel/susp params | size-only items check (then obsolete) |
| D-C-7 | Window owning items vector dual-copy Replace | `m_historyModel.items` sole store (alias) |
| D-C-8 | `m_visibleHistoryIndices` | `DashboardState.visibleHistoryIndices` |
| D-C-9 | `m_historyItems` alias field; dual-read ItemAt fallback | direct `m_historyModel.items` |

## Acceptance checklist

| Item | Status | Notes |
|---|---|---|
| History filter sole on state | **yes** | D-C-1 |
| Persistence flags sole on state | **yes** | D-C-2 |
| Selection / expand sole on state | **yes** | D-C-3/4 |
| Dismissed keys sole on state | **yes** | D-C-5 |
| Visible indices sole on state | **yes** | D-C-8 |
| History items sole store | **yes** | `m_historyModel.items` (D-C-7/9) |
| hermetic | **yes** | `ctest -L hermetic` **50/50** post D-C-9 |
| freeze heads | **yes** | Messages 3660 / Route 5060 / State 1320 nonblank |

## Residual (not D-C package blockers)

- `SyncHistoryModelMirror` — selection clamp helper only (no items dual-copy)
- `m_historyRanges` / `m_actionButtons` / `m_previewInfos` — Host paint/layout caches (HWND-tied), not session dual-write mirrors
- Other Stage1 dual-write domains (Batch/Canvas/SourceRail/PDF tree) — **D-D…D-G**

## Stage1 budget (post D-C-9)

- Stage1 code commits: **20** (D-B-1..11 + D-C-1..9) of target ≤32 / hard stop 55
- ownership cutovers: **20**
- package exits after this evidence: **3/9** (D-A + D-B + D-C)

## NEXT

1. Accept this package evidence → EXECUTION package exit 3/9  
2. Open **D-D State/Controller** first vertical cutover  
