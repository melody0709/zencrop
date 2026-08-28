# S-E-53 evidence: Host projection rename C3c-1 (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-53  
Prior: residual inventory post S-E-52 `97f5bc63` / S-E-52 `78625cc9`

## Intent

**Ownership domain (single slice):** Host projection rename C3c-1 — `m_screenshotAnnotations` → `m_annotationProjection`. Net-delete dual-store naming; Document remains sole store; Host vector is GDI/live-drag projection cache (S-E-47 rebuild sole). Not helper-only (product member + all call sites renamed).

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Product name `m_screenshotAnnotations` (dual-store implication) | `m_annotationProjection` (projection sole naming) |
| Comment dual-write Host vector sole | Document sole store; Host projection cache |

Product `m_screenshotAnnotations` sites: **0**.

## Product-read contract

1. Member `m_annotationProjection` — GDI/live-drag projection cache
2. Document `m_annotationDocument` — sole store
3. RebuildHostProjection after Document-first mutations (unchanged S-E-47)

## Touch paths

- `src/window/OverlayWindow.h` — member rename + ownership comment
- `src/window/OverlayWindow.cpp` — residual sites
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl`

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
rg m_screenshotAnnotations product → 0
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| product m_screenshotAnnotations | **0** |
| m_annotationProjection | **on** |
| Stage 2 code commits | **~98** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: Host projection sole naming cutover (member + all product call sites). Not helper-only. Member not deleted (live-drag projection cache residual). Complements S-E-47 projection sole rebuild. §11.5 full still NOT closed (member delete blocked by live-drag).

## NEXT

§11.5 package exit PARTIAL (Document sole store + Host projection cache legitimate residual) or selectedAnnotationIndex/editingTextIndex field delete. 合域强制. Host-vector member full delete still blocked by live-drag.
