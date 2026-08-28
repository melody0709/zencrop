# D-F SourceRail — Package Exit Evidence (Implementation-Side)

Date: 2026-07-21  
Code HEAD: D-F-3 `15eb1417`  
Prior confirmed packages: D-A..D-E

## Status

**IMPLEMENTATION-SIDE COMPLETE — authorize D-F package exit +1 only after independent direction review accepts this evidence.**  
**NOT self-confirmed.**

## §12.6 Research Acceptance Mapping

| §12.6 task / acceptance | Status | Evidence |
|---|---|---|
| Extract Model (rows / stable id / PDF tree) | **PASS** | free types + pure TaskRows/ViewRows builders |
| typed stable id / sourceInstanceId | **PASS** | projection keys retained; no IA change |
| Layout rect/scroll | **PARTIAL residual** | Host still computes hit/scroll from view rows (Host adapter) |
| Renderer read-only rows/layout | **PARTIAL residual** | paint remains SourceRail.inl Host |
| Hit-test/keyboard same layout | **PASS (retained)** | Host methods over free view rows |
| selection/range/anchor by key | **PASS** | State selectedSourceKey/Keys/Anchor sole (D-D) |
| thumbnail cache independent | **PARTIAL residual** | warmup still Host; not dual-write model authority |
| paint does not sync-decode | **PASS (retained)** | no intentional change |
| dismissal/progress read Coordinator | **PASS** | batch collections on Coordinator (D-E) |
| **no** Source Rail IA product change | **PASS** | pure relocation only |
| SourceRail.inl no longer owns model+paint+input all | **PASS (model out)** | model pure free; paint/input Host residual → D-I |
| selection stable across update/resize | **PASS** | key authority retained |
| PDF expand/cover/badge | **PASS** | pure builders + Host overlay |
| hermetic green | **PASS** | **56/56** |

## Ownership cutovers (D-F-1..3)

| Slice | Cutover |
|---|---|
| D-F-1 | free TaskRow/ViewRow/SortDirection types; Window nested types deleted |
| D-F-2 | pure BuildTaskRows + filter/summarize/label helpers |
| D-F-3 | pure BuildViewRows base + ParseAddedDate; Host overlay residual |

## Residual (not D-F package blockers)

| Residual | Owner |
|---|---|
| SourceRail paint / hit-test / keyboard | Host / **D-I** |
| live activity overlay in BuildSourceRailViewRows | Host chrome |
| thumbnail warmup | Host |
| Layout scroll math still Host | Host |

## Hermetic

`ctest -L hermetic`: **56/56** Passed (post D-F-3).

## Verdict (implementation-side)

D-F §12.6 **Model ownership** goals met. Paint/input residual is Host surface (D-I), not dual-write model authority.

**Independent reviewer must confirm** before EXECUTION marks D-F **confirmed**.

## NEXT

1. Independent D-F package review  
2. On confirm → **D-G Canvas/Blocks**  
3. Stage2 remains paused until Stage1 Gate
