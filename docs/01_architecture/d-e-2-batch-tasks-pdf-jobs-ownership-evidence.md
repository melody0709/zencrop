# D-E-2 — BatchCoordinator Owns batchTasks / activePdfJobs Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-E-1 `43e217f0`

## Purpose

Move live image-task and active PDF job collections into BatchCoordinator:

- `DashboardBatchCoordinator.batchTasks` / `activePdfJobs` sole ownership
- Delete Window `m_batchTasks` / `m_activePdfJobs`

## Change

| Item | Detail |
|---|---|
| `DashboardBatchCoordinator.h` | Add `batchTasks` + `activePdfJobs` vectors |
| `OcrDashboardWindow.h` | Delete Window fields; comments only |
| Host call sites + tests | `m_batch.batchTasks` / `m_batch.activePdfJobs` |
| hermetic coordinator contract | tasks/pdfs coverage |

## Semantics

No intentional product behavior change. Collections relocated; accessors rewritten mechanically.

## Ownership

| Before | After |
|---|---|
| Window `m_batchTasks` | `m_batch.batchTasks` |
| Window `m_activePdfJobs` | `m_batch.activePdfJobs` |

## Residual (D-E later)

- failed job vectors (`m_failedBatchJobs` / `m_failedPdfJobs` / `m_failedPdfPages`)
- PDF render pending/tasks
- external OCR maps / active work presentation
- pause/cancel lifecycle concentration

## Ban check

- No State algorithm dump
- Net-delete Window owner fields
- Coordinator still no HWND/paint

## KPI

| Metric | After |
|---|---:|
| hermetic | **55/55** |
| Window batchTasks / activePdfJobs | **0** |

## Verdict

**D-E-2 done.**

## NEXT

1. D-E-3 failed jobs + PDF render queue into Coordinator
2. Lifecycle concentration
3. Toward D-E package exit
