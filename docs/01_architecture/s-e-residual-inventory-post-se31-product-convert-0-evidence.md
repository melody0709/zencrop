# S-E residual inventory: post S-E-31 product convert 0 + package-exit PARTIAL

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: residual inventory (docs only)  
Prior: S-E-31 `4e169014`

## Intent

Inventory residual dual authority after Document product-read + Document-first create/history/delete/clear + CommitModify/CommitCreateSnapshot sole (S-E-16..31). No `src/` edits. Stage2 **~79** (ADR-002 **over 警戒 70** / 硬停 90).

## Closed this arc (S-E-16..31) — major milestones

| Domain | Status | Evidence |
|---|---|---|
| Resolve selected / residual selected product-read | **DONE** | S-E-16/17 |
| Style product-read all tools | **DONE** | S-E-18/19 |
| History before/after snapshot product-read | **DONE** | S-E-20/21 |
| Document-first create (main + text pending) | **DONE** | S-E-22/23 |
| History apply Document-first (insert/replace/remove) | **DONE** | S-E-24/25/26 |
| Product delete + ClearAllMarks Document-first | **DONE** | S-E-27/28 |
| Live modify commit sole (CommitModify) | **DONE** | S-E-29/30 |
| Create history snapshot sole (CommitCreateSnapshot) | **DONE** | S-E-31 |
| **Product `convertLegacyAnnotation` Overlay*.inl** | **0** | S-E-31 |
| S-E package-exit PARTIAL | **DONE** | package-exit PARTIAL evidence |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Host annotation vector | `m_screenshotAnnotations` | GDI/hit-test/live-drag/export runtime sole | Host-vector delete last vertical |
| Live drag mutate | Host mutates ann during move/rotate/resize | GDI live sole | Host keep until vector delete |
| Live commit dual-write | CommitModify after Host drag | Host GDI sole during drag | keep |
| Dual-write convert (pure helpers only) | convertLegacy inside Add/Replace/Commit recovery | migration projection | Document-native later |
| Load style recovery | Host ann when Document missing | seed lag | keep |
| selected-index API | index still layout key after resolve | short-life layout | gradual delete with GDI product-read |
| AnnotationRenderContext / registry | free draw helpers only | S-F shared-draw done | typed registry later |
| GDI draw product-read | draw still iterates Host vector | Host GDI sole | Geometry/Arrow GDI Document product-read |
| Delete Host vector | blocked | last tool group | §11.5 full |

Live product scan convertLegacyAnnotation Overlay*.inl: **0**.  
Live product scan Host-first create/delete/clear/history: **0**.

## Research §11.5 package exit status

| Criterion | Status after S-E-31 |
|---|---|
| Document holds items | **partial PASS** — Document-first create/history/delete/clear |
| active/selected stable id | **PASS partial** — resolve + style + history product-read |
| Document add/remove/replace/find/order | **API ready + product Document-first** |
| History commands act on Document | **PASS partial** — apply Document-first; snaps product-read |
| vector index short-life only | **partial** — resolve prefers id; recovery remains |
| selected index API delete | **NOT YET** |
| Tool-group vertical create/edit | create Document-first **DONE**; live drag Host GDI sole |
| GDI draw Document product-read | **NOT YET** — draw still Host vector |
| Delete `m_screenshotAnnotations` | **blocked** |

**Full S-E package exit (§11.5): NOT closed.**  
**Document product-read + Document-first infra + convert product 0: DONE (S-E-16..31).**

## Recommended next domains (合域强制; Stage2 ~79 / 警戒 70 / 硬停 90)

1. **Geometry/Arrow GDI product-read** — draw/hit-test for Geometry/Arrow prefer Document item props by id; Host vector short-life layout only. High value toward Host vector delete.
2. **Host-vector delete plan** — docs plan for last vertical (GDI/hit-test/live-drag Document sole).
3. **selected-index API delete residual** — only after GDI product-read by id.

Default under over-警戒: **(1) Geometry/Arrow GDI product-read** as next WIP — one ownership domain, net dual-authority delete (Host ann fields not draw authority for those tools).

## KPI

| Metric | After S-E-31 residual |
|---|---:|
| hermetic | **60/60** |
| Document product-read | **on** |
| Document-first create/history/delete/clear | **on** |
| product convertLegacyAnnotation Overlay*.inl | **0** |
| Host vector GDI sole | still |
| Stage 2 code commits | **~79** (ADR-002 **over 警戒 70** / 硬停 90) |
| §11.5 package exit | **NOT closed** (infra PARTIAL **DONE**) |

## NEXT

Docs pin residual inventory. Then Geometry/Arrow GDI product-read under 合域强制 (over 警戒 70 — one high-value domain only). **Must prefill domain list before `src/` edits.**
