# S-E package-exit PARTIAL: Document product-read + Document-first infrastructure

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E package-exit PARTIAL (docs only)  
Prior: residual inventory `s-e-residual-inventory-document-first-post-se28-evidence.md` / S-E-28 `f0956931`

## Intent

Declare **S-E Document product-read + Document-first create/history/delete/clear infrastructure DONE** as package-exit **PARTIAL**. Full §11.5 package exit still NOT closed (Host vector GDI sole; selected-index API; typed renderer; Host vector delete).

No `src/` edits. Stage2 **~76** (ADR-002 **over 警戒 70** / 硬停 90).

## Research §11.5 criteria

| Criterion | Status | Evidence |
|---|---|---|
| Document holds items | **partial PASS** | dual-write cutover S-E-7..10; Document-first create S-E-22/23 |
| active/selected stable id | **PASS partial** | S-E-8 dual-write id; S-E-16 resolve product-read |
| Document add/remove/replace/find/order | **API ready + product Document-first** | S-E-9 incremental; S-E-22..28 Document-first |
| History commands act on Document | **PASS partial** | S-E-20/21 snaps product-read; S-E-24..26 apply Document-first |
| vector index short-life only | **partial** | resolve prefers id; recovery index remains |
| selected index API delete | **NOT YET** | Host layout still uses index after resolve |
| Tool-group vertical create/edit | **partial** | create Document-first DONE; live drag Host GDI sole |
| Delete `m_screenshotAnnotations` | **blocked** | last tool group / GDI runtime |
| AnnotationRenderContext / registry | **NOT STARTED** | S-F free helpers only |

## What is DONE (infra package-exit PARTIAL)

1. **Document product-read**
   - Resolve selected by pure id / Document active (S-E-16/17)
   - Style product-read all tools (S-E-18/19)
   - History before/after snapshot product-read (S-E-20/21)
2. **Document-first mutations**
   - Create main + text pending (S-E-22/23)
   - History insert/replace/remove (S-E-24/25/26)
   - Product delete + ClearAllMarks (S-E-27/28)
3. **Document dual-write cutover**
   - TTL 3/3 CUTOVER (S-E-9); modify residual closed (S-E-10)
4. **Pure dual-authority Host methods**
   - S-E-1..15 pure free helpers; pure Host dual methods deleted

## What remains (blocks full §11.5)

1. Host `m_screenshotAnnotations` GDI/hit-test/live-drag/export sole
2. Live drag: Host mutates; DocumentReplace on commit (expected while Host GDI sole)
3. selected-index API delete (index still layout key after resolve)
4. AnnotationRenderContext / typed registry
5. Delete Host vector (last vertical)

## KPI

| Metric | After package-exit PARTIAL |
|---|---:|
| hermetic | **60/60** |
| Document product-read | **on** |
| Document-first create/history/delete/clear | **on** |
| product Host-first create/delete/clear | **0** |
| history apply Document-first | **full on** |
| §11.5 full package exit | **NOT closed** |
| S-E package-exit PARTIAL | **DONE** |
| Stage 2 code commits | **~76** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

Docs-only package-exit PARTIAL. No helper-only code. Net progress = ownership infrastructure closed; remaining = Host vector / renderer vertical under ADR-002.

## NEXT

Geometry/Arrow ownership vertical deepen (only if net dual-authority delete) or Host-vector delete plan under over-警戒 discipline. 合域强制. Ban helper-only. §11.5 full still NOT closed.
