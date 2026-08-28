# S-E-CLOSE-10 evidence: preferAnnLayout draft-as-ann sole

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-10  
Prior: S-E-CLOSE-9 `b43752cc`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** mid-edit geometry layout authority residual.  
Rename `preferHostLive` → `preferAnnLayout` (no longer Host projection authority).  
Product sites pass **draft-as-ann** mid-edit; `preferAnnLayout=true` only when draft present.  
Delete Settings mid-drag path that used Host projection + preferHostLive (stale geometry residual).

Not helper-only: Settings mid-drag dual (projection geom vs draft geom) deleted; name authority corrected.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Settings selected handle hit-test: projection[i] + preferHostLive | draft-as-ann when EditSession has draft; else projection read |
| HitTest preferHostLive when mid-edit without draft | preferAnnLayout only when hasLiveDraft |
| Render projectAnn preferHostLive = selected && liveEdit | preferAnnLayout only when ordered item is live draft |
| `preferHostLive` name (Host authority implication) | `preferAnnLayout` (ann layout fields; draft-as-ann) |

## Residual after this knife

1. **`m_annotationProjection` member** remains rebuild cache (read/layout/hit Host index)
2. Empty-Document Host recovery (pre-seed) still ON
3. ProjectOrdered Host geom merge residual still **0** (CLOSE-9)
4. Projection field dual-assign write still **0**

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — preferHostLive → preferAnnLayout
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.cpp` — draft-gated preferAnnLayout
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.cpp` — draft-as-ann mid-drag
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.cpp` — draft-gated preferAnnLayout
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.cpp` — comment rename
- `tests/test_annotation_document_dual_write_contract.cpp` — preferAnnLayout contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
rg preferHostLive src → 0
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| preferHostLive residual | **0** |
| preferAnnLayout draft-as-ann | **ON** |
| ProjectOrdered Host geom merge residual | **0** |
| projection field dual-assign write | **0** |
| `m_annotationProjection` member | residual rebuild cache only |
| Stage 2 code commits | **~133** (user override 硬停 120) |

## Granularity note

One domain: mid-edit layout authority (preferHostLive residual + Settings projection-as-live).  
Member delete still multi-slice. Docs same commit. No pin.

## NEXT

S-E-CLOSE-11 projection member ephemeral first slice **or** S-A residual. Prefer high-value only under override budget.
