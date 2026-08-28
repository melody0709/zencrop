# D-B-2 Independent Review — Folder import prefs sole authority

Date: 2026-07-20  
Review method: command-reproducible mechanical gates (same form as D-B-1).  
Implementer session also authored cutover; **user / other AI should spot-check** before D-B-3.

## Verdict (mechanical)

**PASS — authorize D-B-3 after human/other-AI ack of this checklist.**

## Gates

| Gate | Result |
|---|---|
| `m_folderImportRecursive` / `MaxDepth` / `ExcludePatterns` identifiers | **0** in src+tests |
| `SyncFolderImportPrefsMirror` callable | **0** (2 removal comments only) |
| `DashboardStateSyncFolderImportPrefs` | **0** (renamed Apply) |
| Sole write | `DashboardStateApplyFolderImportPrefs` — Import.inl×2, StateAndHelpers load×1, state contract×2 |
| Build | `build.bat --cmake` Success |
| Hermetic | `ctest -L hermetic` **50/50** |
| Fake wiring | none in diff |
| Frozen heads | MessageRoute/Messages/State/Editor nonblank flat (unchanged files) |

## Deleted

- Window `m_folderImportRecursive`, `m_folderImportMaxDepth`, `m_folderImportExcludePatterns`
- `SyncFolderImportPrefsMirror`
- `DashboardStateSyncFolderImportPrefs` → `DashboardStateApplyFolderImportPrefs`

## Residual (out of scope)

- Batch output root dual-write (`SyncBatchOutputRootsMirror`) → **D-B-3**
- OLE drop target / dialog class extraction → later D-B
- PDF/folder **dialog HWND UI** still on Window

## NEXT

**D-B-3** Batch output root prefs sole authority (after ack).
