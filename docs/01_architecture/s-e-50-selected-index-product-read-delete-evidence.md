# S-E-50 evidence: selectedAnnotationIndex residual product-read delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-50  
Prior: S-E-49 `8b7d7647`

## Intent

**Ownership domain (single slice):** selectedAnnotationIndex residual **product-read dual delete**. Product Overlay*.inl no longer reads `ScreenshotEditorSelectedAnnotationIndex` as selection authority. Selection presence via `HasSelection` (id first); layout index via `ResolveSelectedIndex`. Index remains short-life layout cache only.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Product `SelectedAnnotationIndex >= 0` presence checks | `HasSelection` (id sole; index recovery) |
| Product create-history layout index from SelectedAnnotationIndex | `ResolveSelectedIndex` |
| Product removeAndProject selectedIndex arg | `ResolveSelectedIndex` |
| Product return SelectedAnnotationIndex after create | `ResolveSelectedIndex` |
| HasSelection index-only | HasSelection prefers selectedAnnotationId |

Product Overlay*.inl `ScreenshotEditorSelectedAnnotationIndex` sites: **0**.

## Product-read contract

1. `ScreenshotEditorHasSelection` — prefer non-empty selectedAnnotationId; recovery via index when id empty
2. Product create/history/remove/clear-presence paths use ResolveSelectedIndex / HasSelection / selectedId
3. Index field remains for short-life layout cache + pure SelectInHost clamp

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — HasSelection id-first
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — residual SelectedAnnotationIndex product-read **0**
- `tests/test_annotation_document_dual_write_contract.cpp` — HasSelection id-first contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| product Overlay*.inl SelectedAnnotationIndex | **0** |
| HasSelection id-first | **on** |
| select-by-id sole | **on** |
| Stage 2 code commits | **~96** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: residual product-read dual delete for selectedAnnotationIndex + HasSelection id-first + tests. Not helper-only (product sites switched). Field not deleted (short-life layout residual). Complements S-E-48 select-by-id sole.

## NEXT

Host-vector member delete prep or selectedAnnotationIndex field delete (pure state). Live-drag Host mutate residual intentional. 合域强制. §11.5 full still NOT closed.
