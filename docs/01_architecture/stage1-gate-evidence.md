# Stage 1 Gate — Evidence Pack

> **SUPERSEDED (2026-07-21):** this pack records useful field-cutover evidence, but its Stage 1 completion claim failed strict GOAL/research acceptance. Stage 1 is reopened; do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: `8ce24fd4` (D-I package exit docs) / last code `3b1bcb2a` (D-I-3)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`

## Verdict (implementation-side)

**IMPLEMENTATION-SIDE COMPLETE — authorize Stage1 Gate independent review.**

Stage1 ownership packages D-A…D-I all exited with evidence. Dual-write Window authorities for Dashboard domains deleted or documented as Host-only residual. Freeze heads net-delete or flat. Hermetic 50/50.

## Package exits (9/9)

| Package | Evidence | Code anchor |
|---|---|---|
| D-A Test fixture | tests moved out of production path (prior) | Stage0/1 early |
| D-B Import/Dialogs | `docs/01_architecture/d-b-package-exit-evidence.md` | dialogs/OLE + session prefs |
| D-C History | `docs/01_architecture/d-c-package-exit-evidence.md` | items/selection/filter/persistence |
| D-D State/Controller | `docs/01_architecture/d-d-package-exit-evidence.md` | titlebar/textMode/splitter/prev* |
| D-E Batch | `docs/01_architecture/d-e-package-exit-evidence.md` | runtime/progress/activeOCR |
| D-F SourceRail | `docs/01_architecture/d-f-package-exit-evidence.md` | PDF tree keys + batch selection |
| D-G Canvas/Blocks | `docs/01_architecture/d-g-package-exit-evidence.md` | canvas/hover/block/readingOrder |
| D-H Preview | `docs/01_architecture/d-h-package-exit-evidence.md` | preview edit/persistence flags |
| D-I Host/TU | `docs/01_architecture/d-i-package-exit-evidence.md` | MessageRoute purge + aliases + no-op Sync |

## Ownership cutovers

**44** Stage1 ownership cutover slices (past target ≤32; under hard stop 55).  
D-B-1..11 + D-C-1..9 + D-D-1..8 + D-E-1..4 + D-F-1..4 + D-G-1..4 + D-H-1 + D-I-1..3.

## Freeze heads (nonblank LOC)

| Head | Baseline (Stage0) | Now | Rule |
|---|---:|---:|---|
| `OcrDashboardWindow.Messages.inl` | 3660 | **2416** | no growth — **net delete** |
| `DashboardMessageRoute.h` | 5060 | **145** | no growth — **net delete** |
| `DashboardState.h` | 1320 | **1320** | freeze flat |
| `ScreenshotEditorState.h` | 1467 | 1467 (untouched Stage1) | Stage2 freeze |

## Hermetic

`ctest -L hermetic` in `build/cmake`: **50/50** Passed (re-run 2026-07-21 post D-I package exit).

## Residual dual-write scan (product)

| Pattern | Result |
|---|---|
| `(void)Dashboard*` product consumption | **0** |
| `SyncCanvasViewMirror` / `SyncCanvasHoverMirror` | comments only |
| Window aliases `m_zoom` / `m_panX` | **0** |
| Deleted field names as live members | comments only (or other windows e.g. OcrResultWindow) |

## Host residual (not Stage1 dual-write blockers)

- Production class-method `.inl` as **one-TU Host navigation** (multi-TU deferred; D-I-4 attempted/reverted)
- `SyncHistoryModelMirror` selection clamp (real logic)
- `m_previewHost`, `m_currentBlocks`, `m_batchTasks`, HWND layout — Host runtime

## GOAL § Stage1 checklist (implementation)

- [x] History/Batch/SourceRail/Canvas/Preview own state via DashboardState
- [x] Window dual-write authorities deleted for D-B…D-I domains
- [x] MessageRoute freeze: only net-delete (5060→145)
- [x] hermetic 50/50
- [x] package exits 9/9 with evidence
- [x] no new AppHost seed in Stage1
- [ ] **Independent reviewer confirms evidence** ← Gate
- [ ] tag `stage-1-gate-complete` after PASS
- [ ] open Stage2 Screenshot only after PASS

## NEXT

1. Independent deep review (this or other session)  
2. On PASS: tag + Stage2 S-A  
