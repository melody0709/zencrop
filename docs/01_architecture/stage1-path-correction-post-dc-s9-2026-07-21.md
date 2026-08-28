# Stage 1 Path Correction — 2026-07-21 (post D-C-S9)

Date: 2026-07-21  
Product HEAD at correction: `8507e841`  
Historical board: archived in the private research workspace

## Decision (user accepted)

Independent direction review found D-C-S8/S9 resumed **header-only pure helper** slices without Window owner/method net-delete. Path corrected:

1. **No D-C-S10.**
2. **D-C-S8/S9 = preparatory only** (code kept; not ownership cutover).
3. **D-B-CLOSE first.** D-B unconfirmed blocks D-C product work.
4. After D-B **confirmed**, D-C continues as **at most three result slices**: OWNER / PERSIST / PROJECTION (impl in `.cpp`, delete Window methods same commit).
5. **Ban** further non-template algorithm growth in `DashboardState.h`, `DashboardHistoryModel.h`, `DashboardHistoryCache.h`, `DashboardResultProjection.h`.

## Accounting

| Slice | Credit |
|---|---|
| D-C-S1..S7 | Valid method/owner progress (~47→~35 Window methods in History.cpp) |
| D-C-S8..S9 | Preparatory helpers only |
| D-B | Still REVALIDATE → current WIP |

## Next

**D-B-CLOSE** (evidence refresh → PdfOptions 2046 split/ADR → smoke/runtime → independent package review).

Does not reopen Stage 0. Does not authorize Stage 2.
