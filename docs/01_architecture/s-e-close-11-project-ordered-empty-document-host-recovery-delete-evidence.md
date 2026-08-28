# S-E-CLOSE-11 evidence: ProjectOrdered empty-Document Host recovery residual delete

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-11  
Prior: S-E-CLOSE-10 `fce2434c`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** ProjectOrdered empty-Document Host recovery residual.  
Delete `if (document.empty()) return hostAnns` dual authority.  
Sole: empty Document → empty ordered vector.  
RebuildHostProjection already clears Host when Document empty (unchanged).  
`hostAnns` param retained for call-site API + HitTestHostIndex id→index mapping only.

Not helper-only: dual empty-Document authority (Host vector vs Document empty) deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ProjectOrdered empty Document → return hostAnns | empty Document → empty ordered |
| Pre-seed Host-only read via ProjectOrdered | Document sole; empty Document = no items |

## Residual after this knife

1. **`m_annotationProjection` member** remains rebuild cache (read/layout/hit Host index)
2. Style/layout product-read Host recovery when Document *item* missing (per-tool FromHost) still ON
3. HitTest empty-id Host fallback still ON (legacy empty-id path)
4. ProjectOrdered Host geom merge residual still **0** (CLOSE-9)
5. preferHostLive residual still **0** (CLOSE-10)

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — empty Document → `{}`
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.cpp` — comment sole
- `tests/test_annotation_document_dual_write_contract.cpp` — invert Host recovery expectations

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| ProjectOrdered empty-Document Host recovery | **0** |
| ProjectOrdered Host geom merge residual | **0** |
| preferHostLive residual | **0** |
| projection field dual-assign write | **0** |
| `m_annotationProjection` member | residual rebuild cache only |
| Stage 2 code commits | **~134** (user override 硬停 120) |

## Granularity note

One domain: ProjectOrdered empty-Document Host recovery residual delete.  
Member delete still multi-slice (Host index APIs). Docs same commit. No pin.

## NEXT

S-E residual inventory post CLOSE-11 **or** projection member ephemeral first slice **or** S-A residual. Prefer high-value only under override budget.
