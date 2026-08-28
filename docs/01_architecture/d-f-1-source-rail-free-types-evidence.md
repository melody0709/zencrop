# D-F-1 — SourceRail Free Model Types Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-E confirmed `dae0916a`

## Purpose

Seed SourceRail free model types outside Window nested scope:

- `DashboardSourceRailTaskRow` / `TaskRowKind`
- `DashboardSourceRailViewRow`
- `DashboardSourceRailSortDirection`
- Window nested types deleted; Host uses type aliases for call-site stability

## Change

| Item | Detail |
|---|---|
| `DashboardSourceRailModel.h` | New free types (no HWND / paint / algorithms) |
| `OcrDashboardWindow.h` | Nested enums/structs → `using` aliases to free types |
| CMake + `test_dashboard_source_rail_model_contract` | hermetic type contract |

## Semantics

No intentional product behavior change. Type identity relocated; method bodies unchanged.

## Ownership

| Before | After |
|---|---|
| Nested `OcrDashboardWindow::SourceRail*` types | Free `DashboardSourceRail*` types |
| Window owns type definitions | Model header owns types |

## Residual (D-F later)

- `BuildSourceRailTaskRows` / `BuildSourceRailViewRows` still Window methods
- paint/hit-test still SourceRail.inl
- filter match helpers still static in StateAndHelpers.inl
- pure free builders + Renderer still future slices

## Ban check

- No algorithm dump into State/Model headers (types only)
- Net-delete nested Window type definitions
- Not helper-only (type ownership cutover)

## KPI

| Metric | After |
|---|---:|
| hermetic | **56/56** |

## Verdict

**D-F-1 done** (free type seed).

Not package exit.

## NEXT

1. D-F-2: pure free `BuildSourceRailTaskRows` (+ move filter helpers)
2. D-F-3: pure ViewRows builder / layout
3. Renderer/Input later
