# S-E residual inventory post S-E-CLOSE-4

Date: 2026-07-22  
Package: Stage 2 S-E-CLOSE (EditSession / draft transaction)  
Slice: residual inventory (docs only)  
Prior: S-E-CLOSE-4 `e4a9c7bf`

## Intent

Map residual Host projection authority after EditSession mid-edit vertical (CLOSE-1..4).  
Declare **EditSession mid-edit transaction COMPLETE** as milestone (not package exit).  
No `src/` edits. Stage2 **~102** (ADR-003 硬停 120 final).

## DONE (EditSession mid-edit transaction)

| Slice | HEAD | Domain |
|---|---|---|
| S-E-CLOSE-1 | `732d252c` | before-snapshot sole (`m_annotationModifyBefore` deleted) |
| S-E-CLOSE-2 | `bf374012` | live-drag draft (move/resize/rotate) |
| S-E-CLOSE-3 | `f0097c47` | text mid-edit draft (content + style) |
| S-E-CLOSE-4 | `e4a9c7bf` | ApplyStyle via EditSession transaction |

Target shape progress:
- Document = sole **committed** model — **partial ON** (mutations commit via Document)
- AnnotationEditSession = active draft + before + commit/rollback — **ON for mid-edit**
- renderer = Document view + optional draft overlay — **ON for mid-edit**
- full mutable second vector authority — **shrunk** (mid-edit gone; create/draw residual)

## Residual projection authority (blocks full §11.5)

### 1. Create / freehand draw paths (HIGH)

During create/draw, Host still mutates `m_annotationProjection` as the in-flight new annotation:
- freehand pencil/marker/mosaic/eraser path point push
- geometry/arrow drag create (start/end live)
- broken-line multi-point create
- serial/text pending create seed (text now seeds EditSession; pending create still Host-backed)

**Next ownership domain:** EditSession **Create** kind — draft for in-flight create; Document insert only on commit. Multi-tool create transaction = 1–2 result slices.

### 2. Read / rebuild residual (LEGITIMATE until member delete)

| Path | Why residual |
|---|---|
| `RebuildHostProjection` after Document-first mutations | projection cache rebuild |
| `ProjectOrdered` Host recovery when Document empty | pre-seed read |
| Resolve selected/text index layout | short-life Host index from id |
| Hit-test Host index mapping | mutation paths need Host index |
| Export/preview read when no draft | Document → ordered projection |

These are **not dual authority** after mid-edit cutover; they are projection cache / layout.

### 3. selectedAnnotationIndex / editingTextIndex fields (MEDIUM)

Product dual **0** (S-E-48/50/52). Fields remain short-life layout after SelectById / SyncTextEditingById.  
Resolve* still falls back to index when id missing.  
**Next:** field delete + Resolve* id-only + Select* drop index storage (1 domain; multi-test).

### 4. Full member delete `m_annotationProjection` (BLOCKED)

Blocked until create/draw draft sole + all read paths use ProjectOrdered/Document without Host vector storage. Last vertical.

### 5. AnnotationRenderContext / registry (S-F / Stage3)

Not started. S-D/S-F-CLOSE later.

## KPI after inventory

| Metric | Value |
|---|---:|
| hermetic | **60/60** |
| EditSession mid-edit transaction | **COMPLETE** (milestone) |
| create/draw projection residual | **open** |
| index fields residual | **open** |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~102** (硬停 120 final) |

## NEXT (result slices only)

1. **S-E-CLOSE-5** EditSession Create draft (freehand/geometry create) — high value; 1–2 slices
2. **selectedAnnotationIndex / editingTextIndex field delete** — pure state short-life cleanup
3. **S-A-CLOSE** characterization residual if Gate-blocking
4. Then S-D/S-F-CLOSE → S-C/S-G-CLOSE → S-H-CLOSE → Gate

Default next code: **S-E-CLOSE-5 Create draft** (keep EditSession vertical) or **index field delete** if Create too wide for one knife. Ban micro-slices. 合域强制.
