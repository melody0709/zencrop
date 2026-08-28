# S-E residual inventory: Document-first full arc (post S-E-28)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: residual inventory (docs only)  
Prior: S-E-28 `f0956931`

## Intent

Inventory residual dual authority after full Document product-read + Document-first create/history/delete/clear arc (S-E-16..28). No `src/` edits. Stage2 **~76** (ADR-002 **over 警戒 70** / 硬停 90).

## Closed this arc (S-E-16..28)

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
| Product delete Document-first | **DONE** | S-E-27 |
| ClearAllMarks Document-first | **DONE** | S-E-28 |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Host annotation vector | `m_screenshotAnnotations` | GDI/hit-test/live-drag/export runtime sole | tool-group vertical last |
| Live drag mutate | Host mutates ann geometry during move/rotate/resize | GDI live sole | Host keep until vector delete |
| Live commit dual-write | DocumentReplaceFromLegacy after Host drag | Host still GDI sole during drag; commit dual-write Document | keep until Host vector deleted |
| Dual-write convert | `convertLegacyAnnotation` inside Add/ReplaceFromLegacy | migration projection Host→Document | Document-native later |
| Recovery convert | TakeSnapshotById fail → convertLegacy | empty Document / seed lag | keep |
| Load style recovery | Host ann when Document item missing | seed lag | keep |
| AnnotationRenderContext / registry | free draw helpers only | S-F shared-draw done | typed registry later |
| Delete Host vector | blocked | last tool group | §11.5 full |

Live product scan Host-first create: **0**.  
Live product scan history apply Host-first: **0**.  
Live product scan product delete Host-first: **0**.  
Live product scan ClearAll Host-first: **0**.

## Research §11.5 package exit status

| Criterion | Status after S-E-28 |
|---|---|
| Document holds items | **partial PASS** — Document-first create/history/delete/clear; Host GDI sole |
| active/selected stable id | **PASS partial** — resolve + style + history product-read by id |
| Document add/remove/replace/find/order | **API ready + product Document-first** |
| History commands act on Document | **PASS partial** — apply Document-first; snaps product-read Document |
| vector index short-life only | **partial** — resolve prefers id; index recovery remains |
| selected index API delete | **not yet** |
| Tool-group vertical create/edit | create Document-first **DONE**; live drag still Host |
| Delete `m_screenshotAnnotations` | **blocked** |

**Full S-E package exit (§11.5): NOT closed.**  
**Document product-read + Document-first create/history/delete infrastructure: DONE (S-E-16..28).**

## What NOT to open next

- helper-only / thin rename without dual delete
- 1-field Document read slices
- reverse live-drag Host→Document without GDI rewrite (too wide; Host must stay GDI sole during drag)
- budget-burning slices over 警戒 70 without net dual-authority delete

## Recommended next domains (合域强制; Stage2 ~76 / 警戒 70 / 硬停 90)

1. **S-E package-exit partial close** — declare Document product-read + Document-first infrastructure DONE; remaining = Host vector delete + typed renderer + selected-index API delete.
2. **AnnotationRenderContext / typed registry seed** — only if paired with tool-group ownership (research: no idle renderer).
3. **Host vector delete plan** — blocked until GDI/hit-test/live-drag no longer need Host vector (last vertical).

Default under over-警戒: **(1) S-E package-exit partial** as docs/evidence slice, then Stage2 continues under ADR-002 for remaining tool-group/Host-vector work only.

## KPI

| Metric | After S-E-28 residual |
|---|---:|
| hermetic | **60/60** |
| Document product-read (select/style/history) | **on** |
| Document-first create/history/delete/clear | **on** |
| Host vector GDI sole | still |
| Stage 2 code commits | **~76** (ADR-002 **over 警戒 70** / 硬停 90) |
| §11.5 package exit | **NOT closed** (infra partial **DONE**) |

## NEXT

Docs pin residual inventory. Then S-E package-exit partial close (docs) or high-value Host-vector path under 合域强制. **Must prefill domain list before `src/` edits.**
