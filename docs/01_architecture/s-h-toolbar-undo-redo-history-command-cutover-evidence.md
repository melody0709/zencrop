# S-H Toolbar Undo/Redo History Command Cutover Evidence

Date: 2026-07-23
Package: Stage 2 S-H — ToolbarCommand residual
Code review anchor (commit subject): refactor(arch): extract toolbar undo redo history

## Scope

This is one ownership vertical only: committed annotation history Undo/Redo from
OverlayWindow::HandleScreenshotToolbarCommand. It does not include slider mutation,
color-picker, text/watermark draft editing, side dialogs, tool selection, or generic
session commands.

## Ownership result

AnnotationHistory::applyUndoRedo now owns the complete committed history transaction:

1. It selects an Undo or Redo group from AnnotationHistory.
2. It applies Create/Delete/Modify through the sole AnnotationDocument primitives.
3. It clears the pending-text id when the removed annotation was pending.
4. It projects the final committed result to ScreenshotEditorState: annotation count,
   selected id, cleared text-edit id, serial counter, and history availability.

The owner accepts only the redo direction, ScreenshotAnnotationModel/Document, and
ScreenshotEditorState. It accepts no OverlayWindow&, HWND, POINT, Host container,
callback/std::function, paint, persistence, dialog, tool-selection, or edit-session input.

The historical group order is preserved. The projection is intentionally performed after
the synchronous group transaction; no later failed entry overwrites the selected id from
the last successful mutation.

## Same-commit Host deletion

Deleted from OverlayWindow::HandleScreenshotToolbarCommand:

- recalculateSerialCounter;
- projectAnnotationMutationById;
- removeAnnotationById;
- insertAnnotationFromSnapshot;
- replaceAnnotationFromSnapshot;
- applyHistoryEntry;
- applyHistoryEntries;
- the direct availability projection, undoGroup/redoGroup dispatch, and per-entry
  Document mutation orchestration.

The Host now retains only the product-boundary work: commit any in-flight broken-line/text
edit, close the tertiary panel, call the explicit owner, and redraw when it reports a
committed mutation.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| HandleScreenshotToolbarCommand | 1030 | **936** | -94 |
| Screenshot family LOC | 30363 | **30363** | 0 (strict no-growth held) |
| First-party LOC | 101187 | **101187** | 0 (.cpp -5 / .h +5) |
| ToolbarRender.cpp LOC | 2379 | **2379** | 0 |
| CMake product .cpp count | 118 | **118** | 0 |
| Forbidden include edges | 8 | **8** | 0 |
| AnnotationHistory.cpp physical LOC | 101 | **190** | bounded existing owner; no new file |

Static negative checks:

- old Host history orchestration symbol set = **0**;
- AnnotationHistory Window/handle/callback input symbol set = **0**;
- production class-method .inl remains **0**.

## Characterization coverage

tests/test_annotation_history.cpp now exercises:

- Create Undo/Redo with Document count, selected id, text-edit reset, pending-text cleanup,
  and availability projection;
- Modify Undo/Redo snapshot restoration;
- serial counter recomputation;
- grouped Delete Undo/Redo Document ordering and final selection;
- unavailable Undo/Redo as a no-op.

## Verification

```text
build.bat --cmake --stop-running                              PASS
ctest --test-dir build/cmake -L hermetic --output-on-failure  68/68 PASS
scripts/architecture_audit.ps1                                PASS
git diff --check                                              PASS
```

Audit after the cutover reports Screenshot family **30363**, ToolbarCommand **936**,
ToolbarRender **166**, CMake product .cpp **118**, and forbidden include edges **8**.

## Non-claim and pause

This is an S-H partial ownership cutover. It is not an S-H package exit, Stage 2 Gate
PASS, Stage 3 Gate, or Stage 4 unlock. The next code slice is intentionally paused until
a new read-only mapping selects one different ToolbarCommand ownership domain.
