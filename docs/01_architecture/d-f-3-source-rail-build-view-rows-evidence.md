# D-F-3 — Pure SourceRail BuildViewRows Base Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-F-2 `d1f9dc1f`

## Purpose

Move SourceRail view-row base building into pure free function:

- `DashboardSourceRailBuildViewRows` (no HWND / activity overlay / live timers)
- `DashboardSourceRailParseAddedDate` pure date parse
- Host `BuildSourceRailViewRows` = pure base + residual live activity overlay

## Change

| Item | Detail |
|---|---|
| `DashboardSourceRailModel.h/.cpp` | pure ViewRows builder + date parse |
| `OcrDashboardWindow.SourceRail.inl` | ViewRows thin: pure call + Host overlay residual |
| hermetic | ViewRows / date parse coverage |

## Semantics

No intentional product behavior change. Live activity overlays still Host-applied when cache present.

## Ownership

| Before | After |
|---|---|
| Window owns ViewRows algorithm (~270 LOC) | pure free builder |
| Host method body large | thin adapter + overlay residual |

## Residual (D-F later)

- Host activity overlay residual in BuildSourceRailViewRows
- paint / hit-test / input still SourceRail.inl
- Renderer / InputController not formed
- local ParseSourceRailAddedDate / SourceRailMetaWithElapsed may still exist as unused residual helpers

## Ban check

- Logic in `.cpp` (not header-only algorithms)
- Net thin Host method body
- No State algorithm dump

## KPI

| Metric | After |
|---|---:|
| hermetic | **56/56** |

## Verdict

**D-F-3 done.**

## NEXT

1. Assess D-F package exit vs §12.6 (paint/input residual may remain Host if Model pure)
2. Optional: delete unused local helpers; seed Renderer
3. Independent package review when residual Host-only
