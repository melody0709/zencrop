# D-E-4 — External OCR Runtime Ownership Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-E-3 `4ec46722`

## Purpose

Move external OCR runtime maps and cloud fallback keys into BatchCoordinator:

- `externalOcrBusy` / `externalOcrStartTick` / `externalOcrLabel` / `externalOcrCurrentId`
- `externalOcrJobs` / `externalOcrRuntimes`
- `fallbackAttemptedKeys`
- Type: `DashboardExternalOcrRuntime`

## Change

| Item | Detail |
|---|---|
| `DashboardBatchCoordinator.h` | Own external OCR maps + fallback keys; move type from Window.h |
| `OcrDashboardWindow.h` | Delete Window fields; keep Host timer chrome (`m_activeWorkSummary*`, `m_closeAfterCancel`, `m_activeWorkTimerRunning`) |
| Host call sites + tests | `m_batch.externalOcr*` / `m_batch.fallbackAttemptedKeys` |

## Semantics

No intentional product behavior change. Collections relocated; Host retains timer-driven presentation strip only.

## Ownership

| Before | After |
|---|---|
| Window external OCR maps | `m_batch.externalOcr*` |
| Window fallback keys | `m_batch.fallbackAttemptedKeys` |

## Residual (D-E later / Host)

- `m_activeWorkSummary` / `m_activeWorkSummaryUntilTick` / `m_activeWorkTimerRunning` — Host UI chrome
- `m_closeAfterCancel` — Host close lifecycle flag
- `BatchOcrController m_batchController` — engine adapter (may wrap later)
- typed completion events / generation lifecycle concentration still partial

## Ban check

- No State algorithm dump
- Net-delete Window owner fields
- Coordinator still no HWND/paint

## KPI

| Metric | After |
|---|---:|
| hermetic | **55/55** |
| Window external OCR dual fields | **0** |

## Verdict

**D-E-4 done.**

## NEXT

1. Assess D-E package exit vs §12.5
2. Independent package review when residual Host-only
3. Then D-F SourceRail
