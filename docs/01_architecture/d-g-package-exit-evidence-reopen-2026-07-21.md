# D-G Canvas/Blocks — Package Exit Evidence (Implementation-Side)

Date: 2026-07-21  
Code HEAD: D-G-1 `8ee2497b`  
Prior confirmed packages: D-A..D-F

## Status

**IMPLEMENTATION-SIDE COMPLETE — authorize D-G package exit +1 only after independent direction review accepts this evidence.**  
**NOT self-confirmed.**

## §12.7 Research Acceptance Mapping

| §12.7 task / acceptance | Status | Evidence |
|---|---|---|
| zoom/pan/fit | **PASS** | `DashboardCanvasMath` + State `canvasView` sole |
| block hover/selection | **PASS** | State sole (prior D-G field cutovers) |
| bbox/polygon hit-test | **PASS** | pure `DashboardCanvasHitTestBlock*` |
| reading order flag | **PASS** | State sole |
| copy/center | **PARTIAL residual** | Host Blocks.inl chrome |
| image control strip | **PARTIAL residual** | Host chrome |
| block runtime index | **PASS** | CanvasModel owns `blockRuntimeIndex` |
| selected block preview content | **PASS** | State sole |
| renderer no Window private fields | **PARTIAL residual** | paint Host → D-I |
| DashboardBlockRuntimeIndex runtime-only | **PASS** | retained |
| Preview/Canvas stable block id | **PASS** | no intentional change |
| 4K downsample coordinates | **PASS** | no intentional change |
| ImageAreaMessageHandler shrink | **PARTIAL** | hit-test thinned; paint residual |

## Ownership cutovers

| Slice | Cutover |
|---|---|
| D-G-1 | free `DashboardOcrBlock`; CanvasModel owns blocks+index; pure hit-test |
| prior | canvasView / hover/selected block id / reading order / previewBlockContent on State |

## Residual (not D-G package blockers)

| Residual | Owner |
|---|---|
| GDI image pointers / drag mouse chrome | Host |
| paint / copy-button layout | Host / **D-I** |
| ImageAreaMessageHandler body | Host / **D-I** |

## Hermetic

`ctest -L hermetic`: **57/57** Passed (post D-G-1).

## Verdict (implementation-side)

D-G §12.7 **block/canvas model + pure hit-test** goals met. Paint residual is Host surface (D-I).

**Independent reviewer must confirm** before EXECUTION marks D-G **confirmed**.

## NEXT

1. Independent D-G package review  
2. On confirm → **D-H Preview**  
3. Stage2 remains paused until Stage1 Gate
