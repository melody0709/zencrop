# S-E-1 evidence: pure legacy-document mutations by id (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-1  
Prior: S-D residual `cc7ed667`

## Intent

**Ownership domain (single slice):** Host ToolbarInteraction undo/redo apply lambdas dual-own document mutation (find/remove/insert/replace by id + selection adjust). Move pure mutation logic to `AnnotationLegacyDocument.h`; Host lambdas become thin projectors of side-effects (pending text id, serial counter, pure state).

## Deleted Host dual authority

| Legacy Host lambda | Sole pure API |
|---|---|
| `findAnnotationIndexById` | `ScreenshotAnnotationFindIndexById` |
| `removeAnnotationById` body | `ScreenshotAnnotationRemoveById` |
| `insertAnnotationFromSnapshot` body | `ScreenshotAnnotationInsertFromSnapshot` |
| `replaceAnnotationFromSnapshot` body | `ScreenshotAnnotationReplaceFromSnapshot` |

Host retains: pending-text-id clear, serial counter, pure state projection (Host side-effects).

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — **new** pure sole mutations
- `src/screenshot/OverlayWindowScreenshot.inl` — include pure header
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — 4 dual lambdas → pure + project
- `tests/test_annotation_legacy_document_contract.cpp` — **new** contract
- `tests/CMakeLists.txt` — register hermetic

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 59/59
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **59/59** |
| Host undo mutation dual | **0** |
| Stage 2 code commits | **~40** |

## Granularity note

One domain: 4 related mutation helpers + Host dual delete + tests. Not four 1-helper slices.

## NEXT

S-E-2: AnnotationDocument ownership seam (Model rename/wrap + Host hold dual-write create/delete) or selected stable id dual-write. **Must prefill domain list before src edits.**
