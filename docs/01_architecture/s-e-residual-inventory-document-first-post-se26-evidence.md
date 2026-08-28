# S-E residual inventory: Document-first create/history arc (post S-E-26)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: residual inventory (docs only)  
Prior: S-E-26 `bc88bf63`

## Intent

Inventory residual dual authority after Document product-read (S-E-16..21) + Document-first create/history (S-E-22..26). No `src/` edits. Stage2 **~74** (ADR-002 **over 警戒 70** / 硬停 90).

## Closed this arc (S-E-16..26)

| Domain | Status | Evidence |
|---|---|---|
| Resolve selected by pure id / Document active | **DONE** | S-E-16 |
| Residual selected product-read paths | **DONE** | S-E-17 |
| Geometry/Arrow style product-read | **DONE** | S-E-18 |
| All-tools style product-read (Load) | **DONE** | S-E-19 |
| History after-snapshot product-read | **DONE** | S-E-20 |
| History before-snapshot product-read | **DONE** | S-E-21 |
| Document-first create (main tools) | **DONE** | S-E-22 |
| Text pending-create Document-first | **DONE** | S-E-23 |
| History insert Document-first | **DONE** | S-E-24 |
| History replace Document-first | **DONE** | S-E-25 |
| History remove Document-first | **DONE** | S-E-26 |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Host annotation vector | `m_screenshotAnnotations` | GDI/hit-test/edit/export runtime sole | tool-group vertical last |
| Dual-write Host→Document convert | `convertLegacyAnnotation` inside Add/ReplaceFromLegacy | migration projection | Document-native create later |
| History recovery convert | TakeSnapshotById fail → convertLegacy | empty Document / seed lag | keep until Host vector deleted |
| Load style recovery | Host ann when Document item missing | seed lag | keep |
| Product modify dual-write | DocumentReplaceFromLegacy after Host mutate | Host still GDI sole for live edit | Document-first modify later |
| AnnotationRenderContext / registry | free draw helpers only | S-F shared-draw done | typed registry later |
| Delete Host vector | blocked | last tool group | §11.5 full |

Live product scan Host-first create: **0**.  
Live product scan history apply Host-first: **0** (insert/replace/remove Document-first).

## Research §11.5 package exit status

| Criterion | Status after S-E-26 |
|---|---|
| Document holds items | **partial** — Document-first create/history; Host vector GDI sole |
| active/selected stable id | **PASS partial** — resolve + style + history product-read by id |
| Document add/remove/replace/find/order | **API ready** — product create/history Document-first |
| History commands act on Document | **PASS partial** — apply Document-first; snapshots product-read Document |
| vector index short-life only | **partial** — resolve prefers id; index recovery remains |
| selected index API delete | **not yet** |
| Tool-group vertical create/edit | create Document-first **DONE**; live edit still Host mutate + dual-write |
| Delete `m_screenshotAnnotations` | **blocked** |

**Full S-E package exit (§11.5): NOT closed.**

## What NOT to open next

- helper-only / thin rename without dual delete
- 1-field Document read slices
- idle AnnotationRenderContext without tool-group ownership cutover
- budget-burning slices over 警戒 70 without net dual-authority delete

## Recommended next domains (合域强制; Stage2 ~74 / 警戒 70 / 硬停 90)

1. **Live modify Document-first (Geometry/Arrow)** — reverse dual-write order for move/rotate/resize/style-apply: Document replace first, Host project. High value under over-警戒.
2. **S-E package-exit partial close** — declare Document product-read + Document-first create/history infrastructure DONE; remaining = Host vector delete + typed renderer.
3. **Delete residual recovery convert** only when Document always present (not yet safe).

Default under over-警戒: **(1) Geometry/Arrow live modify Document-first** as next WIP — one ownership domain, net dual-authority delete.

## KPI

| Metric | After S-E-26 residual |
|---|---:|
| hermetic | **60/60** |
| Document product-read (select/style/history) | **on** |
| Document-first create | **on** |
| history apply Document-first | **full on** |
| Host vector GDI sole | still |
| Stage 2 code commits | **~74** (ADR-002 **over 警戒 70** / 硬停 90) |
| §11.5 package exit | **NOT closed** |

## NEXT

Docs pin residual inventory. Then Geometry/Arrow live modify Document-first under 合域强制 (over 警戒 70 — one high-value domain only). **Must prefill domain list before `src/` edits.**
