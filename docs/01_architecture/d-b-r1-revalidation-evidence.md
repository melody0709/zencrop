# D-B-R1 — Import/Dialogs Strict Revalidation Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Baseline review: research §12.2 + EXECUTION D-B-R1  
Stage1 status: REOPENED (strict package accounting)

## Purpose

Strict revalidation of D-B against research §12.2. Prior package-exit verdict was provisional. This slice:

1. Documents what already meets §12.2;
2. Closes a concrete residual: Window.cpp thin dialog wrappers;
3. Lists remaining gaps that still block **strict confirmed** package exit.

## §12.2 checklist

| Item | Status | Evidence |
|---|---|---|
| PDF password dialog extracted | **yes** | `DashboardPdfPasswordDialog.*`; `DashboardPromptForPdfPassword` out-param only |
| PDF options dialog extracted | **partial** | WndProc/helpers in `DashboardPdfOptionsDialog.cpp`; Window still owns `PromptForPdfImportOptions` orchestration (CreateWindow + message loop) |
| Folder options dialog extracted | **yes** | `DashboardRunFolderImportOptionsDialog` typed result |
| Output artifact options picker | **yes** | `DashboardShowOutputArtifactOptionsDialog` |
| OLE drop object | **yes** | `DashboardOleDropTarget` COM; Window owns accept/handle policy |
| Dialog state/font/DPI ownership | **partial** | Dialogs own controls; layout helpers in `DashboardDialogLayout.h` |
| Window only receives typed result | **partial** | Folder/artifact/password: typed API. PDF options: Window still builds `PdfOptionsDialogState` and runs loop |
| Import.inl rename (no Export) | **yes** | `OcrDashboardWindow.Import.inl` |

### Acceptance (research)

| Item | Status | Notes |
|---|---|---|
| OcrDashboardWindow.cpp top helpers reduced | **improved this slice** | physical LOC **689→510**; deleted thin static wrappers to Dashboard* |
| password not persisted | **yes** | module comment + out-param only; models text says not saved |
| cancel/error contract unchanged | **yes** | hermetic 50/50; no intentional contract change |
| import/OLE/manual smoke | **open** | no durable manual smoke log in-repo; hermetic only |

## This slice code change (minimal gap)

**Delete thin wrappers in `OcrDashboardWindow.cpp`** that only forwarded to public `Dashboard*` APIs; rename call sites in Import/Batch/StateAndHelpers/EntryPoints/Messages and PdfOptionsDialog to call `Dashboard*` directly.

Removed static wrappers include (non-exhaustive): password/options/preflight/validate/preview/artifact summary/format/OCR mode normalize/cloud mode, plus dead dialog layout helpers (`GetWindowTextWide`, `SetControlFont`, `CenterWindowOnOwner`, `ScaleDialogValue`).

Also fixed a rename hazard in `DashboardPdfOptionsDialog.cpp` where local statics had been self-recursive after mechanical rename; deleted those locals so global `Dashboard*` from headers are used.

## Residual (blocks strict D-B confirmed)

1. **PDF options host orchestration** still in `OcrDashboardWindow::PromptForPdfImportOptions` (CreateWindowEx + message loop on `PdfOptionsDialogState`). Need a single `DashboardRunPdfImportOptionsDialog(...)` (or equivalent) that returns typed `DashboardPdfImportOptions` + outputRoot, so Window only seeds prefs and applies result.
2. **Manual smoke evidence** for import/OLE not archived (hermetic cannot fully cover modal/OLE).
3. Optional: move `PopulateOcrModeCombo` to a pure/shared helper (still local static in Window.cpp and PdfOptionsDialog.cpp).

## Verdict

**REVALIDATE progress: residual thin-wrapper debt closed; D-B remains REVALIDATE (not confirmed).**  
Do **not** mark D-B package exit until residual (1) is closed and independent reviewer accepts §12.2 with smoke evidence.

## KPI

| Metric | Before | After |
|---|---:|---:|
| `OcrDashboardWindow.cpp` physical LOC | 689 | **510** |
| hermetic | 50/50 | **50/50** |
| Messages / Route / State freeze | 2416 / 145 / 1320 | unchanged |

## NEXT

1. D-B-R2 (or continue R1): extract `DashboardRunPdfImportOptionsDialog` typed facade; delete Window message-loop ownership for PDF options.
2. Capture/import OLE smoke evidence path for package exit.
3. Independent reviewer for D-B confirmed only after (1)+(2).
