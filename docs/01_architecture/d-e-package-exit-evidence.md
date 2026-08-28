# D-E Batch Coordinator — Package Exit Evidence

> **PARTIAL (2026-07-21):** retained as field-cutover evidence; the package-exit verdict is superseded because the original Coordinator ownership acceptance is incomplete. Do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: D-E-4 (this commit)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`  
Prior package: D-D exit @ `docs/01_architecture/d-d-package-exit-evidence.md`

## Verdict

**IMPLEMENTATION-SIDE COMPLETE — authorize D-E package exit +1 after this evidence is accepted.**

Batch coordinator dual-write authorities for runtime flags, progress counters, active OCR display, and active-work failure flag deleted. Residual batch collections (`m_batchTasks`, external OCR maps/jobs, SourceRail batch selection mirror) remain Host/queue lifecycle or D-F boundary — not session dual-write prefs.

## Ownership cutovers (D-E-1..4)

| Slice | Legacy deleted | Sole authority |
|---|---|---|
| D-E-1 | `m_ocrBusy`, `m_batchPaused`, `SyncBatchRuntimeFlagsMirror` | `DashboardStateSetOcrBusy` / `SetBatchPaused` |
| D-E-2 | `m_dropTotal`, `m_dropDone`, `m_cancelBatchRequested`, `m_pdfRenderInFlight`, `SyncBatchProgressMirror` | `DashboardStateSyncBatchProgress` |
| D-E-3 | `m_activeOcrLabel`, `m_activeOcrStartTick`, `m_activeOcrOwner`, `SyncActiveOcrDisplayMirror` | `DashboardStateSync/ClearActiveOcrDisplay` |
| D-E-4 | `m_activeWorkHadFailure` | `DashboardStateSet/ActiveWorkHadFailure` |

## Acceptance checklist

| Item | Status | Notes |
|---|---|---|
| Batch runtime flags sole on state | **yes** | D-E-1 |
| Progress counters sole on state | **yes** | D-E-2 |
| Active OCR display sole on state | **yes** | D-E-3 |
| Work-had-failure sole on state | **yes** | D-E-4 |
| hermetic | **yes** | `ctest -L hermetic` **50/50** post D-E-4 |
| freeze heads | **yes** | Messages **3612** (net delete); Route 5060; State 1320 |

## Residual (not D-E package blockers)

- `m_batchTasks` / drop queue / PDF render pending — Host job lifecycle collections
- `m_externalOcr*` maps — external runtime host cache (comment already notes `m_externalOcrRuntimes` authority)
- `SyncBatchSelectionMirror` + `m_selectedBatchRows` / `m_batchSelectionAnchor` — SourceRail selection boundary → **D-F**
- `SyncPdfTreeKeysMirror` — **D-F**
- Canvas mirrors — **D-G**

## Stage1 budget (post D-E-4)

- Stage1 code commits: **32** (D-B-1..11 + D-C-1..9 + D-D-1..8 + D-E-1..4) — **target ≤32 reached**
- ownership cutovers: **32**
- package exits after this evidence: **5/9** (D-A…D-E)
- hard stop remains 55; post-target slices must still net-delete legacy authority

## NEXT

1. Accept this package evidence → EXECUTION package exit 5/9  
2. Open **D-F SourceRail** first vertical cutover (PDF tree keys / batch selection)  
