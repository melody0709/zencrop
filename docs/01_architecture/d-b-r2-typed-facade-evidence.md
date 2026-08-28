# D-B-R2 — PDF Options Typed Facade Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-B-R1 `e96a9634` (thin wrappers deleted)

## Purpose

Close the §12.2 residual: Window owned PDF options `CreateWindow` + message loop.  
Introduce typed `DashboardRunPdfImportOptionsDialog` so Window only seeds prefs and applies result via callbacks.

## Change

| Item | Detail |
|---|---|
| `DashboardPdfImportOptions` | Moved to `DashboardPdfOptionsDialog.h` (dialog domain owns type) |
| `DashboardRunPdfImportOptionsDialog` | New API: owns class reg, CreateWindow, message loop, preview temp cleanup, optional test-drive |
| `OcrDashboardWindow::PromptForPdfImportOptions` | Thinned: seed from `DashboardState` + preflight + callbacks + call Run + cloud consent post-step |
| Import.inl | Removed `CreateWindowExW` / `GetMessageW` / static dialog helper calls for PDF options |

### Typed path (product)

1. Window seeds `options` from `DashboardState` PDF session prefs + OCR mode + artifact defaults.
2. Window runs `DashboardCollectPdfImportPreflight`.
3. Window calls `DashboardRunPdfImportOptionsDialog` with:
   - `editOutputArtifacts` → `PromptForOutputArtifactOptions`
   - `saveSettings` → apply prefs/artifacts/OCR mode to `DashboardState` + settings files
4. On accept: Window re-applies save + cloud full-PDF consent MessageBox if needed.

### Test hooks

`DashboardPdfOptionsDialogTestDrive` optional on run input; hermetic window tests inject via existing `g_dashboardWindowTest*` flags without product path owning dialog HWND loop.

## §12.2 update after R2

| Item | Status |
|---|---|
| PDF password dialog | yes (R1) |
| PDF options dialog typed run | **yes (R2)** — modal loop in dialog TU |
| Folder / artifact / OLE | yes (prior) |
| Window only typed result | **yes for dialog entry points** (orchestration seed/apply remains Host, not dialog ownership) |
| password not persisted | yes |
| import/OLE manual smoke archive | **still open** (blocks **confirmed** package exit) |

## Verdict

**D-B still REVALIDATE** until:

1. Independent reviewer accepts §12.2 against R1+R2 evidence;
2. Manual import/OLE smoke evidence is archived (or reviewer accepts hermetic window PDF options tests as substitute).

Not a full package exit. Net-delete Host dialog loop ownership is real progress.

## KPI

| Metric | Before R2 | After R2 |
|---|---:|---:|
| `OcrDashboardWindow.Import.inl` physical | 929 | **799** |
| `DashboardPdfOptionsDialog.cpp` physical | ~1865 | **2046** (Run API moved here) |
| hermetic | 50/50 | **50/50** |
| Messages / Route / State | 2416 / 145 / 1320 | unchanged |

## NEXT

1. Optional D-B-R3: archive smoke checklist or expand hermetic PDF options contract if missing coverage.
2. Independent reviewer for D-B → confirmed (only if smoke accepted).
3. Else proceed D-C History strict package work without claiming D-B confirmed.
