# S-E-9 evidence: Document incremental dual-write cutover (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-9  
Prior: S-E-8 `2615304e`  
**Dual-write TTL: commit 3 of ≤3 — CUTOVER**

## Intent

**Ownership domain (single slice):** Cut over Document dual-write from full rebuild (`SyncFromLegacy` after every mutation) to incremental Document ops (add/remove/replace/clear). Product mutation path no longer full-rebuilds Document; Host vector remains runtime sole for GDI; Document mirrors via incremental dual-write sole. Full rebuild retained only for settings-load recovery.

## Deleted product dual authority

| Legacy product path | Sole after cutover |
|---|---|
| `DocumentSyncFromLegacy` after every create/delete/clear/undo | incremental `DocumentAdd/Remove/Replace/Clear` |
| full rebuild dual-write on mutation | **0** product mutation sites |

## Added Document ops

| API | Role |
|---|---|
| `AnnotationDocument::insertAt` | order-preserving insert |
| `AnnotationDocument::replaceById` | replace item by stable id |
| `ScreenshotAnnotationDocumentAddFromLegacy` | incremental add dual-write |
| `ScreenshotAnnotationDocumentRemoveById` | incremental remove dual-write |
| `ScreenshotAnnotationDocumentReplaceFromLegacy` | incremental replace dual-write |
| `ScreenshotAnnotationDocumentClear` | clear dual-write |

## Residual full rebuild

| Path | Why kept |
|---|---|
| Settings load recovery | re-seed Document from Host vector after settings load (not mutation dual-write) |

## Touch paths

- `src/screenshot/annotation/AnnotationModel.h` / `.cpp` — insertAt / replaceById
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — incremental dual-write helpers; SyncFromLegacy non-product
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — incremental dual-write
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — incremental undo + clear
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — recovery SyncFromLegacy only
- `src/window/OverlayWindow.h` — comment cutover
- `tests/test_annotation_document_dual_write_contract.cpp` — incremental contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product mutation scan for `DocumentSyncFromLegacy`: **0** (Settings recovery only).

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| product mutation full rebuild dual-write | **0** |
| dual-write TTL | **3/3 CUTOVER** |
| Stage 2 code commits | **~48** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: Model insert/replace + incremental dual-write helpers + all product mutation sites + tests. Not per-mutation-site slices.

## NEXT

S-E residual / package exit: Document product reads (active/find) where safe; Host vector still GDI runtime sole until tool-group vertical (S-E/S-F). **Must prefill domain list before src edits.** 合域强制.
