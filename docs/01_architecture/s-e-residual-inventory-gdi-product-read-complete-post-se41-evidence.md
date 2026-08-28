# S-E residual inventory: GDI product-read complete (post S-E-41)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: residual inventory (docs only)  
Prior: S-E-41 `c9b5f2de`

## Intent

Inventory residual dual authority after **all tool GDI draw style product-read complete** (S-E-32..41). No `src/` edits. Stage2 **~89** (ADR-002 **over 警戒 70** / near 硬停 90).

## Closed this arc (S-E-32..41 GDI product-read)

| Tool | Status | Evidence |
|---|---|---|
| Geometry | **DONE** | S-E-32 |
| Arrow | **DONE** | S-E-32 |
| Pencil | **DONE** | S-E-33 |
| BrokenLine | **DONE** | S-E-33 |
| Marker | **DONE** | S-E-34 |
| Serial | **DONE** | S-E-35 |
| Text | **DONE** | S-E-36 |
| Magnifier | **DONE** | S-E-37 |
| Watermark | **DONE** | S-E-38 |
| Mosaic (+ AutoMosaic) | **DONE** | S-E-39 |
| HighLight | **DONE** | S-E-40 |
| Eraser | **DONE** | S-E-41 |

**GDI product-read tools: 12/12 DONE.**

## Closed prior (S-E-16..31 Document-first infra)

| Domain | Status |
|---|---|
| Document product-read select/style/history | **DONE** |
| Document-first create/history/delete/clear | **DONE** |
| CommitModify / CommitCreateSnapshot sole | **DONE** |
| product convertLegacyAnnotation Overlay*.inl | **0** |
| S-E package-exit PARTIAL | **DONE** |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Host annotation vector | `m_screenshotAnnotations` (~131 sites) | GDI geometry/hit-test/live-drag/export runtime sole | Host-vector delete plan + multi-slice exit |
| Live drag mutate | Host mutates geometry during move/rotate/resize | GDI live sole | Host keep until vector exit |
| selected-index API | index still layout key after resolve | short-life layout | gradual delete after vector exit prep |
| AnnotationRenderContext / registry | free draw helpers only | S-F shared-draw done | typed registry later (Stage3?) |
| Delete Host vector | blocked | last tool group | §11.5 full |

Live product scan draw style Host dual for all 12 tools: **0** when Document item present.

## Research §11.5 package exit status

| Criterion | Status after S-E-41 |
|---|---|
| Document holds items | **partial PASS** |
| active/selected stable id | **PASS partial** |
| Document add/remove/replace/find/order | **API ready + product Document-first** |
| History commands act on Document | **PASS partial** |
| GDI draw Document product-read | **PASS** — 12/12 tools DONE |
| vector index short-life only | **partial** |
| selected index API delete | **NOT YET** |
| Delete `m_screenshotAnnotations` | **blocked** |

**Full S-E package exit (§11.5): NOT closed.**  
**GDI product-read for all tools: DONE.**

## Recommended next domains (合域强制; Stage2 ~89 / 警戒 70 / 硬停 90)

1. **Host-vector delete plan** — docs plan for last vertical (near 硬停 90: docs only preferred).
2. **selected-index API delete plan** — blocked until Host-vector exit path clear.
3. **AnnotationRenderContext** — Stage3 candidate; not Stage2 tool-group budget.

Default under near-硬停: **(1) Host-vector delete plan** as next WIP — docs only, no code budget burn.

## KPI

| Metric | After S-E-41 residual |
|---|---:|
| hermetic | **60/60** |
| GDI product-read tools | **12/12** |
| product convert Overlay*.inl | **0** |
| Host vector GDI sole | still (~131 sites) |
| Stage 2 code commits | **~89** (ADR-002 **over 警戒 70** / near 硬停 90) |
| §11.5 package exit | **NOT closed** |

## NEXT

Docs pin residual inventory. Then Host-vector delete plan under 合域强制 (near 硬停 90 — docs only; no more 1-tool style slices).
