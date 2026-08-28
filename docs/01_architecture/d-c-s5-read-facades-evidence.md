# D-C-S5 — Delete Thin History Read Facades Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S4 `2f377f05` (pure dismissal Build/Is)

## Purpose

Close research §12.3 residual: Window owned thin read facades over sole
`m_historyModel` store and sole `DashboardState.expandedHistoryIndex`.

Delete four Window methods; call sites use model/state directly.

## Change

| Item | Detail |
|---|---|
| `DashboardHistoryModel::itemAt` | New sole-store index read; `selected()` routes through it |
| Deleted Window methods | `HistoryItemForRead`, `SelectedHistoryItemForRead`, `HistoryItemsForKeys`, `SetExpandedHistoryIndex` |
| Call sites | History.cpp / SourceRail / Blocks / Batch / ImagePreview / HistoryPaint / Messages → `m_historyModel.itemAt` / `m_historyModel.items` / `DashboardStateSetExpandedHistoryIndex` |
| Kept Window | `SetSelectedHistoryIndex` (stable-key write authority from D-C-S1) |
| Model contract | Extended hermetic `itemAt` coverage |

### Semantics preserved

1. Out-of-range index → nullptr (same as former HistoryItemForRead).
2. Selected item reads `DashboardStateSelectedHistoryIndex` then `itemAt`.
3. Expanded index write sole on DashboardState (was already thin wrapper).
4. `HistoryItemsForKeys` dual-write pure helper in Model.h unchanged (different API).

### Not claimed

- D-C package exit (~38 Window methods remain).
- GetCurrentResult* orchestration still Window-owned.

## KPI

| Metric | Before S5 | After S5 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,459 | **2,440** |
| Window methods in History.cpp | ~42 | **~38** |
| `OcrDashboardWindow.h` physical | 907 | **903** |
| Messages / Route / State nonblank | 2416 / 145 / 1335 | unchanged |
| hermetic | 52/52 | **52/52** |

## Verdict

**D-C still PARTIAL.** Real Host method net-delete; read path no longer launders through Window.

## NEXT

1. D-C-S6: further Window method reduction (GetCurrentResult* / load-save / delete).
2. Independent review only at D-C strict package exit.
