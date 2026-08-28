# D-D State/Controller — Package Exit Evidence (Implementation-Side)

Date: 2026-07-21  
Code HEAD: D-D-6 (this docs commit) / last code prior `36726579` + D-D-6  
Prior confirmed packages: D-A, D-B, D-C

## Status

**IMPLEMENTATION-SIDE COMPLETE — authorize D-D package exit +1 only after independent direction review accepts this evidence.**  
**NOT self-confirmed.** Package exit hard rule: implementing session cannot self-pass.

## §12.4 Research Acceptance Mapping

| §12.4 task / acceptance | Status | Evidence |
|---|---|---|
| Window handles vs business state separation | **PASS (D-D scope)** | Selection/filter/textMode/splitter/titlebar/hoveredAction on State; Controller pure |
| persisted/session/resolved layout layered | **PASS (prior + retained)** | DashboardLayoutState / Responsive / Resolved remain Host layout adapters |
| selection, active source, text mode in State | **PASS** | D-D-2/3/5 + prior field cutovers |
| command/event typed payload | **PASS** | `DashboardCommand.h` / `DashboardEvent.h` |
| Controller does not paint | **PASS** | `DashboardController.cpp` — no HWND/HDC/HBITMAP |
| State mutation testable without HWND | **PASS** | `test_dashboard_controller_contract` + `test_dashboard_state_contract` |
| State UI-thread only; workers immutable events | **PASS (contract retained)** | no worker holds State/Window ref in D-D scope |
| language/DPI via UI adapter | **PASS (Host)** | Host remains adapter |
| OcrDashboardWindow.h no longer declares **all** business collections | **PASS for D-D scope** | D-D domain collections (selection/filter/textMode dual-writes) deleted; residual collections owned by **other packages** (see Residual) |
| state transitions unit tests | **PASS** | hermetic controller + state contracts |
| Controller no HDC/HBITMAP | **PASS** | |
| Window still responds to commands/shortcuts | **PASS** | Host thin adapters; build green |

## Ownership cutovers (this reopen arc D-D-1..6)

| Slice | Cutover |
|---|---|
| D-D-1 | pure Controller seed (filter/textMode); Host thin adapters |
| D-D-2 | image/PDF selection Window fields deleted — State sole |
| D-D-3 | `m_sourceSelection` deleted — selectedSourceKey/Keys/Anchor sole |
| D-D-4 | `m_updatingSourceList` deleted — State sole |
| D-D-5 | Controller SelectHistoryIndex/Clear; SetSelectedHistoryIndex thin |
| D-D-6 | `m_hoveredActionBtn` deleted — State sole |

Prior field cutovers (titlebar/textMode/splitter/prev*/sourceSort) retained from earlier D-D work and still sole on State.

## Residual (not D-D package blockers)

| Residual | Owner package / reason |
|---|---|
| `m_batchTasks`, `m_dropQueue`, `m_activePdfJobs`, `m_pdfRender*`, failed job vectors, external OCR maps | **D-E** Batch Coordinator |
| `m_currentBlocks`, block runtime index | **D-G** Canvas/Blocks |
| `m_previewHost`, preview edit chrome | **D-H** Preview |
| `m_historyRanges`, `m_actionButtons`, `m_previewInfos` | Host paint geometry (HWND hit-test caches) — D-I / paint |
| `m_pendingFilterText` | Host debounce timer chrome (not business authority) |
| GDI image pointers, drag mouse points, fonts, HWNDs | Host chrome — correct residual |
| `DashboardState.h` still large aggregate | Ban: no further algorithm growth; structural split may continue later without reopening D-D domain |

## Hermetic

`ctest -L hermetic` in `build/cmake`: **54/54** Passed (post D-D-6).

## KPI snapshot

| Metric | Value |
|---|---:|
| hermetic | **54/54** |
| Controller | Command/Event + ApplyFilter/TextMode/Select |
| Dual-write selection fields (image/PDF/history multi/hover/updating) | **0** |

## Verdict (implementation-side)

D-D §12.4 domain goals met for State/Controller package scope.  
Remaining Window collections map to D-E/G/H/I, not D-D authority.

**Independent reviewer must confirm** before EXECUTION marks D-D **confirmed**.

## NEXT

1. Independent D-D package review  
2. On confirm → **D-E Batch Coordinator**  
3. Stage2 remains paused until Stage1 Gate
