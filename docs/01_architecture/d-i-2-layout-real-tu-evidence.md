# D-I-2 — Layout Real TU + Free MeasureButtonWidth Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-I-1 `6e35fe04`

## Purpose

Continue production class-method `.inl` → real `.cpp` conversion:

- Convert `OcrDashboardWindow.Layout.inl` → `.cpp` TU
- Promote `MeasureButtonWidth` to free `DashboardMeasureButtonWidth` in `DashboardDialogLayout.h`
- Remove Layout from OcrDashboardWindow.cpp `#include` chain

## Change

| Item | Detail |
|---|---|
| `OcrDashboardWindow.Layout.cpp` | real TU (was `.inl`) |
| `DashboardDialogLayout.h` | free `DashboardMeasureButtonWidth` |
| `OcrDashboardWindow.cpp` | drop Layout `#include`; thin wrap measure helper |
| CMake | list Layout.cpp; drop Layout.inl |
| Deleted | `OcrDashboardWindow.Layout.inl` |

## Semantics

No intentional product behavior change.

## Ownership / Gate progress

| Before | After |
|---|---|
| 10 production class-method `.inl` | **9** |
| MeasureButtonWidth Window.cpp static | free shared helper |

## Residual (D-I later)

- 9 production `.inl` remaining
- Static cross-TU helper deps still block bulk conversion
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
| production class-method `.inl` | **9** (was 10) |

## Verdict

**D-I-2 done.**

## NEXT

1. Convert EntryPoints / Lifecycle / more leaf TUs
2. Promote remaining static helpers as needed
3. Toward D-I package exit (`.inl` = 0)
