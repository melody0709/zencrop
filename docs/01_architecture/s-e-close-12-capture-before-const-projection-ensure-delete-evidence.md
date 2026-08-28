# S-E-CLOSE-12 evidence: CaptureBefore const + projection Ensure residual delete

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-12  
Prior: residual inventory post CLOSE-11 `652fe491`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** CaptureBefore / BeginModify seed Host projection Ensure mutation residual.  
`ScreenshotAnnotationDocumentCaptureBeforeSnapshot` took non-const `ScreenshotAnnotation&` and called `EnsureLegacyAnnotationId(ann)` — could write id into Host projection.  
Product sites also called `EnsureLegacyAnnotationId` on `m_annotationProjection[i]` before BeginModify/delete.  

Sole after:
- CaptureBefore: `const ScreenshotAnnotation&` + local copy for Host recovery only
- Product BeginModify/delete seeds: const Host projection read
- `ScreenshotAnnotationIdAt`: const vector read (no Ensure)

Not helper-only: dual projection id-write residual via Ensure deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| CaptureBefore EnsureLegacyAnnotationId(ann) mutates Host | local copy Ensure only; Document product-read by id first |
| Settings ApplyStyle Ensure on projection[selected] | const Host seed |
| DeleteSelected Ensure on projection[selected] | const Host seed |
| BeginModify seed sites non-const Host refs | const Host seed |
| ToolbarInteraction clear-all CaptureBefore non-const | const Host read |
| ScreenshotAnnotationIdAt Ensure on vector element | const id read |

## Residual after this knife

1. **`m_annotationProjection` member** remains rebuild cache (read/layout/hit Host index)
2. Create/replace/modify Document-first APIs still Ensure on **local ann** params (not projection member) — OK
3. Product read residual (~161 refs) still needs Host index APIs for mutation layout
4. ProjectOrdered / preferHostLive / empty-Document recovery residual still **0**

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — CaptureBefore const + IdAt const
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.cpp` — const seed
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.cpp` — const seed sites
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.cpp` — const clear-all read

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
rg "EnsureLegacyAnnotationId\\(m_annotationProjection" src → 0
rg "auto&\\s+\\w+\\s*=\\s*m_annotationProjection" src → 0
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| CaptureBefore projection Ensure residual | **0** |
| product Ensure on projection for seed | **0** |
| ProjectOrdered Host geom merge residual | **0** |
| preferHostLive residual | **0** |
| empty-Document Host recovery residual | **0** |
| projection field dual-assign write | **0** |
| `m_annotationProjection` member | residual rebuild cache only |
| Stage 2 code commits | **~135** (user override 硬停 120) |

## Granularity note

One domain: CaptureBefore / seed Ensure mutation of Host projection residual delete.  
Member delete still multi-slice (Host index APIs). Docs same commit. No pin.

## NEXT

S-E residual inventory post CLOSE-12 **or** projection member ephemeral first slice **or** S-A residual. Prefer high-value only under override budget.
