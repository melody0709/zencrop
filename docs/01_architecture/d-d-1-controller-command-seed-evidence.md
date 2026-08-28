# D-D-1 — Controller / Command / Event Seed Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C package review confirmed `aa13859c` area

## Purpose

Seed pure Dashboard Controller for filter + text-mode transitions:

- typed `DashboardCommand` / `DashboardEvent`
- pure `DashboardController` (no HWND / HDC / HBITMAP)
- Host thin adapters apply UI side effects from events

## Change

| Item | Detail |
|---|---|
| `DashboardCommand.h` | SetFilter / SetTextMode / SelectHistoryIndex / ClearHistorySelection |
| `DashboardEvent.h` | FilterChanged / TextModeChanged / VisibleHistoryChanged / SourceListRebuildRequired + flags |
| `DashboardController.h/.cpp` | ApplyFilter / ApplyTextMode / Dispatch / ProjectionLinkedHistoryIndices |
| `DashboardHistory.cpp` | `ApplyFilter` uses Controller for filter text + visible indices |
| `OcrDashboardWindow.ImagePreview.inl` | `SetTextMode` uses Controller; Host applies select/preview/layout/persist |
| `OcrDashboardWindow.cpp` | include Controller |
| CMake | list Controller TU |
| `test_dashboard_controller_contract` | hermetic pure-state contract (no HWND) |

## Semantics

No intentional product behavior change:

- Filter still rebuilds visible indices + source list + selection stability in Host
- Text mode still EnsurePreviewHost / Fallback / Layout / RebuildHistoryText / Persist

State mutations for filter/text-mode testable without HWND.

## Ownership

| Before | After |
|---|---|
| ApplyFilter dual: Host sets filter + builds visible | Controller owns filter/visible mutation + events |
| SetTextMode dual: Host ApplyTextMode + clear block | Controller owns ApplyTextMode + clear block + event flags |
| No typed Command/Event surface | Command/Event + Dispatch seed |

Host residual: RebuildSourceList, selection restore, preview host, layout, paint — correct for D-D-1 (no paint in Controller).

## Ban check

- **No** algorithm growth in `DashboardState.h` this slice
- Controller logic lives in `.cpp`
- No D-C-S10 reopen

## KPI

| Metric | Before | After |
|---|---:|---:|
| hermetic | 53/53 | **54/54** |
| Controller files | 0 | Command/Event/Controller.* |
| `DashboardState.h` | 1570 | unchanged (no algo dump) |
| `test_dashboard_controller_contract` | n/a | **PASS** |

## Verdict

**D-D-1 seed done** (hermetic green).

Not package exit: D-D still needs more Command kinds, stop State growth, more Host collection cutovers.

## NEXT

1. Expand Controller (selection commands) without paint
2. D-D residual slices toward package exit
3. Then D-E…D-I per EXECUTION queue
