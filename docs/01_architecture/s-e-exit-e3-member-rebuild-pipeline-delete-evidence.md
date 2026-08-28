# S-E-EXIT E3 — Host projection member + rebuild pipeline delete

Date: 2026-07-23  
Slice: **S-E-EXIT E3** (3/3 code commits)  
Prior: E1 `f276e560` / E2 `e6212052` / PLAN tighten `e12bf5eb`

## Ownership domain (single slice)

Delete last dual-authority residual of Stage2 S-E:

1. `OverlayWindow::m_annotationProjection` long-life member
2. `ScreenshotAnnotationDocumentRebuildHostProjection` + all call sites
3. Product consumers that treated Host vector as second committed/layout authority

**Sole after E3:**

```text
Document          = unique committed model
EditSession       = active draft + before + commit/rollback
renderer/hit/edit = Document immutable view + optional single draft overlay
禁止               完整可变第二 vector 权威
```

## Deleted dual authority

| Before | After |
|---|---|
| `OverlayWindow::m_annotationProjection` member | **deleted** |
| `ScreenshotAnnotationDocumentRebuildHostProjection` | **deleted** (body + all call sites) |
| Product `CreateAndProject` / `RemoveAndProject` rebuild Host | product uses `DocumentCreate` / `DocumentRemove` / sole snapshot helpers |
| Product `SelectByIdInHostAndDocument` / ResolveSelectedIndex on Host | product `SelectById(state, doc, id)` + Document/draft reads |
| dual_write test `RebuildHostProjection` corrupt/restore | `anns = ProjectOrdered(doc)` local ephemeral refresh |
| Comment/symbol residual matching hard-stop greps | **0** hits for `m_annotationProjection` and `RebuildHostProjection` in `*.{h,cpp,hpp}` |

## Landed sole APIs (product)

- `ScreenshotAnnotationSelectById(state, doc, id)`
- `ScreenshotAnnotationDocumentCreate` / `CreatePendingText`
- `ScreenshotAnnotationDocumentRemove`
- `ScreenshotAnnotationDocumentInsertFromSnapshotSole` / `ReplaceFromSnapshotSole`
- `ScreenshotAnnotationDocumentResolveLiveAnn` / `TryLegacyById`
- `ScreenshotAnnotationDocumentProjectOrdered(document, liveDragId, liveDraft)` — ephemeral only

## Residual (allowed; not product Host authority)

- Test-only helpers that take a **local** `std::vector<ScreenshotAnnotation>&` and refresh via `ProjectOrdered` (`CreateAndProject` / `RemoveAndProject` / compat hostAnns overload). Not a product member; not a second store.
- `SelectByIdInHostAndDocument` / `ResolveSelectedIndex` remain for tests / short-life layout on local vectors.

## Files (explicit paths)

- `src/window/OverlayWindow.h` — member deleted
- `src/window/OverlayWindow.cpp`
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — rebuild pipeline deleted; forward ProjectOrdered; sole create/remove
- `src/screenshot/annotation/AnnotationEditSession.h` — comment only
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.cpp`
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.cpp`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.cpp`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.cpp`
- `tests/test_annotation_document_dual_write_contract.cpp`
- private historical execution ledger
- this evidence file

## Verification

| Check | Result |
|---|---|
| `build.bat --cmake --stop-running` | **PASS** |
| `ctest -L hermetic` | **64/64** |
| `rg m_annotationProjection` on `*.{h,cpp,hpp}` | **0** |
| `rg RebuildHostProjection` on `*.{h,cpp,hpp}` | **0** |
| net LOC (this slice) | **−96** (568 / 664) |

## Hard-stop outcome

E3 post-condition met: both greps **0**. Package **S-E-EXIT complete**. Next fixed order: **S-A-EXIT** (not CLOSE-14+).

## Not done (out of domain)

- S-A fixed-DPI golden / Preview-Export pixels / P95-GDI
- S-D/S-F registry + shared renderer
- S-C/S-G Catalog/VM/Layout/Renderer/Controller vertical
- S-B/S-H Host oversized methods ownership exit
- Stage2 Gate / family LOC ≤ 30640
