# S-E-52 evidence: textEditingId sole + Document.empty() early-out (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-52  
Prior: S-E-51 `5fda075e`

## Intent

**Ownership domain (single slice):** textEditingId sole + Document.empty() render/clear early-out (C3c-3). Text-edit presence/layout prefers pure `editingTextId`; Host index short-life only. Empty-check prefers Document sole. Net-delete product text-edit Host index dual + Host-only empty dual.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Product text-edit read via TextEditingIndex | `ResolveTextEditingIndex` (id sole) |
| Product start text-edit via SyncTextEditingIndex(index) | `SyncTextEditingById(index, id)` |
| IsEditingText index-only | IsEditingText prefers editingTextId |
| Render empty Host-only early-out | Document.empty() first |
| ClearAllMarks hasAnnotations Host-first | Document.empty() first |

Product Overlay*.inl `ScreenshotEditorTextEditingIndex` reads: **0** (set clear `-1` remains; clears id too).

## Product-read contract

1. `editingTextId` field + `SyncTextEditingById` / `TextEditingId` / `IsEditingText` id-first
2. `ScreenshotAnnotationResolveTextEditingIndex` — id → Host layout index; recovery index
3. Product text-edit set/read/start/clear paths use id sole
4. Document.empty() early-out for render + ClearAllMarks

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — editingTextId + SyncById + IsEditingText id-first
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ResolveTextEditingIndex
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — text-edit product paths
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — empty-out + textEditingId
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — Resolve reads + ClearAllMarks + mut re-sync
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — ResolveTextEditingIndex
- `tests/test_annotation_document_dual_write_contract.cpp` — textEditingId contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| product Overlay*.inl TextEditingIndex reads | **0** |
| textEditingId sole | **on** |
| Document.empty() early-out | **on** |
| Stage 2 code commits | **~97** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: textEditingId sole + Document.empty() early-out + product paths + tests. Not helper-only (product sites switched). Complements S-E-48/50 select-by-id. Unblocks Host-vector member delete (C3c-4).

## NEXT

Host-vector member delete (C3c-4) under ADR-003. Live-drag Host mutate residual intentional. 合域强制. §11.5 full still NOT closed.
