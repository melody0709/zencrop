# S-E-CLOSE-9 evidence: ProjectOrdered Host geometry merge residual delete

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-9  
Prior: residual inventory post CLOSE-8 `65b4bae4`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** ProjectOrdered mid-edit Host geometry merge residual.  
After EditSession (CLOSE-2/3), product mid-edit overlay is liveDraft sole.  
Delete `liveDragId` without `liveDraft` Host geom merge (start/end/points/angle/pathMode/ellipse).  
Sole: Document committed geometry; EditSession liveDraft full-replace when present.

Not helper-only: dual mid-edit geometry authority (Host merge vs Document) deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ProjectOrdered liveDragId → Host start/end/points/angle/pathMode/ellipse merge | liveDraft full draft replace only; else Document sole |
| RebuildHostProjection liveDragId Host geom merge (via ProjectOrdered) | Document sole rebuild cache |

## Residual after this knife

1. **`m_annotationProjection` member** remains rebuild cache (read/layout/hit Host index)
2. **preferHostLive** residual on ResolveGeometryLayout product sites (safety when draft-as-ann)
3. Empty-Document Host recovery (pre-seed) still ON
4. Projection field dual-assign write still **0**

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ProjectOrdered Host merge else-branch delete
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.cpp` — comment sole authority
- `tests/test_annotation_document_dual_write_contract.cpp` — invert Host-merge expectations → Document sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| ProjectOrdered Host geom merge residual | **0** |
| EditSession liveDraft sole mid-edit overlay | **ON** |
| projection field dual-assign write | **0** |
| `m_annotationProjection` member | residual rebuild cache only |
| Stage 2 code commits | **~132** (user override 硬停 120) |

## Granularity note

One domain: ProjectOrdered Host geometry merge residual delete.  
Member delete / preferHostLive residual still multi-slice. Docs same commit. No pin.

## NEXT

S-E-CLOSE-10 preferHostLive residual **or** projection member ephemeral **or** S-A residual. Prefer high-value only under override budget.
