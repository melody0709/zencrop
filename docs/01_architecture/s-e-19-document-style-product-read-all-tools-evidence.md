# S-E-19 evidence: Document style product-read all tools (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-19  
Prior: S-E-18 `63d52759`

## Intent

**Ownership domain (single slice):** Expand Document **style product-read** from Geometry/Arrow-only (S-E-18) to **all drawing tools**. `LoadScreenshotStyleFromSelection` prefers Document item props; Host legacy `ann` style path is recovery-only when Document item missing.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Load style from Host `ann` fields when Document item present | Document item props via pure `ApplyStyleFromItem` |
| Geometry/Arrow-only Document style apply | All tools: Geometry/Arrow/BrokenLine/Pencil/Marker/HighLight/Magnifier/Mosaic/AutoMosaic/Eraser/Serial/Text/Watermark |

Host vector remains GDI runtime sole. Recovery Host path kept for Document item missing (seed lag / empty Document).

## Product-read contract

1. `ScreenshotAnnotationDocumentItemToolCommand` — role first (Magnifier/HighLight/Watermark/AutoMosaic), else `AnnotationTypeToToolCommand`
2. `ScreenshotAnnotationDocumentApplyStyleFromItem` — select tool + group memory + color + tool-specific style props from Document
3. `ScreenshotAnnotationDocumentApplyGeometryArrowStyle` — thin wrapper (Geometry/Arrow only) over full apply
4. Load early-returns after pure apply + Host HSV projection
5. Host residual path only when Document item missing or apply returns false

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ItemToolCommand + ApplyStyleFromItem pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Load Document-first all tools
- `tests/test_annotation_document_dual_write_contract.cpp` — multi-tool style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Document style product-read all tools | **on** |
| Stage 2 code commits | **~67** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: Document style product-read for all tools + Load product path + tests. Not helper-only (product call site switched). Geometry/Arrow create/edit Document-first still open.

## NEXT

Geometry/Arrow ownership vertical continue (create/edit Document-first) under ADR-002. 合域强制. §11.5 package exit still NOT closed.
