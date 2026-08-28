# D-I Host/TU — Package Exit Evidence (reopen path)

Date: 2026-07-22  
Package: D-I Host/TU  
Product HEAD: `8bf5f9e9` (D-I-4)  
Prior: D-I-3 `fdfffe8c`, D-I-2 `6d5ab679`, D-I-1 `6e35fe04`  
Supersedes: historical `d-i-package-exit-evidence.md` (NOT PASSED; residual `.inl` claim obsolete)

## Intent

Close Stage 1 package D-I against research **§12.9** after production class-method `.inl` reached **0** and Host sections are real TUs.

## §12.9 checklist

| Item | Status | Evidence |
|---|---|---|
| Main WndProc only takes self + forwards | **yes** | `WndProc` → `MessageHandler` only |
| MessageHandler routes command/event/subview | **yes** | still Host router; pure route classify residual in MessageRoute |
| ImageArea/SourceRail/Subclass controllers | **partial residual** | sub-WndProcs remain Host methods (paint/input); domain ownership already on models |
| `.inl` → real `.cpp` | **yes** | **0** production class-method `.inl` |
| `OcrDashboardWindow.cpp` Host/Lifecycle only | **yes** | **301** phys; section TUs separate |
| History window methods cleanup | **yes** | D-C OWNER/PERSIST/PROJECTION prior packages |
| CMake dashboard model/controller/ui split | **partial** | multi-TU sources under one target; dependency direction Host → models (no new cycles) |
| Production class-method `.inl` = 0 | **yes** | glob empty under `dashboard/OcrDashboardWindow*.inl` |
| MessageHandler &lt; ~300 lines **or explicit exemption** | **exemption** | see below |
| Window does not own Repository / Batch queue | **yes** | `m_history` session facade; `m_batch` coordinator sole queues |
| Target deps one-way | **yes** | hermetic green; no cycle increase reported |
| Hermetic green | **yes** | **58/58** |

## MessageHandler exemption (§12.9)

| Metric | Value |
|---|---:|
| `OcrDashboardWindow.Messages.cpp` physical | **2542** |
| `MessageHandler` span (to next method) | **~1170** lines |
| nonblank in span | **~1118** |

**Exemption:** MessageHandler remains a Host Win32 router (timers, async completions, chrome commands, subview dispatch). Domain decisions live in Controller/State/BatchCoordinator/SourceRailModel/CanvasModel/PreviewCoordinator. Further thinning is optional Host structure work, **not** a dual-write/ownership blocker for Stage 1 package exit. Full extract of MessageHandler into per-command handlers is deferred past Stage 1 Gate (may track as Stage 3 Host polish).

## Ownership residual (Host chrome — non-blocking)

| Residual | Notes |
|---|---|
| `m_previewHost` unique_ptr | WebView2 HWND lifecycle |
| `m_historyRanges` / action buttons | paint layout Host |
| `m_canvasImagePath` | Host image path for GDI+ load |
| sub-WndProcs | ImageArea/SourceRail/Splitter Host input |
| MessageHandler size | exempted above |

## KPI (D-I-4 HEAD)

| Metric | Value |
|---|---:|
| production class-method `.inl` | **0** |
| hermetic CTest | **58/58** |
| `OcrDashboardWindow.cpp` | **301** phys |
| `OcrDashboardWindow.h` | **791** phys |
| Host section real TUs | **11** |

## Slice trail

| Slice | Commit | Result |
|---|---|---|
| D-I-1 HistoryPaint + Theme | `6e35fe04` | `.inl` 11→10 |
| D-I-2 Layout + MeasureButtonWidth | `6d5ab679` | `.inl` 10→9 |
| D-I-3 EntryPoints/Lifecycle/Import/Blocks + HostUtils/HostIds | `fdfffe8c` | `.inl` 9→5 |
| D-I-4 heavy TUs + HostTypes/Internals | `8bf5f9e9` | `.inl` 5→**0** |

## Freeze heads

Messages route surface freeze respected for dual-write debt; no unauthorized State algorithm growth in this package close.

## Verdict (implementation-side)

**READY FOR INDEPENDENT PACKAGE REVIEW.**  
Implementing session does not self-confirm package exit.
