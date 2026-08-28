# D-E-3 — Failed Jobs + PDF Render Queue Ownership Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-E-2 `97827273`

## Purpose

Move failed-job retry lists and PDF render queue into BatchCoordinator:

- `failedBatchJobs` / `failedPdfJobs` / `failedPdfPages`
- `pdfRenderTasks` / `pdfRenderPending` / `pdfRenderMaxConcurrent`
- Types: `DashboardPdfRetryPage`, `DashboardPdfRenderTracker`, `DashboardPendingPdfRender`

## Change

| Item | Detail |
|---|---|
| `DashboardBatchCoordinator.h` | Own failed + PDF render collections; move types from Window.h |
| `OcrDashboardWindow.h` | Delete Window fields + nested PendingPdfRender |
| Host call sites + tests | `m_batch.failed*` / `m_batch.pdfRender*` |

## Semantics

No intentional product behavior change. Collections relocated; `PendingPdfRender` renamed to `DashboardPendingPdfRender`.

## Ownership

| Before | After |
|---|---|
| Window failed job vectors | `m_batch.failed*` |
| Window PDF render queue/tasks | `m_batch.pdfRender*` |

## Residual (D-E later)

- external OCR maps / active work presentation (`m_externalOcr*`)
- `BatchOcrController m_batchController` (engine adapter — may stay Host or wrap)
- pause/cancel/generation lifecycle concentration
- typed completion events

## Ban check

- No State algorithm dump
- Net-delete Window owner fields
- Coordinator still no HWND/paint

## KPI

| Metric | After |
|---|---:|
| hermetic | **55/55** |
| Window failed/PDF-render dual fields | **0** |

## Verdict

**D-E-3 done.**

## NEXT

1. External OCR runtime maps / active work into Coordinator or State
2. Assess D-E package exit vs §12.5
3. Independent package review when residual Host-only
