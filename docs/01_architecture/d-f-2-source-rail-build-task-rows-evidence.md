# D-F-2 — Pure SourceRail BuildTaskRows + Filter Helpers Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-F-1 `b242583d`

## Purpose

Move SourceRail task-row building and filter matchers into pure free functions:

- `DashboardSourceRailBuildTaskRows` (no HWND)
- filter matchers + PDF status summarize + status labels
- Host `BuildSourceRailTaskRows` becomes thin adapter

## Change

| Item | Detail |
|---|---|
| `DashboardSourceRailModel.h/.cpp` | pure builders + filter helpers |
| `OcrDashboardWindow.SourceRail.inl` | `BuildSourceRailTaskRows` thin wrapper |
| `OcrDashboardWindow.StateAndHelpers.inl` | static filter/status helpers thin-wrap free funcs |
| hermetic | BuildTaskRows / filter / summarize coverage |

## Semantics

No intentional product behavior change. Status-label search tokens still zh/en via `S::IsChinese()` at Host edge.

## Ownership

| Before | After |
|---|---|
| Window method owns TaskRows algorithm | pure free builder |
| static filter helpers in Host TU | free helpers in Model TU |
| Host method body ~100 LOC | thin adapter |

## Residual (D-F later)

- `BuildSourceRailViewRows` still Window (large display builder)
- paint/hit-test still SourceRail.inl
- Renderer / InputController not formed

## Ban check

- No algorithm dump into State
- Logic in `.cpp` (not header-only algorithms)
- Net-delete Window method body (thin Host)

## KPI

| Metric | After |
|---|---:|
| hermetic | **56/56** |

## Verdict

**D-F-2 done.**

## NEXT

1. D-F-3: pure ViewRows builder (or substantial thin)
2. Layout/Renderer seed
3. Toward D-F package exit
