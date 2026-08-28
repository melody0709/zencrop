# D-C-S3 — ResultProjection Pure Builders Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S2 `f6f3e67e` (cache ownership pure)

## Purpose

Close research §12.3 residual: result summary/JSON builders lived as
`DashboardHistory.cpp` file-statics and fed `GetCurrentResultText`.

Move pure Build*Json / Build*SummaryText / StripMarkdown into
`DashboardResultProjection` so Window only orchestrates selection + file IO.

## Change

| Item | Detail |
|---|---|
| `DashboardResultProjection.h` | Pure: Pdf page/job summary+JSON, Image task summary+JSON, History item JSON, StripMarkdown |
| `DashboardHistory.cpp` | Deleted static Build* bodies (~160 LOC); call sites use `DashboardResultProjection*` |
| `ReadUtf8FileToWide` | Stays local (IO helper; not pure projection) |
| `GetCurrentResultText` / preview markdown | Still Window-owned orchestration over selection + projection builders |
| `test_dashboard_result_projection_contract` | Hermetic: image/pdf/history shape, escape, page size, strip markdown |

### Semantics preserved

1. JSON field layout and escape match prior `EscapeJsonString` via `WideEscapeJsonString`.
2. Summary text labels (slash counts, DPI, page, ms) unchanged (`WideFormat*`).
3. History item index still 1-based in JSON (`index + 1`).
4. Empty blocks serialize via existing `OcrLayoutBlocksToJson`.

### Not claimed

- Full ResultProjection package (selection resolution / file IO / edit HWND fallback still Window).
- Window method count drop (still ~47 methods).
- D-C package exit.

## KPI

| Metric | Before S3 | After S3 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,683 | **2,516** |
| `DashboardResultProjection.h` | 0 | **187** |
| Messages / Route / State nonblank | 2416 / 145 / 1335 | unchanged |
| ScreenshotEditorState nonblank | 1467 | unchanged |
| hermetic | 51/51 | **52/52** (+result projection) |

## Verdict

**D-C still PARTIAL.** Pure result builders extracted toward §12.3
("result provider 不再直接散落在 Window"). Selection/file-path orchestration remains Host residual.

## NEXT

1. D-C-S4: reduce Window methods (dismiss/load-save facades, or further GetCurrentResult* thinning).
2. Independent review only at D-C strict package exit — not this slice.
