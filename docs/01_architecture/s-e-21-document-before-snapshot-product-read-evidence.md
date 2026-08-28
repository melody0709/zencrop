# S-E-21 evidence: Document product-read before-snapshot (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-21  
Prior: S-E-20 `2e2c1e41`

## Intent

**Ownership domain (single slice):** History **before-snapshot** (`m_annotationModifyBefore` / style-apply beforeSnap) product-read from Document by stable id. Host `convertLegacyAnnotation` not authority when Document holds item.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| `m_annotationModifyBefore = convertLegacyAnnotation(...).takeSnapshot()` | `ScreenshotAnnotationDocumentCaptureBeforeSnapshot` |
| Settings ApplyActiveScreenshotStyleToSelection beforeSnap | Document CaptureBeforeSnapshot |
| ToolbarInteraction watermark text modify beforeSnap | Document CaptureBeforeSnapshot |

Host convert remains recovery inside CaptureBeforeSnapshot when Document item missing.

## Product-read contract

1. `ScreenshotAnnotationDocumentCaptureBeforeSnapshot(document, ann, index)`  
   - EnsureLegacyAnnotationId  
   - Prefer TakeSnapshotById  
   - Else recovery convertLegacyAnnotation
2. All modify-start capture sites use CaptureBeforeSnapshot
3. After-snapshot already Document product-read from S-E-20

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — CaptureBeforeSnapshot pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — all before-snapshot sites
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — style apply beforeSnap
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — watermark text before/after

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| before-snapshot Document product-read | **on** |
| Stage 2 code commits | **~69** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: history before-snapshot product-read + product call sites. Not helper-only. Complements S-E-20 after-snapshot. Full history Document-sole closed for create/modify when Document item present.

## NEXT

Geometry/Arrow ownership vertical continue under ADR-002. Stage2 at 警戒 70 after next slice — prefer high-value ownership cutover only. 合域强制. §11.5 package exit still NOT closed.
