# S-E-8 evidence: selected stable-id dual-write + Document active sync (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-8  
Prior: S-E-7 `73807bf1`  
**Dual-write TTL: commit 2 of ≤3** (seed → deepen → cutover)

## Intent

**Ownership domain (single slice):** Deepen Document dual-write with pure selected stable-id. Add `selectedAnnotationId` to pure editor state; Host select dual-writes index+id; Document active syncs from pure id after select and after vector rebuild. Delete empty-active dual path (call sites always pass pure selected id).

## Added / changed authority

| Surface | Role |
|---|---|
| `ScreenshotEditorState::selectedAnnotationId` | pure selected stable id dual-write |
| `ScreenshotEditorSelectAnnotationById` | index+id sole select |
| `ScreenshotEditorSetAnnotationCountAndSelect(..., id)` | optional id dual-write |
| `ScreenshotAnnotationIdAt` | resolve id at index (EnsureLegacy) |
| `ScreenshotAnnotationSelectInHost` | Host select dual-writes id |
| `ScreenshotAnnotationDocumentSyncActive` | Document active only (select path) |
| `ScreenshotAnnotationSelectInHostAndDocument` | select + Document active |
| DocumentSyncFromLegacy call sites | always pass pure selected id |

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — selectedAnnotationId + select helpers
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — IdAt / SelectInHost / SyncActive / SelectInHostAndDocument
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — SelectInHostAndDocument
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — SelectInHostAndDocument
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — SelectInHostAndDocument + DocumentSync
- `tests/test_annotation_document_dual_write_contract.cpp` — select id + Document active contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| pure selectedAnnotationId | **on** |
| Document active from pure id | **on** |
| dual-write TTL | **2/3** |
| Stage 2 code commits | **~47** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: pure selected id + Host select dual-write + Document active sync + tests. Not three slices.

## NEXT

S-E-9 (TTL 3/3 **must cutover**): product reads Document for selection/active/findById where safe; delete dual-write rebuild where Document sole; or index→id cutover for selection. **Must prefill domain list before src edits.** 合域强制.
