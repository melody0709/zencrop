# S-E-CLOSE-8 evidence: pending-create projection seed dual-write delete

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-8  
Prior: S-G residual inventory `67ca5a73`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** pending-create text commit projection seed dual-write.  
Delete last remaining `m_annotationProjection[i] = ann` field write before Document replace.  
Sole: local `ann` (draft/read) → Document ReplaceFromLegacy + CommitCreateSnapshot → Clear session → RebuildHostProjection.

Not helper-only: last projection field dual-assign write residual deleted (grep `m_annotationProjection[.*] =` → 0).

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| pendingCreate: `projection[i] = ann` seed then Document replace | Document-first replace from local ann + rebuild |

## Residual after this knife

1. **`m_annotationProjection` member** remains as rebuild cache (read/layout/hit Host index)
2. No field dual-assign write residual
3. Projection mutates only via `RebuildHostProjection` / Document-first project APIs

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.cpp` — pending-create commit path

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
rg "m_annotationProjection\\[.*\\]\\s*=" src → 0 matches
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| projection field dual-assign write | **0** |
| commit dual-assign residual | **0** (CLOSE-6..8) |
| `m_annotationProjection` member | residual rebuild cache only |
| Stage 2 code commits | **~131** (user override 硬停 120) |

## Granularity note

One domain: pending-create projection seed dual-write delete.  
Member delete still multi-slice. Next: residual inventory **or** S-A residual **or** projection member ephemeral redesign. Docs same commit. No pin.

## NEXT

S-E residual inventory post CLOSE-8 **or** S-A residual golden/P95 **or** projection member ephemeral. Prefer high-value only under override budget.
