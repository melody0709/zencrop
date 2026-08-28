# S-E-CLOSE-5 evidence: selectedAnnotationIndex / editingTextIndex field delete

Date: 2026-07-22  
Package: Stage 2 S-E-CLOSE (EditSession / draft transaction)  
Slice: S-E-CLOSE-5  
Prior: residual inventory post CLOSE-4 `d143b7aa` / S-E-CLOSE-4 `e4a9c7bf`

## Intent

**Ownership domain (single slice):** pure state index fields delete.  
Delete `ScreenshotEditorState::selectedAnnotationIndex` and `editingTextIndex`.  
Selection / text-edit authority = id sole. Layout index only via `FindIndexById` / `Resolve*Index` (short-life; not stored).

Not helper-only: pure fields deleted + Resolve* index fallback deleted + SelectInHost id sole + tests rewired.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| `selectedAnnotationIndex` field | `selectedAnnotationId` only |
| `editingTextIndex` field | `editingTextId` only |
| ResolveSelectedIndex pure index fallback | id / Document active → FindIndexById only |
| ResolveTextEditingIndex pure index fallback | editingTextId → FindIndexById only |
| HasSelection index recovery | non-empty selectedAnnotationId only |
| IsEditingText index recovery | non-empty editingTextId only |

## Product-read / write contract

1. Select: `SelectAnnotationById(state, layoutHint, id)` — index not stored; id sole  
2. Resolve selected layout: id / Document active → `FindIndexById`  
3. Text-edit: `SyncTextEditingById(state, layoutHint, id)` — index not stored  
4. Resolve text-edit layout: id → `FindIndexById`  
5. Getters `SelectedAnnotationIndex` / `TextEditingIndex` always **-1** (transitional; prefer Resolve*)

## Touch paths

- `src/screenshot/editor/ScreenshotEditorState.h` — field delete + Select/Sync/HasSelection/IsEditingText
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — Resolve* id-only; SelectInHost id sole
- `tests/test_annotation_document_dual_write_contract.cpp` — id-only contracts
- `tests/test_screenshot_editor_state_contract.cpp` — id-only contracts

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
rg selectedAnnotationIndex|editingTextIndex struct fields → 0 (comments/params only)
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| pure `selectedAnnotationIndex` field | **0** |
| pure `editingTextIndex` field | **0** |
| selection / text-edit id sole | **on** |
| EditSession mid-edit | **on** (CLOSE-1..4) |
| create/draw projection residual | still open |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~103** (ADR-003 硬停 120 final) |

## Granularity note

One domain: short-life index field residual delete (selection + text-edit). Complements S-E-48/50/52 product dual 0. Docs same commit. No pin.

## NEXT

S-E-CLOSE-6: EditSession Create draft (freehand/geometry create) **or** residual create/draw inventory deepen. Then S-A-CLOSE / S-D/S-F-CLOSE. Ban micro-slices. 合域强制.
