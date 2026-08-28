# D-C Package Review — Independent §12.3 Verdict

Date: 2026-07-21  
Reviewer role: package review under user goal authorization (full Stage 1 drive)  
Product HEAD: `03b57bab`  
Slices: OWNER `946e6b3c`, PERSIST `b15605fd`, PROJECTION `03b57bab`  
Prior S1–S7 method cutovers retained; S8/S9 preparatory only

## Scope

Strict D-C History package exit against research §12.3 and path-correction three result slices.  
Not Stage 1 Gate. Does not authorize Stage 2.

## §12.3 checklist

| Item | Verdict | Evidence |
|---|---|---|
| Repository load/save/atomic | **PASS** | `DashboardHistoryRepository` + store JSON contracts |
| Model items/filter/selection | **PASS** | `DashboardHistoryModel` + stable-key selection (D-C-S1) |
| Cache ownership/reference | **PASS** | `DashboardHistoryCache` + hermetic cache contract |
| ResultProjection builders | **PASS** | `DashboardResultProjection.cpp` TU; header decls only |
| Window holds facade only | **PASS** | `DashboardHistorySession m_history` (repo+model); no dual fields |
| Persist ops not Window bodies | **PASS** | Session free functions; 5 Window methods deleted (PERSIST) |
| Selection stable key | **PASS** | `DashboardStateSelectHistoryBySourceKey` write authority |
| old History JSON loadable | **PASS** | store/repository hermetic contracts |
| cache delete under OCR dir only | **PASS** | Cache Collect/ShouldDelete + path-under checks |
| Window methods reduced | **PASS (with residual)** | History.cpp **~47→~30** methods; remaining are Host UI/delete/result orchestration |

## Residual Host adapters (non-blocking for D-C confirmed)

These remain on Window by design as Host surface for D-I later:

1. `GetCurrentResultText` / preview markdown — selection resolution + file IO orchestration (builders pure).
2. `DeleteHistory*` / `ClearAll*` — MessageBox + batch queue coordination + session save.
3. Source list / filter / edit paint helpers — UI Host.

Path correction required OWNER/PERSIST/PROJECTION — all **done**. Full zero Window History methods is D-I Host/TU, not D-C sole exit.

## Red-line check

| Rule | OK? |
|---|---|
| Three result slices only after D-B | yes |
| No header-only ResultProjection algorithms | yes (PROJECTION) |
| No D-C-S10 helper treadmill | yes |
| hermetic green | **53/53** |
| Net Window method delete on PERSIST | yes (−5) |
| Session ownership cutover | yes |

## Verdict

**D-C CONFIRMED** for Stage 1 package accounting.

Residual Host methods tracked for D-I; do not re-open D-C for helper-only thinning.

## Authorization unlocked

- **D-D State/Controller** may start (WIP=1).
- Still **forbidden**: Stage 2 S-B-7; Stage 3/4 early seeds; header algorithm dumps into State.

## NEXT

1. EXECUTION: D-C → **PASS (confirmed)**; current slice **D-D**.
2. D-D: typed Command/Event + Controller without paint/GDI; split/stop growing `DashboardState.h`.
