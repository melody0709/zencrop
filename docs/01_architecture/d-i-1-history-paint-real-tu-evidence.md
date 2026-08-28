# D-I-1 — HistoryPaint Real TU + Shared Theme Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-H confirmed `85ff1211`

## Purpose

Start production class-method `.inl` → real `.cpp` conversion:

- Convert `OcrDashboardWindow.HistoryPaint.inl` → `.cpp` TU
- Extract shared `DashboardTheme.h` for multi-TU color constants
- Remove HistoryPaint from OcrDashboardWindow.cpp `#include` chain

## Change

| Item | Detail |
|---|---|
| `OcrDashboardWindow.HistoryPaint.cpp` | real TU (was `.inl`) |
| `DashboardTheme.h` | shared Theme colors (was local namespace in Window.cpp) |
| `OcrDashboardWindow.cpp` | drop HistoryPaint `#include`; include Theme header |
| CMake | list HistoryPaint.cpp; drop HistoryPaint.inl |
| Deleted | `OcrDashboardWindow.HistoryPaint.inl` |

## Semantics

No intentional product behavior change.

## Ownership / Gate progress

| Before | After |
|---|---|
| 11 production class-method `.inl` | **10** |
| Theme local to Window.cpp | shared header for multi-TU |

## Residual (D-I later)

- 10 production `.inl` remaining (StateAndHelpers, EntryPoints, Lifecycle, Layout, SourceRail, ImagePreview, Blocks, Import, Batch, Messages)
- Static cross-TU helper deps still block bulk conversion (promote free helpers as needed)
- MessageHandler still large
- full CMake dashboard target split later

## Ban check

- Net-delete production class-method `.inl`
- hermetic green
- No product behavior change

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| production class-method `.inl` | **10** (was 11) |

## Verdict

**D-I-1 done** (first real TU conversion).

## NEXT

1. Convert more leaf `.inl` files (Layout / EntryPoints after helper promotion)
2. Shrink MessageHandler
3. Toward D-I package exit (`.inl` = 0)
