# S-E residual inventory: post S-E-9 Document dual-write cutover

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E residual inventory (docs only)  
Prior: S-E-9 `e86873bd` Document incremental dual-write cutover (TTL 3/3 CUTOVER)

## Intent

Inventory residual dual authority / package-exit gaps after pure dual-authority method deletes (S-E-1..6) + Document dual-write seed/deepen/cutover (S-E-7..9). No `src/` edits. Stage2 **~48** (过警戒 45；合域强制；硬停 55).

## Closed this arc (S-E-1..9)

| Domain | Status | Evidence |
|---|---|---|
| Host pure dual getters/setters/predicates | **DONE** | S-E-1..6 residual recheck |
| Document dual-write seed (member + full rebuild) | **DONE** | S-E-7 |
| selected stable-id dual-write + Document active | **DONE** | S-E-8 |
| Document incremental dual-write cutover | **DONE TTL 3/3** | S-E-9 |
| product mutation full rebuild dual-write | **0** | Settings recovery only |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Host annotation vector | `m_screenshotAnnotations` | GDI/hit-test/edit/export runtime sole | tool-group vertical (S-E/S-F) |
| Document product **reads** | Document write-only in product; reads still Host vector index | no product path uses `findById`/`activeItem` for business logic | Document product-read deepen (safe select/find) |
| selected index API | index still primary layout/hit-test key | short-life index OK per research; long-term selected-index APIs remain | gradual delete with vertical groups |
| History apply | pure mutations + Host vector apply + incremental Document dual-write | Host undo stacks | History-on-Document later |
| Settings recovery | `SyncFromLegacy` after load | re-seed only, not mutation dual-write | keep until Host vector deleted |
| Text edit / IME | Host vector + HWND | session side-effects | Host keep |
| Style apply | mutates Host annotations | Host vector | dual-write replace already on some paths |
| Hit-test bounds | Host annotation struct | GDI geometry | Host keep until renderer |

Live product mutation scan `DocumentSyncFromLegacy`: **0** (Settings recovery only).  
Live product Document **read** (business path `findById`/`activeItem`/`count`): **0** (mirror write only).

## Research §11.5 package exit status

| Criterion | Status after S-E-9 |
|---|---|
| Document holds items | **partial** — Document mirrors via incremental dual-write; Host vector still runtime sole for GDI |
| active/selected stable id | **partial** — pure `selectedAnnotationId` + Document active dual-write; product still drives by index |
| Document add/remove/replace/find/order | **API ready** — product uses incremental add/remove/replace/clear; find not product-read |
| History commands act on Document | **partial** — pure mutations; Host apply + Document incremental |
| vector index short-life only | **not yet** — index still long-lived selection/edit key |
| selected index API delete | **not yet** |
| Legacy struct → migration ns | **not yet** |
| AnnotationRenderContext / registry | **deferred S-F** |
| Tool-group vertical cutover | **NOT STARTED** (Geometry/Arrow … AutoMosaic) |
| Delete `m_screenshotAnnotations` | **blocked** on last tool group |

**Full S-E package exit (§11.5): NOT closed.**

## What NOT to open next

- 1-field slices / helper-only / no-op Sync
- full Document product-read rewrite of all hit-test/GDI (too wide; hard stop 55)
- idle S-F renderer without tool-group vertical
- new dual-write TTL chain without cutover plan

## Recommended next domains (合域强制; Stage2 ~48 / 硬停 55)

Pick **one** domain only:

1. **S-E residual product-read deepen (narrow)** — one safe product path read Document by stable id (e.g. select/active resolve) without GDI rewrite; must delete dual index authority at that path. Prefer if residual can net-delete selected-index dual use.
2. **S-E package exit inventory close as partial + open S-F/tool-group** — declare S-E infrastructure DONE (pure methods + Document dual-write cutover); remaining vertical groups = S-E/S-F joint work; first tool group Geometry/Arrow as next package slice chain.
3. **S-F shared Annotation Renderer seed with Geometry group** — only if paired with create/edit/hit-test/history/export for that group (research: no idle renderer).

Default recommendation under hard-stop pressure: **(2) then first Geometry/Arrow vertical** as next WIP package — residual pure dual-authority + Document dual-write infrastructure already delivered; package exit full §11.5 needs tool groups which are multi-commit verticals.

## KPI

| Metric | After S-E-9 residual |
|---|---:|
| hermetic | **60/60** |
| product mutation full rebuild | **0** |
| Document dual-write TTL | **3/3 CUTOVER** |
| Host pure dual-authority methods | **0** |
| tool-group vertical | **NOT STARTED** |
| §11.5 package exit | **NOT closed** |
| Stage 2 code commits | **~48** (过警戒 45；合域强制；硬停 55) |

## NEXT

Docs pin residual inventory. Then choose next ownership domain under 合域强制 (prefer package-exit partial close + Geometry/Arrow vertical or narrow Document product-read). **Must prefill domain list before `src/` edits.**
