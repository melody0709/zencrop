# D-D State/Controller — Package Exit Evidence

> **PARTIAL (2026-07-21):** retained as field-cutover evidence; the package-exit verdict is superseded because the original component/ownership acceptance is incomplete. Do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: D-D-8 (this commit)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`  
Prior package: D-C exit @ `docs/01_architecture/d-c-package-exit-evidence.md`

## Verdict

**IMPLEMENTATION-SIDE COMPLETE — authorize D-D package exit +1 after this evidence is accepted.**

Dashboard session/controller dual-write authorities for titlebar, text mode, preview availability, source sort, splitter geometry/drag, and prev size fields deleted. Residual Host layout structs (DPI, layout/responsive/resolved, pending filter debounce, source selection host cache) remain intentional HWND-tied Host boundary. Batch/Canvas/PDF-tree dual-write mirrors belong to **D-E / D-F / D-G**.

## Ownership cutovers (D-D-1..8)

| Slice | Legacy deleted | Sole authority |
|---|---|---|
| D-D-1 | `m_showTitlebar` | `DashboardState.showTitlebar` |
| D-D-2 | `m_textMode`, `m_preferredTextMode` | `DashboardState.textMode` |
| D-D-3 | `m_previewAvailable` (write-only dual-write) | `DashboardState.previewAvailable` |
| D-D-4 | `m_sourceSortDirection` | `DashboardState` sourceSort |
| D-D-5 | `m_splitterX`, `m_splitterRatio` | `DashboardState.splitterX/ratio` |
| D-D-6 | `m_sourceSplitterX`, `m_resultSplitterX` | `DashboardState` source/result splitter |
| D-D-7 | `m_draggingSplitter`, `m_splitterPressPending`, `m_draggingSplitterKind`, `m_splitterDragPreviewX` | `DashboardStateSyncSplitterDrag` |
| D-D-8 | `m_prevWidth`, `m_prevImageWidth`, `m_prevImageHeight` | `DashboardState` prev* APIs |

## Acceptance checklist

| Item | Status | Notes |
|---|---|---|
| Business state vs HWND separation advanced | **yes** | session prefs/controller flags sole on state |
| No net growth on frozen heads | **yes** | Messages **3622** (net delete from 3660); Route 5060; State 1320 |
| hermetic | **yes** | `ctest -L hermetic` **50/50** post D-D-8 |
| DashboardState.h not used as dumping ground | **yes** | no new domain fields this package; only existing dual-write deleted |

## Residual (not D-D package blockers)

- Host layout: `m_layout` / `m_responsiveLayout` / `m_resolvedLayout` / `m_dpi` / `m_pendingFilterText` / `m_sourceSelection` / `m_splitterPressPoint`
- `SyncHistoryModelMirror` — selection clamp helper (D-C residual)
- `SyncBatchSelectionMirror` / `SyncBatchRuntimeFlagsMirror` / `SyncBatchProgressMirror` / `SyncActiveOcrDisplayMirror` — **D-E**
- `SyncPdfTreeKeysMirror` — **D-F**
- `SyncCanvasViewMirror` / `SyncCanvasHoverMirror` — **D-G**

## Stage1 budget (post D-D-8)

- Stage1 code commits: **28** (D-B-1..11 + D-C-1..9 + D-D-1..8) of target ≤32 / hard stop 55
- ownership cutovers: **28**
- package exits after this evidence: **4/9** (D-A + D-B + D-C + D-D)

## NEXT

1. Accept this package evidence → EXECUTION package exit 4/9  
2. Open **D-E Batch Coordinator** first vertical cutover (runtime flags / progress / active OCR display)  
