# D-B Import/Dialogs — Package Exit Evidence

> **PROVISIONAL / REVALIDATE (2026-07-21):** retained as field/component evidence; the package-exit verdict is superseded pending strict research-criteria review. Do not execute the historical NEXT section below. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: `3168c61c` (D-B-11)  
Baseline: `48bb8020`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`

## Verdict

**IMPLEMENTATION-SIDE COMPLETE — authorize D-B package exit +1 after this evidence is accepted.**

Research §12.2 tasks and acceptance are met for the purposes of Stage1 package progress. Residual host glue (OLE register/handle on Window, Import.inl orchestration) is intentional Host boundary, not dual-write session prefs.

## Research §12.2 task checklist

| Task | Status | Evidence |
|---|---|---|
| PDF password dialog | **done** | `DashboardPdfPasswordDialog.*` (D-B-8) |
| PDF options dialog | **done** | `DashboardPdfOptionsDialog.*` (D-B-11) |
| Folder options dialog | **done** | `DashboardFolderImportOptionsDialog.*` (D-B-9) |
| output root / artifact options picker | **done** | `DashboardSelectOcrOptionsOutputRoot` + `DashboardOutputArtifactOptionsDialog.*` (D-B-9/10) |
| OLE drop object | **done** | `DashboardOleDropTarget.*` COM TU (D-B-7); Window keeps Register/CanAccept/Handle as Host |
| dialog state/font/DPI ownership | **done** | `DashboardDialogLayout.h` shared helpers; dialogs own local state |
| Window typed result | **mostly** | Folder: `DashboardRunFolderImportOptionsDialog` → Apply state. PDF: Import.inl still fills `PdfOptionsDialogState` then writes state (orchestration Host, not dual-write prefs) |
| ImportExport.inl → Import.inl | **done** | pre-existing rename |

## Acceptance checklist

| Item | Status | Notes |
|---|---|---|
| `OcrDashboardWindow.cpp` helpers 显著减少 | **yes** | ~4046 → **689** LOC after D-B dialog extracts |
| password 不持久化 | **yes** | password out-param only; no session field |
| cancel/error contract | **yes** | hermetic 50/50; no intentional contract change |
| import/OLE hermetic smoke | **yes** | `ctest -L hermetic` **50/50** post D-B-11 |

## Session-prefs ownership cutovers (D-B-1..6)

All Window dual-write fields deleted (0 production symbol hits):

- PDF session prefs, folder prefs, batch output roots, artifact defaults, cloud risk, OCR mode

## Frozen-head guardrails (post D-B-11)

| File | nonblank | Rule |
|---|---:|---|
| Messages.inl | 3660 | no growth |
| MessageRoute.h | 5060 | frozen |
| DashboardState.h | 1320 | frozen |
| EditorState.h | 1467 | frozen |

## Residual (not D-B package blockers)

- OLE Register/CanAccept/Handle/Extract still Window methods (Host + HWND lifecycle)
- Import.inl still orchestrates PDF dialog HWND loop (could further thin later)
- D-C+ dual-write mirrors still present (History/Batch/Canvas) — **next packages**

## Stage1 budget

- Stage1 code commits: **11** (D-B-1..11) of target ≤32 / hard stop 55
- ownership cutovers: **11**
- package exits after this evidence: **2/9** (D-A + D-B)

## NEXT

1. Accept this package evidence → EXECUTION package exit 2/9  
2. Open **D-C History** first vertical cutover (delete `SyncHistoryModelMirror` / Window history write authority slice)
