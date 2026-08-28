# S-E-7 evidence: Document dual-write seed (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-7  
Prior: residual recheck `5ffc4606`  
**Dual-write TTL: commit 1 of ≤3** (seed → deepen → cutover)

## Intent

**Ownership domain (single slice):** Seed AnnotationDocument ownership seam. Host holds `AnnotationDocument` (alias of `ScreenshotAnnotationModel`); every Host vector mutation dual-writes Document via `ScreenshotAnnotationDocumentSyncFromLegacy`. Host vector remains runtime sole until vertical cutover. Not helper-only: member + all mutation dual-write wiring + contract tests.

## Added authority

| Surface | Role |
|---|---|
| `using AnnotationDocument = ScreenshotAnnotationModel` | Document ownership seam alias (§11.5) |
| `OverlayWindow::m_annotationDocument` | Host Document dual-write store |
| `ScreenshotAnnotationDocumentSyncFromLegacy` | rebuild Document from legacy vector after mutation |

## Dual-write call sites (Host vector → Document)

| Path | Site |
|---|---|
| undo/redo project | `projectAnnotationMutation` |
| text commit erase empty | AnnotationEdit erase |
| create (generic/watermark) | AnnotationEdit push_back ×2 |
| delete selected | AnnotationEdit erase |
| text create | AnnotationEdit push_back |
| serial create | AnnotationEdit push_back |
| drawing tool create | AnnotationEdit push_back |
| clear all | ToolbarInteraction clear |

## Touch paths

- `src/screenshot/annotation/AnnotationModel.h` — Document alias
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — SyncFromLegacy helper
- `src/window/OverlayWindow.h` — `m_annotationDocument` member
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — dual-write after mutations
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — dual-write clear + projectMutation
- `tests/test_annotation_document_dual_write_contract.cpp` — **new**
- `tests/CMakeLists.txt` — register hermetic

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Host Document member | **1** (dual-write seed) |
| dual-write TTL | **1/3** |
| Stage 2 code commits | **~46** (over 警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: Document alias + Host member + all mutation dual-write + tests. Not member-only / sync-only / test-only three slices.

## NEXT

S-E-8 (TTL 2/3): deepen Document dual-write — product reads from Document (findById / active / count) where safe; or stable-id selection dual-write with Document. **Must cutover or delete dual-write by TTL 3.** 合域强制. **Must prefill domain list before src edits.**
