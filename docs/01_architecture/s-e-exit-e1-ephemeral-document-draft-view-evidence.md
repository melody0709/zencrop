# S-E-EXIT E1 evidence: ephemeral Document+draft view; Render/Export/Hit projection consumers deleted

Date: 2026-07-23  
Package: Stage 2 **S-E-EXIT**  
Slice: **E1** (1/3 code commits)  
Prior PLAN tighten: `e12bf5eb`  
Code prior: `ca8e68ce`  
**User override 硬停 120** + **PLAN tighten S-E-EXIT hard**.

## Intent

**Ownership domain:** ephemeral Document + optional EditSession draft view for Render / Export / Hit.  
Same commit **delete** Host projection consumers in those three TUs.  
Not helper-only: product paths no longer read `m_annotationProjection`.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| AnnotationRender `ProjectOrdered(..., m_annotationProjection, ...)` | `ProjectOrdered(document, liveDragId, liveDraft)` |
| AnnotationRender selectedId/textEditingId recovery from `projection[i]` | pure EditorState id + Document `activeItem` |
| AnnotationRender liveDragId recovery via `ResolveSelectedIndex` + projection | pure id / EditSession only |
| Export `ProjectOrdered(..., m_annotationProjection)` | `ProjectOrdered(document)` |
| HitTest `ResolveSelectedIndex` + `projection[selected]` liveAnn | pure selectedId + draft or `TryLegacyById` |
| HitTest body via `HitTestHostIndex(..., projection)` | ephemeral ordered + id compare |

## Landed (not seed-only)

| API | Role |
|---|---|
| `ScreenshotAnnotationDocumentProjectOrdered(document, liveDragId, liveDraft)` | Document-only ephemeral ordered view |
| `ScreenshotAnnotationDocumentTryLegacyById` | Document product-read → Host-shaped ann by id |
| Compatibility overload `(document, hostAnns, ...)` | hostAnns ignored; kept until E2/E3 Host-index exit |

## Residual after E1

1. **`m_annotationProjection` member** still on OverlayWindow（Edit/Settings/Toolbar rebuild+index）
2. **`RebuildHostProjection`** still residual
3. **Host-index APIs**（ResolveSelectedIndex / HitTestHostIndex / BeginModify seed from projection）→ **E2**
4. Member + rebuild pipeline delete → **E3**

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — Document-only ProjectOrdered + TryLegacyById
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.cpp` — projection consumers **0**
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.cpp` — projection consumers **0**
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.cpp` — projection consumers **0**
- `tests/test_annotation_document_dual_write_contract.cpp` — E1 contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
rg m_annotationProjection OverlayWindowScreenshot.AnnotationRender.cpp → 0
rg m_annotationProjection OverlayWindowScreenshot.Export.cpp → 0
rg m_annotationProjection OverlayWindowScreenshot.AnnotationHitTest.cpp → 0
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| Render/Export/Hit `m_annotationProjection` refs | **0** |
| `m_annotationProjection` member | residual（E3） |
| `RebuildHostProjection` | residual（E3） |
| S-E-EXIT code commits | **1 / 3** |
| Stage2 code commits (approx) | **~137** |

## Granularity note

One domain: Render/Export/Hit ephemeral view + delete projection consumers.  
E2/E3 still required for §11.5 full. Docs same commit. No pin. No package switch.

## NEXT

**S-E-EXIT E2:** selection / text-edit / BeginModify → stable id + Document product-read；delete Host-index API old paths same commit.  
Then **E3:** delete member + RebuildHostProjection + all rebuild calls.  
E3 后 `rg m_annotationProjection` / `rg RebuildHostProjection` 必须 **0** 否则硬停复审.
