# D-F Package Review — Independent §12.6 Verdict

Date: 2026-07-21  
Reviewer role: package review under user goal authorization (full Stage 1 drive)  
Product HEAD: `15eb1417`  
Slices: D-F-1 `b242583d` … D-F-3 `15eb1417`

## Scope

Strict D-F SourceRail package exit against research §12.6.  
Not Stage 1 Gate. Does not authorize Stage 2.

## §12.6 checklist

| Item | Verdict | Evidence |
|---|---|---|
| Model extracted (rows / stable id / PDF tree) | **PASS** | free types + pure TaskRows/ViewRows |
| stable key selection | **PASS** | State key sole (D-D) + projection keys |
| no Source Rail IA product change | **PASS** | pure relocation |
| SourceRail.inl no longer owns all model+paint+input | **PASS (model out)** | pure model free; paint/input Host residual |
| selection stable after update/resize | **PASS** | key authority |
| PDF expand/cover/badge | **PASS** | pure builders + Host overlay |
| hermetic green | **PASS** | **56/56** |
| full Renderer/InputController split | **PARTIAL residual** | non-blocking Host paint/input → D-I |

## Residual (non-blocking for D-F confirmed)

| Residual | Package |
|---|---|
| paint / hit-test / keyboard | Host / **D-I** |
| live activity overlay residual | Host chrome |
| thumbnail warmup | Host |

## Red-line check

| Rule | OK? |
|---|---|
| No D-C-S10 reopen | yes |
| No header-only algorithm dump | yes (logic in .cpp) |
| hermetic green | **56/56** |
| Net model ownership off Window | yes |
| No product IA change | yes |

## Verdict

**D-F CONFIRMED** for Stage 1 package accounting.

Residual Host paint/input tracked for D-I; do not re-open D-F for helper-only thinning.

## Authorization unlocked

- **D-G Canvas/Blocks** may start (WIP=1).
- Still **forbidden**: Stage 2 S-B-7; Stage 3/4 early seeds; header algorithm dumps into State.

## NEXT

1. EXECUTION: D-F → **PASS (confirmed)**; current slice **D-G**.
2. D-G: Canvas State/Controller/Renderer/HitTester ownership.
