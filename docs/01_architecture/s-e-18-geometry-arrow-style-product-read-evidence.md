# S-E-18 evidence: Geometry/Arrow style product-read from Document (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-18  
Prior: S-E-17 `eb935366`

## Intent

**Ownership domain (single slice):** First Geometry/Arrow **ownership vertical** step — style product-read. Pure helpers resolve Document selected item and apply Geometry/Arrow style props into pure editor state. `LoadScreenshotStyleFromSelection` Geometry/Arrow path no longer treats Host legacy `ann` fields as style authority.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| `LoadScreenshotStyleFromSelection` Geometry/Arrow style from Host `ann` fields | Document active/findById item props via pure apply helper |
| Host `ann.ellipse/filling/lineStyle/penWidth/roundedRadius/arrowShape/color*` for Geometry/Arrow style load | Document `PathMode/Filling/PenStyle/PenWidth/RectRoundRadius/LineShape/Color*/ColorIndex` |

Host vector remains GDI runtime sole; recovery Host path kept for non-Geometry/Arrow tools and when Document item missing.

## Product-read contract

1. `ScreenshotAnnotationDocumentResolveSelectedItem` — pure id → Document findById; else Document activeItem
2. `ScreenshotAnnotationDocumentApplyGeometryArrowStyle` — when item type Geometry/Arrow:
   - select tool + tool-group memory
   - color custom/index/alpha from Document props
   - Geometry: PathMode==3 → ellipse; Filling; PenStyle; PenWidth; RectRoundRadius
   - Arrow: PenStyle; LineShape; PenWidth
3. Host only projects color-picker HSV after pure apply
4. Non Geometry/Arrow → apply returns false; Host residual path unchanged

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveSelectedItem + ApplyGeometryArrowStyle pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — LoadScreenshotStyleFromSelection Geometry/Arrow Document path
- `tests/test_annotation_document_dual_write_contract.cpp` — Geometry/Arrow style product-read contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Geometry/Arrow style product-read | **on** |
| Stage 2 code commits | **~66** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: Geometry/Arrow style product-read helpers + Load product path + tests. Not helper-only (product call site switched). First ownership-vertical step for Geometry/Arrow (create/edit/history Document sole still open).

## NEXT

Geometry/Arrow ownership vertical continue (create/edit Document-first or more tool style product-read) under ADR-002. 合域强制. §11.5 package exit still NOT closed.
