# S-E-16 evidence: Document product-read resolve selected (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-16  
Prior: S-F residual inventory `1a9a20cd`

## Intent

**Ownership domain (single slice):** First Document **product-read** path. Pure free helper `ScreenshotAnnotationResolveSelectedIndex` resolves Host layout index from pure `selectedAnnotationId` and Document `activeItem` product-read. Net-delete index-only selection authority at product selected-resolve paths (style apply/load, delete, move/rotate/resize, LButton selected-handle).

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| `ScreenshotEditorSelectedAnnotationIndex` as selection authority for product resolve | pure id + Document active product-read via `ScreenshotAnnotationResolveSelectedIndex` |
| index-only selected access in Apply/Load style, Delete, move/rotate/resize, selected-handle | ResolveSelectedIndex |

Index remains short-life layout key after resolve; not selection authority.

## Product-read contract

1. Prefer pure `selectedAnnotationId`
2. Else product-read Document `activeItem()->id()`
3. Else recovery pure index when in-range (legacy empty-id annotations)
4. Else `-1`

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — pure sole resolve helper
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — Apply/Load style product-read
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — Delete, move/rotate/resize, selected-handle product-read
- `tests/test_annotation_document_dual_write_contract.cpp` — resolve contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Document product-read (selected resolve) | **on** |
| Stage 2 code commits | **~64** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: pure resolve helper + product selected-resolve paths + tests. Not helper-only (product call sites switched).

## NEXT

Continue Document product-read deepen (more selected paths / style property reads from Document) or Geometry/Arrow ownership vertical under ADR-002. 合域强制.
