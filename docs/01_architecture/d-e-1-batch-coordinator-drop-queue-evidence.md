# D-E-1 — BatchCoordinator Drop Queue + Generation Sole Authority Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-D confirmed `ff63df5f`

## Purpose

Seed BatchCoordinator ownership for OCR drop queue and close ocrGeneration dual-write:

- `DashboardBatchCoordinator` owns `dropQueue`
- `DashboardState.ocrGeneration` sole authority (delete Window `m_ocrGeneration`)
- Move `DashboardQueuedOcr` type next to coordinator

## Change

| Item | Detail |
|---|---|
| `DashboardBatchCoordinator.h` | New; owns `dropQueue`; push/pop/clear/erase_if |
| `OcrDashboardWindow.h` | `m_batch` coordinator; delete `m_dropQueue` / `m_ocrGeneration` / inline QueuedOcr |
| Host call sites | `m_batch.dropQueue.*`; generation writes only State + async atom |
| `test_dashboard_batch_coordinator_contract` | hermetic queue + generation |

## Semantics

No intentional product behavior change. Queue operations same; generation still advances via `g_dashboardGeneration` into State + async dispatch atom.

## Ownership

| Before | After |
|---|---|
| Window `std::deque m_dropQueue` | `m_batch.dropQueue` |
| Window `m_ocrGeneration` + State dual-write | State sole |
| QueuedOcr on Window.h | Coordinator header |

## Residual (D-E later)

- `m_batchTasks`, `m_activePdfJobs`, failed job vectors, PDF render pending/tasks
- external OCR maps / active work presentation
- generation global atom still in Window.cpp TU (OK for Host)

## Ban check

- No State algorithm dump
- Net-delete Window owner fields
- Header-only coordinator (queue ops only; no paint)

## KPI

| Metric | After |
|---|---:|
| hermetic | **55/55** |
| `m_dropQueue` / `m_ocrGeneration` Window fields | **0** |

## Verdict

**D-E-1 done** (drop queue ownership seed + generation sole).

Not package exit.

## NEXT

1. Move more batch collections into Coordinator (tasks / PDF jobs / failed)
2. Pause/cancel/generation lifecycle concentration
3. Toward D-E package exit
