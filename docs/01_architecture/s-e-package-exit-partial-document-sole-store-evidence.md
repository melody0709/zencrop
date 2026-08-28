# S-E package-exit PARTIAL: Document sole store + Host projection cache residual

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group + Host-vector exit vertical  
Slice: S-E package-exit PARTIAL (docs only)  
Prior: S-E-53 `849ff93e` / residual inventory post-se52 `97f5bc63`

## Intent

Declare **S-E Document sole store + Host projection cache residual DONE** as package-exit **PARTIAL**. Full §11.5 package exit still NOT closed (`m_annotationProjection` member remains for live-drag GDI cache; selectedAnnotationIndex/editingTextIndex fields short-life residual).

No `src/` edits. Stage2 **~98** (ADR-003 警戒 100 / 硬停 120).

## Research §11.5 criteria

| Criterion | Status | Evidence |
|---|---|---|
| Document holds items | **PASS** | dual-write cutover S-E-7..10; Document-first create S-E-22/23; rebuild sole S-E-47 |
| active/selected stable id | **PASS** | S-E-8 dual-write id; S-E-16 resolve product-read; S-E-48 select-by-id sole |
| Document add/remove/replace/find/order | **PASS** | S-E-9 incremental; S-E-22..28 Document-first; S-E-47 rebuild sole |
| History commands act on Document | **PASS** | S-E-20/21 snaps product-read; S-E-24..26 apply Document-first; S-E-47 rebuild |
| GDI draw Document product-read | **PASS** | S-E-32..41 style 12/12; S-E-43/44 geometry layout |
| Document-order iterate | **PASS** | S-E-45 export/preview; S-E-46 hit-test |
| vector index short-life only | **PASS partial** | product dual 0 (S-E-50); fields remain short-life |
| selected index API delete | **PASS partial** | product dual 0 (S-E-48/50); field remains short-life |
| text-edit id sole | **PASS** | S-E-52 editingTextId sole |
| Delete Host annotation vector member | **NOT YET** | `m_annotationProjection` live-drag GDI cache residual |
| AnnotationRenderContext / registry | **NOT STARTED** | S-F free helpers only; Stage3 |

## What is DONE (package-exit PARTIAL)

1. **Document sole store**
   - Document-first create/history/delete/clear (S-E-16..31)
   - Host projection rebuild sole after Document-first mutations (S-E-47)
   - product convertLegacyAnnotation Overlay*.inl **0**
2. **Document product-read**
   - Resolve selected by pure id / Document active (S-E-16/17)
   - Style product-read all tools 12/12 (S-E-18/19 + S-E-32..41)
   - Geometry layout product-read export/preview/hit-test (S-E-43/44)
   - Document-order iterate export/preview/hit-test (S-E-45/46)
3. **Selection / text-edit id sole**
   - SelectById product sole (S-E-48)
   - product SelectedAnnotationIndex dual **0** (S-E-50)
   - textEditingId sole + Document.empty() early-out (S-E-52)
4. **Host projection naming**
   - `m_screenshotAnnotations` → `m_annotationProjection` (S-E-53)
   - Document dual-store naming **0**
5. **Document dual-write cutover**
   - TTL 3/3 CUTOVER (S-E-9); modify residual closed (S-E-10)
6. **Pure dual-authority Host methods**
   - S-E-1..15 pure free helpers; pure Host dual methods deleted

## What remains (blocks full §11.5)

1. Host `m_annotationProjection` GDI/live-drag/text-edit mutable projection cache
2. Live drag: Host mutates projection; DocumentReplace on CommitModify (expected while projection cache)
3. selectedAnnotationIndex / editingTextIndex fields (short-life layout residual; product dual 0)
4. AnnotationRenderContext / typed registry (Stage3)
5. Delete `m_annotationProjection` member (last vertical; live-drag redesign)

## Legitimate residual (not dual authority)

| Residual | Why legitimate |
|---|---|
| `m_annotationProjection` | Projection cache of Document; rebuilt after Document-first mutations; live-drag mutates then CommitModify |
| selectedAnnotationIndex field | Short-life layout after SelectById; product dual 0 |
| editingTextIndex field | Short-life layout after SyncTextEditingById; product dual 0 |
| Live-drag Host mutate | Projection cache runtime; Document sole after commit |

## KPI

| Metric | After package-exit PARTIAL |
|---|---:|
| hermetic | **60/60** |
| Document sole store | **on** |
| Host projection sole naming | **on** |
| product m_screenshotAnnotations | **0** |
| product SelectedAnnotationIndex dual | **0** |
| product TextEditingIndex reads | **0** |
| GDI product-read tools | **12/12** |
| §11.5 full package exit | **NOT closed** |
| S-E package-exit PARTIAL | **DONE** |
| Stage 2 code commits | **~98** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

Docs-only package-exit PARTIAL. No helper-only code. Net progress = Document sole store + Host projection cache ownership closed as PARTIAL; remaining = member delete / field delete under ADR-003 near 警戒 100.

## NEXT

1. selectedAnnotationIndex / editingTextIndex field delete (pure state short-life cleanup)
2. Host-vector member delete (live-drag projection redesign)
3. §11.5 full package exit evidence
4. AnnotationRenderContext / Stage3

Default under over-警戒 near 硬停: field delete or residual inventory only; ban helper-only. 合域强制. §11.5 full still NOT closed.
