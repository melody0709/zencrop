# S-E-CLOSE-7 evidence: watermark content Document-first

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-7  
Prior: S-E-CLOSE-6 `84dc95b3`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** Toolbar watermark content path dual-write onto projection.  
Delete `m_annotationProjection[i].text = text` then Document replace dual.  
Sole: local copy → Document CommitModify/ReplaceFromLegacy → RebuildHostProjection.

Not helper-only: projection field dual-assign authority on watermark content deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ConfigWatermarkContent: `ann = projection[i]; ann.text=…; CommitModify(ann)` via projection ref | local `ann` copy → CommitModify → RebuildHostProjection |
| ConfigWatermarkContent create/update: `projection[i].text = text; ReplaceFromLegacy(projection[i])` | local copy → ReplaceFromLegacy → rebuild |

## Residual

1. Pending-create layout seed assign in AnnotationEdit (create path; rebuild follows) — documented S-E-CLOSE-6
2. Projection member remains rebuild cache (read/layout/hit Host index)
3. `m_annotationProjection` field not deleted

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.cpp` — ConfigWatermarkContent

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| watermark content projection dual-assign | **0** |
| live-drag/ApplyStyle/text-modify commit dual-assign | **0** (CLOSE-6) |
| projection field mutate (write) residual | pending-create seed only |
| `m_annotationProjection` member | residual rebuild cache |
| Stage 2 code commits | **~124** (user override 硬停 120) |

## Granularity note

One domain: watermark content Document-first. Next: residual inventory of projection mutates **or** S-A/S-G. Docs same commit. No pin.

## NEXT

Projection residual inventory (docs) **or** S-A characterization nails **or** S-C/S-G. Prefer high-value only under override budget.
