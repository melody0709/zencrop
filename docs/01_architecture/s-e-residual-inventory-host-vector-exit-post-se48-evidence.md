# S-E-49 residual inventory: Host-first mutate + Host-vector delete prep (post S-E-48)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: residual inventory Phase C3 (docs only)  
Prior: S-E-48 `261ae690`

## Intent

Inventory residual dual authority after Phase A/B/C1/C2 (geometry product-read, Document-order iterate/hit-test, Host projection sole rebuild, select-by-id sole). No `src/` edits. Stage2 **~95** (ADR-003 警戒 100 / 硬停 120).

## Closed this arc (S-E-43..48 Host-vector exit)

| Domain | Status | Evidence |
|---|---|---|
| Geometry product-read export | **DONE** | S-E-43 |
| Geometry product-read preview/hit-test | **DONE** | S-E-44 |
| Document-order iterate export/preview | **DONE** | S-E-45 |
| Document-order hit-test | **DONE** | S-E-46 |
| Host projection sole rebuild | **DONE** | S-E-47 |
| Product select-by-id sole | **DONE** | S-E-48 |
| GDI product-read tools 12/12 | **DONE** | S-E-32..41 |
| Document-first create/history/delete/clear | **DONE** | S-E-16..31 |

## Residual dual authority / seams

| Category | Residual | Why still Host | Next owner |
|---|---|---|---|
| Live drag mutate | Host mutates `ann.start/end/points/angle` during move/rotate/resize; Document via CommitModify on mouse-up | GDI live sole; Document stale mid-drag | Host keep until live geometry Document-first (expensive) or accept Host projection mutate + Document commit |
| Text edit live | Host mutates `ann.text` during text edit; Document on commit | live text content | Host keep mid-edit |
| `selectedAnnotationIndex` field | still in ScreenshotEditorState; short-life layout after SelectById | ResolveSelectedIndex / clamp / history mut index | gradual field delete after all product paths id-only |
| `SelectInHostAndDocument(index)` helper | remains for tests / residual short-life layout | lower-level | delete after field delete |
| Host `m_screenshotAnnotations` member | ~141 sites | GDI projection cache + live-drag mutate + mutation index map | Host-vector member delete last |
| AnnotationRenderContext / registry | free draw helpers only | S-F shared-draw done | Stage3 candidate |
| Empty-check early out | `m_screenshotAnnotations.empty()` in render | Host projection cache | Document.empty() sole later |

Live product scan: export/preview/hit-test/create/remove/insert/replace/clear/select — Document sole when item present. Host residual = live-drag geometry mutate + index field + member vector as projection cache.

## Research §11.5 package exit status

| Criterion | Status after S-E-48 |
|---|---|
| Document holds items | **PASS** |
| active/selected stable id | **PASS** (select-by-id sole) |
| Document add/remove/replace/find/order | **PASS** + product Document-first + rebuild sole |
| History commands act on Document | **PASS** partial (Document-first + rebuild) |
| GDI draw Document product-read | **PASS** — 12/12 tools + geometry layout |
| vector index short-life only | **partial** — product select-by-id; index field remains |
| selected index API delete | **partial** — product paths id sole; field not deleted |
| Delete `m_screenshotAnnotations` | **blocked** — live-drag + projection cache |

**Full S-E package exit (§11.5): NOT closed.**  
**Host-vector exit Phase A/B/C1/C2: DONE.**

## Host-vector member delete exit path (remaining)

### C3a — selectedAnnotationIndex field delete (1–2 commits)

1. Replace residual `ScreenshotEditorSelectedAnnotationIndex` product reads with ResolveSelectedIndex / FindIndexById(selectedId).
2. Delete or demote `selectedAnnotationIndex` field to pure short-life cache recomputed from id (or delete field entirely if all resolve paths id-only).
3. Delete `SelectInHostAndDocument(index)` if unused.

### C3b — live-drag Document commit deepen (optional; high risk)

1. Option: keep Host mutate + CommitModify on mouse-up (current; acceptable for §11.5 if Host is pure projection cache).
2. Option: dual-write geometry to Document during drag (perf/cost; not required if projection sole accepted).

### C3c — Delete `m_screenshotAnnotations` (1–3 commits)

1. Host holds only projection rebuilt from Document; live-drag mutates projection then CommitModify.
2. Replace all `m_screenshotAnnotations` with ProjectOrdered local or member projection rebuilt each frame / after mutation.
3. Delete member; OverlayWindow.h residual 0 for annotation vector.
4. §11.5 package exit evidence.

## Recommended next domains (合域强制; Stage2 ~95 / 警戒 100 / 硬停 120)

1. **selectedAnnotationIndex residual product-read delete** — net dual-authority delete; high value near §11.5.
2. **Host-vector member delete** — last vertical; may need 2–3 commits under budget.
3. **§11.5 package exit evidence** — docs after member delete.

Default under over-警戒 near 硬停: **(1) selectedAnnotationIndex residual product-read delete** as next WIP — one ownership domain, net dual-authority delete.

## KPI

| Metric | After S-E-48 residual |
|---|---:|
| hermetic | **60/60** |
| GDI product-read tools | **12/12** |
| Host projection sole | **on** |
| select-by-id sole | **on** |
| Host vector sites | **~141** (projection + live-drag + layout) |
| Stage 2 code commits | **~95** (ADR-003 警戒 100 / 硬停 120) |
| §11.5 package exit | **NOT closed** |

## NEXT

Docs pin residual inventory. Then selectedAnnotationIndex residual product-read delete under 合域强制 (警戒 100 / 硬停 120 — one high-value domain). **Must prefill domain list before `src/` edits.**
