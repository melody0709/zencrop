# D-D-2 — Image/PDF Selection Sole Authority Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-D-1 `a7c43809`

## Purpose

Close dual-write for SourceRail image-task / PDF selection:

- `DashboardState.imageTaskSelection` / `pdfSelection` sole authority
- Delete Window fields that only mirrored State

## Change

| Item | Detail |
|---|---|
| `OcrDashboardWindow.h` | Delete 11 dual-write fields (`m_hasImageTaskSelection`, `m_selectedImageTask*`, `m_hasPdfSelection`, `m_selectedPdf*`) |
| `OcrDashboardWindow.SourceRail.inl` | Clear/Activate write only State |
| `OcrDashboardWindow.Batch.inl` | Rerun identity refresh writes only State |
| `tests/support/OcrDashboardWindow.Tests.inl` | Reads via DashboardState* helpers |

## Semantics

No intentional product behavior change. Production reads already used `DashboardStateHas*` / field getters; dual-write was write-only mirror.

## Ownership

| Before | After |
|---|---|
| Window fields + State dual-write | State sole authority |
| 11 selection fields on Window | **deleted** |

## Ban check

- **No** new algorithm in `DashboardState.h`
- Net-delete Window owner fields
- Not helper-only

## KPI

| Metric | After |
|---|---:|
| hermetic | **54/54** |
| Window selection dual-write fields | **0** |

## Verdict

**D-D-2 done** (selection sole authority).

Not package exit: D-D still needs more business collections off Window (batch queues → D-E; blocks → D-G; remaining Host chrome).

## NEXT

1. More D-D residual: pending filter text / sourceSelection / actionButtons if pure
2. Or expand Controller selection Commands
3. Then D-E package
