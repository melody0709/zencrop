# S-E residual recheck: pure dual-authority methods (post S-E-6)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E residual recheck (post S-E-6 `2c7ecc9a`)  
Prior residual: `s-e-residual-inventory-host-pure-methods-complete-evidence.md` (post S-E-4)

## Intent

Recheck residual Host pure dual-authority after S-E-5 (active color) + S-E-6 (crop geometry). Stage2 at **警戒 45** — no 1-field slices; Document dual-write is multi-slice vertical.

## Pure dual-authority cutover complete (S-E-1..6)

| Slice | Deleted Host dual | Sole pure |
|---|---|---|
| S-E-1 | undo find/remove/insert/replace lambdas | `AnnotationLegacyDocument.h` |
| S-E-2 | `SetSelectedScreenshotAnnotationIndex` | `ScreenshotEditorSetAnnotationCountAndSelect` |
| S-E-3 | `SetActiveScreenshotTool` | `ScreenshotEditorSelectToolWithHistory` |
| S-E-4 | 6× color-target predicates + MarkDirty | pure `ScreenshotEditorIs*ColorTargetActive` |
| S-E-5 | `GetActiveScreenshotColor` / `Index` | `ScreenshotActiveColor.h` |
| S-E-6 | `GetCropRect` / `GetCropBounds` | `ScreenshotEditorCropDragRect` / `CropBounds` |

Live product scan (non-comment Host pure dual getters/setters of pure state only): **0**.

## Remaining Host surfaces (not pure dual authority)

| Category | Examples | Why Host |
|---|---|---|
| Annotation vector | `m_screenshotAnnotations` | runtime sole collection; Document dual-write **not started** |
| History | `m_annotationHistory`, `m_annotationModifyBefore` | Host undo stacks |
| Selection | pure index field | stable-id dual-write **not started** |
| Event/GDI | LButton/MouseMove/Draw/HitTest/Bitmap | HWND/GDI session |
| Settings I/O | Load/Save/Flush | disk side-effects |
| Style apply | SetActiveColorIndex/CustomColor, ApplyStyle | mutates Host annotations |
| Text edit | IsEditingScreenshotText, caret/IME | Host vector + HWND IME |
| Hit-test bounds | GetScreenshotAnnotationBounds | Host annotation struct |

## Research §11.5 status (unchanged structural gaps)

| Criterion | Status |
|---|---|
| Document holds items | **partial** — Model exists (tests); Host vector runtime sole |
| selected stable id | **partial** — history by id; selection still index |
| History on Document | **partial** — pure mutations; Host vector apply |
| Tool group vertical cutover | **NOT STARTED** |
| RenderContext / S-F | **deferred** |

## KPI

| Metric | After S-E-6 |
|---|---:|
| hermetic | **59/59** |
| Host pure dual-authority methods | **0** |
| Stage 2 code commits | **~45（警戒）** |

## Package status

- **Host pure dual-authority method cutover: DONE** (S-E-1..6)
- **Full S-E package exit (§11.5 Document + vertical groups): NOT closed**
- **Next at 警戒:** S-E-7 Document dual-write seed — Host holds `ScreenshotAnnotationModel` (or Document alias); dual-write create/delete/clear; **one domain**; TTL ≤3; no 1-field. Or independent residual-only docs if Document needs design slice first.

## NEXT

S-E-7: Document dual-write seed (Host member + create/delete/clear dual-write path). **Must prefill domain list before src edits.** 合域强制；Stage2 警戒 45 / 硬停 55.
