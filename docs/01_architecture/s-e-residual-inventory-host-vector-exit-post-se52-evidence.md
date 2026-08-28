# S-E residual inventory: Host-vector exit post S-E-52

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: residual inventory (docs only)  
Prior: S-E-52 `78625cc9`

## Intent

Inventory residual dual authority after textEditingId sole + Document.empty() early-out (S-E-52). No `src/` edits. Stage2 **~97** (ADR-003 警戒 100 / 硬停 120).

## Closed this arc (S-E-43..52 Host-vector exit)

| Domain | Status | Evidence |
|---|---|---|
| Geometry product-read export/preview/hit-test | **DONE** | S-E-43/44 |
| Document-order iterate + hit-test | **DONE** | S-E-45/46 |
| Host projection sole rebuild | **DONE** | S-E-47 |
| Product select-by-id sole | **DONE** | S-E-48 |
| product SelectedAnnotationIndex dual | **0** | S-E-50 |
| textEditingId sole | **DONE** | S-E-52 |
| Document.empty() early-out | **DONE** | S-E-52 |
| GDI product-read tools 12/12 | **DONE** | S-E-32..41 |
| Host-vector member delete prep | **DONE** | S-E-51 |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Host vector member | `m_screenshotAnnotations` ~141 sites | Live-drag + text-edit mutable projection cache; RebuildHostProjection target | rename → projection sole name, then delete when local/rebuild-only |
| Live drag mutate | Host mutates geometry mid-drag; CommitModify on mouse-up | GDI live sole | Host projection keep |
| Live text edit | Host mutates text mid-edit | live content | Host projection keep |
| selectedAnnotationIndex field | short-life layout cache | ResolveSelectedIndex / clamp | field delete later |
| editingTextIndex field | short-life layout cache | ResolveTextEditingIndex | field delete later |
| AnnotationRenderContext | free draw helpers only | S-F done | Stage3 |

**Document is sole store** for identity/order/style/committed geometry. Host vector is **projection runtime cache** (not dual store) after S-E-47 rebuild sole.

## Research §11.5 package exit status

| Criterion | Status after S-E-52 |
|---|---|
| Document holds items | **PASS** |
| active/selected stable id | **PASS** |
| Document add/remove/replace/find/order | **PASS** |
| History commands act on Document | **PASS** |
| GDI draw Document product-read | **PASS** |
| vector index short-life only | **PASS** partial (product dual 0; fields remain) |
| selected index API delete | **PASS** partial (product dual 0; field remains) |
| Delete `m_screenshotAnnotations` | **blocked** — live-drag projection cache |

**Full S-E package exit (§11.5): NOT closed.**  
**Host-vector exit Phase A/B/C1/C2/C3c-3: DONE.**

## Recommended next (合域强制; Stage2 ~97 / 警戒 100 / 硬停 120)

1. **Rename `m_screenshotAnnotations` → `m_annotationProjection`** — ownership naming cutover; documents projection sole (not dual store). High value; mechanical.
2. **Host-vector member delete** — requires live-drag projection redesign; multi-slice.
3. **§11.5 package exit PARTIAL** — declare Document sole store + Host projection cache legitimate residual.

Default: **(1) rename to m_annotationProjection** as next WIP — one ownership domain, net dual-store naming delete.

## KPI

| Metric | After S-E-52 residual |
|---|---:|
| hermetic | **60/60** |
| product SelectedAnnotationIndex dual | **0** |
| product TextEditingIndex reads | **0** |
| textEditingId sole | **on** |
| Host vector sites | **~141** |
| Stage 2 code commits | **~97** (ADR-003 警戒 100 / 硬停 120) |
| §11.5 package exit | **NOT closed** |

## NEXT

Docs pin residual inventory. Then rename `m_screenshotAnnotations` → `m_annotationProjection` under 合域强制. **Must prefill domain list before `src/` edits.**
