# D-G-1 — CanvasModel Block Ownership + Pure Hit-Test Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-F confirmed `463163b6`

## Purpose

Seed Canvas/Blocks ownership cutover:

- Move `DashboardOcrBlock` free type out of Window.h
- `DashboardCanvasModel` owns `currentBlocks` + `blockRuntimeIndex`
- Pure free hit-test: `DashboardCanvasHitTestBlock` / `HitTestBlockClient` / `PointInPolygon`
- Host `HitTestImageBlock` thin adapter

## Change

| Item | Detail |
|---|---|
| `DashboardCanvasModel.h` | free `DashboardOcrBlock`, `DashboardCanvasModel`, pure hit-test |
| `OcrDashboardWindow.h` | delete nested block type + `m_currentBlocks` / `m_blockRuntimeIndex`; add `m_canvas` |
| Host call sites | `m_canvas.currentBlocks` / `m_canvas.blockRuntimeIndex` |
| `test_dashboard_canvas_model_contract` | hermetic hit-test + ownership |

## Semantics

No intentional product behavior change. Hit-test prefers smallest-area containing block (top-most on equal area).

## Ownership

| Before | After |
|---|---|
| Window nested `DashboardOcrBlock` | free type in CanvasModel |
| Window `m_currentBlocks` / `m_blockRuntimeIndex` | `m_canvas.*` |
| Window owns hit-test algorithm | pure free hit-tester |

## Residual (D-G later)

- GDI image pointers (`m_gdiplusImage*`) Host chrome
- drag/mouse chrome (`m_draggingImage`, etc.) Host
- paint / copy-button layout still Blocks.inl Host
- CanvasController / Renderer not formed
- ImageAreaMessageHandler still large

## Ban check

- Pure hit-test in header (small template-free inline math; matches CanvasMath pattern)
- Net-delete Window owner fields
- Not helper-only

## KPI

| Metric | After |
|---|---:|
| hermetic | **57/57** |

## Verdict

**D-G-1 done.**

## NEXT

1. D-G residual: pure copy-button layout / more paint extraction
2. Toward D-G package exit
3. Then D-H Preview
