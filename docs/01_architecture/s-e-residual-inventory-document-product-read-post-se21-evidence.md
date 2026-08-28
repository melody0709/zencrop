# S-E residual inventory: Document product-read arc (post S-E-21)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: residual inventory (docs only)  
Prior: S-E-21 `e133a062`

## Intent

Inventory residual dual authority after Document product-read arc S-E-16..21. No `src/` edits. Stage2 **~69** (ADR-002 警戒 70 / 硬停 90).

## Closed this arc (S-E-16..21)

| Domain | Status | Evidence |
|---|---|---|
| Resolve selected by pure id / Document active | **DONE** | S-E-16 |
| Residual selected product-read paths | **DONE** | S-E-17 |
| Geometry/Arrow style product-read | **DONE** | S-E-18 |
| All-tools style product-read (Load) | **DONE** | S-E-19 |
| History after-snapshot product-read | **DONE** | S-E-20 |
| History before-snapshot product-read | **DONE** | S-E-21 |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Host annotation vector | `m_screenshotAnnotations` | GDI/hit-test/edit/export runtime sole | tool-group vertical |
| Dual-write Host→Document | `convertLegacyAnnotation` inside Add/ReplaceFromLegacy | migration projection Host→Document | Document-first create later |
| History recovery convert | TakeSnapshotById fail → convertLegacy | empty Document / seed lag | keep until Host vector deleted |
| Load style recovery | Host ann when Document item missing | seed lag | keep |
| create path order | Host push_back then DocumentAdd | Host still GDI sole | Geometry/Arrow create Document-first |
| AnnotationRenderContext / registry | free draw helpers only | S-F shared-draw done | typed registry later |
| Delete Host vector | blocked | last tool group | §11.5 full |

Live product scan `convertLegacyAnnotation` in product paths: recovery + dual-write projection only (not product-read authority when Document holds item).

## Research §11.5 package exit status

| Criterion | Status after S-E-21 |
|---|---|
| Document holds items | **partial** — dual-write; Host vector GDI sole |
| active/selected stable id | **PASS partial** — resolve + style + history product-read by id |
| Document add/remove/replace/find/order | **API ready** — product uses incremental dual-write |
| History commands act on Document | **partial** — snapshots product-read Document; apply still Host vector + dual-write |
| vector index short-life only | **partial** — resolve prefers id; index recovery remains |
| selected index API delete | **not yet** |
| Tool-group vertical create/edit | style+history product-read **DONE**; create Document-first **NOT** |
| Delete `m_screenshotAnnotations` | **blocked** |

**Full S-E package exit (§11.5): NOT closed.**

## What NOT to open next

- helper-only / thin rename without dual delete
- 1-field Document read slices
- idle AnnotationRenderContext without tool-group ownership cutover
- budget-burning slices near 警戒 70 without net dual-authority delete

## Recommended next domains (合域强制; Stage2 ~69 / 警戒 70)

1. **Geometry/Arrow create Document-first** — reverse dual-write order for Geometry/Arrow create: Item+Document first, Host vector projection for GDI; net-delete Host-first authority for those types. High value under budget.
2. **S-E package-exit partial close** — declare Document product-read infrastructure DONE; remaining = tool-group create Document-first + Host vector delete chain.
3. **Delete residual recovery convert sites** only when Document always present (not yet safe).

Default under 警戒 70: **(1) Geometry/Arrow create Document-first** as next WIP — one ownership domain, net dual-authority delete, ADR-002 tool-group vertical.

## KPI

| Metric | After S-E-21 residual |
|---|---:|
| hermetic | **60/60** |
| Document product-read (select/style/history) | **on** |
| Host vector GDI sole | still |
| Stage 2 code commits | **~69** (ADR-002 警戒 70 / 硬停 90) |
| §11.5 package exit | **NOT closed** |

## NEXT

Docs pin residual inventory. Then Geometry/Arrow create Document-first under 合域强制 (near 警戒 70 — one high-value domain only). **Must prefill domain list before `src/` edits.**
