# S-E-CLOSE-13 evidence: dual empty-check + HitTest empty-id Host fallback residual delete

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-13  
Prior: residual inventory post CLOSE-12 `b790e0d9`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** dual empty-check residual + HitTest empty-id Host fallback residual.  
After CLOSE-11, Document empty → empty ordered; Host projection is rebuild cache only.  
Dual empty-check (`document.empty() && projection.empty()` / `!doc.empty() || !proj.empty()`) still treated Host as second empty authority.  
HitTest empty-id fell back to Host vector hit-test (legacy dual).

Sole after:
- Empty early-out / hasAnnotations: **Document.empty() sole**
- HitTest empty hitId: **miss (-1)** — no Host fallback dual

Not helper-only: dual empty authority + empty-id Host hit-test dual deleted.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| AnnotationRender empty early-out Document && projection empty | Document.empty() sole |
| ToolbarInteraction clear-all hasAnnotations Document \|\| projection | Document.empty() sole |
| HitTestHostIndex empty hitId → Host vector HitTestLocal | empty hitId → -1 miss |

## Residual after this knife

1. **`m_annotationProjection` member** remains rebuild cache (read/layout/hit Host index)
2. Host-index APIs (ResolveSelectedIndex / HitTest id→index) still need member
3. Style product-read Host recovery when Document *item* missing still ON
4. Prior residuals (geom merge / preferHostLive / empty-Document recovery / CaptureBefore Ensure) still **0**

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.cpp` — Document sole empty early-out
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.cpp` — Document sole hasAnnotations
- `src/screenshot/ScreenshotAnnotationHelpers.cpp` — empty hitId → -1

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| dual empty-check residual | **0** |
| HitTest empty-id Host fallback residual | **0** |
| CaptureBefore projection Ensure residual | **0** |
| ProjectOrdered Host geom merge residual | **0** |
| preferHostLive residual | **0** |
| empty-Document Host recovery residual | **0** |
| projection field dual-assign write | **0** |
| `m_annotationProjection` member | residual rebuild cache only |
| Stage 2 code commits | **~136** (user override 硬停 120) |

## Granularity note

One domain: dual empty + HitTest empty-id Host fallback residual delete.  
Member delete still multi-slice (Host index APIs). Docs same commit. No pin.

## NEXT

S-E residual inventory post CLOSE-13 **or** projection member ephemeral first slice **or** S-A residual. Prefer high-value only under override budget.
