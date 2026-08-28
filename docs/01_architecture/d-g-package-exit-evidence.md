# D-G Canvas/Blocks — Package Exit Evidence

> **PARTIAL (2026-07-21):** retained as field-cutover evidence; the package-exit verdict is superseded because the original Canvas component/ownership acceptance is incomplete. Do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: D-G-4 (this commit)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`  
Prior package: D-F exit @ `docs/01_architecture/d-f-package-exit-evidence.md`

## Verdict

**IMPLEMENTATION-SIDE COMPLETE — authorize D-G package exit +1 after this evidence is accepted.**

Canvas/Blocks dual-write authorities for canvas view, canvas hover, block selection/hover, reading-order flag, and preview-block content deleted. Residual Window canvas/hover **aliases** remain non-owning views of `DashboardState` (no dual-copy Sync). `m_currentBlocks` remains Host runtime collection (HWND paint/hit-test).

## Ownership cutovers (D-G-1..4)

| Slice | Legacy deleted | Sole authority |
|---|---|---|
| D-G-1 | Window owning canvas view dual-copy | `DashboardState.canvasView` (Window aliases) |
| D-G-2 | Window owning canvas hover dual-copy | `DashboardState` hover fields (aliases) |
| D-G-3 | `m_selectedBlockId`, `m_hoveredBlockId` | `DashboardState` block selection APIs |
| D-G-4 | `m_showReadingOrder`, `m_previewBlockContent` | `DashboardState` reading order / preview block APIs |

## Acceptance checklist

| Item | Status | Notes |
|---|---|---|
| Canvas zoom/pan/mode/overlay sole on state | **yes** | D-G-1 (aliases; Sync no-op) |
| Canvas hover sole on state | **yes** | D-G-2 (aliases; Sync no-op) |
| Block selection/hover sole on state | **yes** | D-G-3 |
| Reading order + preview block sole on state | **yes** | D-G-4 |
| hermetic | **yes** | `ctest -L hermetic` **50/50** post D-G-4 |
| freeze heads | **yes** | Messages **3611** (net delete); Route 5060; State 1320 |

## Residual (not D-G package blockers)

- Window canvas/hover **aliases** (`m_zoom` etc.) — non-owning; dual-copy deleted
- `m_currentBlocks` / GDI+ image / drag flags — Host runtime/paint
- Preview WebView2 host lifecycle — **D-H**
- MessageRoute dead predicates / class-method `.inl` production — **D-I**

## Stage1 budget (post D-G-4)

- Stage1 code commits: **40** (past target ≤32; hard stop 55)
- ownership cutovers: **40**
- package exits after this evidence: **7/9** (D-A…D-G)

## NEXT

1. Accept this package evidence → EXECUTION package exit 7/9  
2. Open **D-H Preview Coordinator** first vertical cutover  
