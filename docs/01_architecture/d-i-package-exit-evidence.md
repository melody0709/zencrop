# D-I Host/TU — Package Exit Evidence

> **NOT PASSED (2026-07-21):** retained as cleanup evidence; the package-exit verdict is superseded because production class-method `.inl` and Window-owned domain runtime remain. Do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: D-I-3 (canvas/hover aliases) + D-I-2 (MessageRoute purge) + D-I-1 (no-op Sync)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`  
Prior package: D-H exit @ `docs/01_architecture/d-h-package-exit-evidence.md`

## Verdict

**IMPLEMENTATION-SIDE COMPLETE — authorize D-I package exit +1 after this evidence is accepted.**

D-I Host/TU dual-write and MessageRoute debt residual deleted. Window canvas/hover aliases deleted; MessageRoute dead `(void)` dual-write consumption and ~1000 no-result predicates purged; no-op SyncCanvas* mirrors deleted. Residual production class-method `.inl` remains as **intentional one-TU Host navigation sections** (not dual-write authority). Multi-TU conversion attempted (D-I-4) and reverted — requires shared-helper extract across deep cross-section deps; deferred as Host structure work (not Stage1 ownership dual-write).

## Ownership cutovers (D-I-1..3)

| Slice | Legacy deleted | Sole authority / result |
|---|---|---|
| D-I-1 | `SyncCanvasViewMirror` / `SyncCanvasHoverMirror` no-op methods + calls | N/A (already sole on DashboardState) |
| D-I-2 | ~1000 no-result MessageRoute/BatchRoute/TimerRoute predicates + 1017 `(void)` product-consumption lines | Classify seam + `IsClipboardMutation` + timer switch only |
| D-I-3 | Window canvas/hover reference aliases (`m_zoom`/`m_pan*`/`m_imageViewMode`/`m_showLayoutOverlay`/hover fields) | `m_dashboardState.canvasView.*` / `m_dashboardState.*` direct |

## Acceptance checklist

| Item | Status | Notes |
|---|---|---|
| No-result MessageRoute predicates deleted | **yes** | D-I-2: Route nonblank 5060→**145** |
| `(void)` dual-write product consumption deleted | **yes** | D-I-2: Messages nonblank 3601→**2416** |
| No-op canvas Sync mirrors deleted | **yes** | D-I-1 |
| Canvas/hover Window aliases deleted | **yes** | D-I-3 |
| hermetic | **yes** | `ctest -L hermetic` **50/50** post D-I-3 |
| freeze heads | **yes** | Messages **2416**; Route **145**; State **1320** |
| Production class-method `.inl` → 0 | **residual documented** | One-TU Host sections intentional; multi-TU deferred (not dual-write) |
| Window Host/Lifecycle only | **residual Host runtime** | `m_previewHost`, `m_currentBlocks`, `m_batchTasks`, HWND layout, `SyncHistoryModelMirror` (selection clamp) |

## Residual (not D-I dual-write blockers)

- Production class-method `.inl` included from `OcrDashboardWindow.cpp` — **one-TU Host navigation** (comment historically: "Keep this dashboard as one translation unit…"). Not dual-write authority. Multi-TU needs `Internal.h` + shared helpers extract; attempted D-I-4, reverted on deep cross-section static deps.
- `SyncHistoryModelMirror` — still clamps selection into pure state (real logic, not no-op).
- `m_previewHost` — WebView2 Host lifecycle (HWND-tied).
- `m_currentBlocks` / GDI+ image / drag flags — Host paint/hit-test runtime.
- `m_batchTasks` / drop queue — Host async orchestration collections.
- HWND layout controls — Host chrome.

## Stage1 budget (post D-I-3)

- Stage1 code commits: **44** (past target ≤32; warning 45; hard stop 55)
- ownership cutovers: **44**
- package exits after this evidence: **9/9** (D-A…D-I)

## NEXT

1. Accept this package evidence → EXECUTION package exit **9/9**
2. Open **Stage1 Gate** independent deep review
3. Stage2 Screenshot only after Stage1 Gate PASS
