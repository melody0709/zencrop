# D-H-1 — Preview Protocol Pure Decisions Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-G confirmed `8fafc75c`

## Purpose

Seed pure Preview protocol decision surface (no HWND / WebView2):

- hover/select/edit/save/restore eligibility gates
- reject tokens for stale/restore/persist/rollback
- Host callbacks thin-wrap pure decisions
- `DashboardPreviewCoordinator` session shell on Window

## Change

| Item | Detail |
|---|---|
| `DashboardPreviewCoordinator.h` | pure decide helpers + reject tokens + session shell |
| `OcrDashboardWindow.h` | `m_preview` coordinator; host still owns `m_previewHost` |
| `OcrDashboardWindow.ImagePreview.inl` | preview callbacks use pure gates |
| `test_dashboard_preview_coordinator_contract` | hermetic protocol + security seed |

## Semantics

No intentional product behavior change. Same stale_target / restore_unavailable / rollback_failed / persist_failed tokens.

## Ownership

| Before | After |
|---|---|
| Host inline mode/id/existence gates | pure free decide helpers |
| fail token string dual-inline | pure token helpers |
| no Preview coordinator type | `m_preview` session shell |

## Residual (D-H later)

- `unique_ptr<OcrMarkdownPreviewHost>` Host lifecycle (HWND/WebView2)
- Web message JSON still parsed in OcrMarkdownPreviewHost (not Window — §12.8 partially met)
- EnsurePreviewHost / FallbackPreview Host adapters
- full typed protocolVersion/renderToken session on Coordinator still thin

## Ban check

- Pure decisions header-only (small policy; matches PreviewSecurity seed pattern)
- Net ownership of protocol gates off Host inline
- Not helper-only (callbacks rewired)

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |

## Verdict

**D-H-1 done.**

## NEXT

1. Assess D-H package exit vs §12.8
2. Independent package review when residual Host-only
3. Then D-I Host/TU
