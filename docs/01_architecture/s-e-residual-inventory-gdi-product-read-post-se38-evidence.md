# S-E residual inventory: GDI product-read arc (post S-E-38)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: residual inventory (docs only)  
Prior: S-E-38 `fa4fdd3e`

## Intent

Inventory residual dual authority after GDI draw style product-read for Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text/Magnifier/Watermark (S-E-32..38). No `src/` edits. Stage2 **~86** (ADR-002 **over 警戒 70** / 硬停 90).

## Closed this arc (S-E-32..38 GDI product-read)

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
| Host annotation vector | `m_screenshotAnnotations` | GDI geometry/hit-test/live-drag/export runtime sole | Host-vector delete last |
| Live drag mutate | Host mutates geometry during move/rotate/resize | GDI live sole | Host keep |
| Mosaic/HighLight/Eraser draw style | Host ann style props | not yet Document product-read | more tool GDI product-read |
| selected-index API | index still layout key after resolve | short-life layout | gradual delete |
| AnnotationRenderContext / registry | free draw helpers only | S-F shared-draw done | typed registry later |
| Delete Host vector | blocked | last tool group | §11.5 full |

Live product scan Geometry/Arrow/Pencil/BrokenLine/Marker/Serial/Text/Magnifier/Watermark draw style Host dual: **0** when Document item present.

## Research §11.5 package exit status

| Criterion | Status after S-E-38 |
|---|---|
| Document holds items | **partial PASS** |
| active/selected stable id | **PASS partial** |
| Document add/remove/replace/find/order | **API ready + product Document-first** |
| History commands act on Document | **PASS partial** |
| GDI draw Document product-read | **partial** — 9 tools DONE; Mosaic/HighLight/Eraser residual |
| vector index short-life only | **partial** |
| selected index API delete | **NOT YET** |
| Delete `m_screenshotAnnotations` | **blocked** |

**Full S-E package exit (§11.5): NOT closed.**  
**GDI product-read for 9 tools: DONE.**

## Recommended next domains (合域强制; Stage2 ~86 / 警戒 70 / 硬停 90)

1. **Mosaic GDI product-read** — draw style from Document by id. High value.
2. **HighLight GDI product-read** — remaining style dual.
3. **Eraser GDI product-read** — penWidth/pathMode dual.
4. **Host-vector delete plan** — docs plan for last vertical (near 硬停 90: prefer high-value only).

Default under over-警戒 near 硬停: **(1) Mosaic GDI product-read** as next WIP — one ownership domain, net dual-authority delete.

## KPI

| Metric | After S-E-38 residual |
|---|---:|
| hermetic | **60/60** |
| GDI product-read tools | **9** |
| product convert Overlay*.inl | **0** |
| Host vector GDI sole | still |
| Stage 2 code commits | **~86** (ADR-002 **over 警戒 70** / 硬停 90) |
| §11.5 package exit | **NOT closed** |

## NEXT

Docs pin residual inventory. Then Mosaic GDI product-read under 合域强制 (over 警戒 70 / near 硬停 90 — one high-value domain only). **Must prefill domain list before `src/` edits.**
