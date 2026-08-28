# S-E-EXIT E2 evidence: BeginModify seed + pure selected/text-edit read → id + Document/draft

Date: 2026-07-23  
Package: Stage 2 **S-E-EXIT**  
Slice: **E2** (2/3 code commits)  
Prior: S-E-EXIT E1 `f276e560`  
**User override 硬停 120** + **PLAN tighten S-E-EXIT hard**.

## Intent

**Ownership domain:** BeginModify seed + pure selected/text-edit read.  
Same commit **delete** projection seed consumers for EditSession BeginModify and text mid-edit recovery.  
Sole: pure id + `ResolveLiveAnn` (draft prefer / Document product-read).

Not helper-only: product BeginModify no longer seeds from `m_annotationProjection[i]` as primary path.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Settings ApplyStyle BeginModify from `projection[selected]` | pure selectedId + ResolveLiveAnn |
| Text KeyDown/Char recovery seed from `projection[editingIdx]` | pure text-editing id + ResolveLiveAnn |
| Geometry selected handle BeginModify from `projection[selected]` | Document/draft seed local |
| HitAnnotation drag/text BeginModify from `projection[hit]` | hitId + Document/draft seed local |
| Pending text create BeginModify from `projection[textIdx]` | local `textAnn` after CreatePending |

## Landed

| API | Role |
|---|---|
| `ScreenshotAnnotationDocumentResolveLiveAnn` | draft prefer + Document TryLegacyById |

## Residual after E2（→ E3）

1. **`m_annotationProjection` member** still on OverlayWindow  
2. **`RebuildHostProjection`** + all rebuild call sites  
3. Host-index residual for mutation layout（RemoveAndProject / HitTestHostIndex / FindIndexById after create）  
4. Some pure type/text reads still from projection when Host index residual required  
5. Projection recovery fallback when pure id empty（short-life until E3）

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveLiveAnn  
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.cpp` — ApplyStyle seed  
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.cpp` — BeginModify seeds  
- `tests/test_annotation_document_dual_write_contract.cpp` — E2 contract  

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
test_annotation_document_dual_write_contract → Passed
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| BeginModify primary seed from Document/draft | **ON** |
| Render/Export/Hit projection refs | **0** (E1) |
| `m_annotationProjection` member | residual（E3） |
| `RebuildHostProjection` | residual（E3） |
| S-E-EXIT code commits | **2 / 3** |
| Stage2 code commits (approx) | **~138** |

## Granularity note

One domain: BeginModify/seed + pure selected/text-edit read.  
E3 must delete member + rebuild pipeline. Docs same commit. No pin. No package switch.

## NEXT

**S-E-EXIT E3:** delete `m_annotationProjection` + `RebuildHostProjection` + all rebuild calls + dead `hostAnns` param.  
E3 后 `rg m_annotationProjection` / `rg RebuildHostProjection` 必须 **0** 否则硬停复审.
