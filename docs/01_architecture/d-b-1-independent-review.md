# D-B-1 Independent Review — PDF session prefs sole authority

Date: 2026-07-20  
Commit: `5529e258`  
Review method: **command-reproducible adversarial checklist** (not implementer narrative).  
Subagent review tasks were launched but returned empty transcripts; this file records re-runnable mechanical gates only.

## Verdict

**PASS — authorize D-B-2.**

## Mechanical gates

| Gate | Command / check | Result |
|---|---|---|
| Window fields gone | `rg m_lastPdf\|m_pdfCloudRemember src tests` | **0** production hits |
| Sync dual-write gone | `rg SyncPdfImportSessionPrefsMirror src tests` | **2 comments only** (removal notes in `.h` / `DashboardHistory.cpp`); no decl/def/call |
| Old Sync API gone | `rg DashboardStateSyncPdfImport src tests` | **0** hits |
| Sole write API | `rg DashboardStateApplyPdfImportSessionPrefs` | def in `DashboardState.h` + writes in Import.inl×3, StateAndHelpers load×1, state contract test×1 |
| Frozen heads nonblank | file line counts | Messages **3660**, MessageRoute **5060**, DashboardState **1320**, EditorState **1467** — flat vs live baseline |
| Hermetic | `ctest -L hermetic` @ build after cutover (implement session) | **50/50** |
| Build | `build.bat --cmake` after cutover | **Success** |
| Fake wiring | scan diff for `(void)` new dual-write / dead Sync | **none** in 5529e258 |
| Password persist | prefs struct has no password field; passwords only on `DashboardPdfImportOptions` per-import | **not persisted** in session prefs |

## Deleted symbols (confirmed absent as identifiers)

- `m_lastPdfPageRange`
- `m_lastPdfRenderDpi`
- `m_lastPdfMaxPixelEdge`
- `m_lastPdfMaxMegapixels`
- `m_lastPdfImageFormat`
- `m_lastPdfImageQuality`
- `m_pdfCloudRememberFullPdfConsent`
- `SyncPdfImportSessionPrefsMirror` (callable form)
- `DashboardStateSyncPdfImportSessionPrefs` (renamed to Apply)

## Residual notes (non-blocking)

- PDF **dialog UI** still lives in Window / `OcrDashboardWindow.cpp` — out of D-B-1 scope (later D-B slices).
- Folder / output-root / OLE dual-write still present — D-B-2+.
- `DashboardState.h` LOC flat (rename Sync→Apply, no dual-write growth).

## NEXT

**Authorize D-B-2** Folder import prefs sole authority cutover.
