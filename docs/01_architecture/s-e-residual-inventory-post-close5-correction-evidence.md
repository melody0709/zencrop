# S-E residual inventory post S-E-CLOSE-5 (correction)

Date: 2026-07-22  
Package: Stage 2 S-E-CLOSE  
Slice: residual inventory (docs only)  
Prior: S-E-CLOSE-5 `151ad28a`

## Intent

Correct residual map after CLOSE-1..5. **Create mid-draw does NOT dual-write `m_annotationProjection`.**  
Prior inventory overstated create residual. No `src/` edits.

## DONE (S-E EditSession vertical)

| Slice | HEAD | Domain |
|---|---|---|
| S-E-CLOSE-1 | `732d252c` | before-snapshot sole |
| S-E-CLOSE-2 | `bf374012` | live-drag draft |
| S-E-CLOSE-3 | `f0097c47` | text mid-edit draft |
| S-E-CLOSE-4 | `e4a9c7bf` | ApplyStyle via EditSession |
| S-E-CLOSE-5 | `151ad28a` | index fields delete |

**EditSession mid-edit transaction COMPLETE** (milestone).  
**Index long-life fields COMPLETE** (deleted).

## Create/draw residual — CORRECTED

| Path | Authority | Dual? |
|---|---|---|
| Mid-draw geometry | `m_editorState` geometry scratch | **No** |
| Mid-draw freehand | `m_screenshotFreehandPoints` | **No** (scratch path buffer) |
| Mid-draw preview | temporary `preview` ann in render | **No** |
| Create commit (LButtonUp) | Document-first `CreateAndProject` | **No** (Document sole) |

Create residual is **scratch buffers + freehand Host vectors**, not full-vector dual store.  
EditSession `Create` kind is optional cleanup (merge freehand into session draft), not hard dual-authority block.

## Remaining true residuals (blocks §11.5 full)

1. **`m_annotationProjection` member** — projection cache (rebuild after Document-first; read/layout/hit-test Host index). Legitimate cache until member delete redesign (ephemeral per-frame or no Host store).
2. **Freehand / broken-line Host path buffers** — create scratch; optional EditSession Create merge.
3. **AnnotationRenderContext / typed registry** — S-D/S-F-CLOSE (research §11.6).
4. **S-A characterization residual** — baseline/DPI/Preview-Export/P95 if Gate needs.
5. **S-G Toolbar / S-H Host TU** — NOT STARTED.

## Target shape status

| Target | Status |
|---|---|
| Document sole committed model | **ON** for mid-edit + create commit |
| EditSession draft + before + commit | **ON** for mid-edit |
| renderer Document view + draft overlay | **ON** for mid-edit |
| No full mutable second vector authority | **PARTIAL** — member remains as rebuild cache, not mid-edit authority |

## KPI

| Metric | Value |
|---|---:|
| hermetic | **60/60** |
| EditSession mid-edit | **COMPLETE** |
| index fields | **0** |
| create dual-write projection | **0** (corrected) |
| projection member | residual cache |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~103** (硬停 120 final) |

## NEXT

1. **S-D/S-F-CLOSE-1** — `AnnotationRenderContext` typed seed + wire one Preview/Export path  
2. **S-A-CLOSE** if Gate characterization residual blocking  
3. **S-E member delete prep** — ephemeral projection / drop Host store (multi-slice near 硬停)  
4. S-C/S-G-CLOSE → S-H-CLOSE → Gate  

Default next code: **S-D/S-F-CLOSE-1** (research §11.6; high Gate value). Ban create micro-slices unless freehand merge is one clear domain.
