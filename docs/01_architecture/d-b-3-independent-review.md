# D-B-3 Independent Review — Batch output root prefs sole authority

Date: 2026-07-20  
Review method: command-reproducible mechanical gates.  
Implementer session also authored cutover; **user / other AI should spot-check** before D-B-4.

## Verdict (mechanical)

**PASS — authorize D-B-4 after human/other-AI ack of this checklist.**

## Gates

| Gate | Result |
|---|---|
| `m_preferredBatchOutputRoot` / `m_lastBatchOutputRoot` / `m_recentBatchOutputRoots` identifiers | **0** in src+tests (comments only for Sync removal) |
| `SyncBatchOutputRootsMirror` callable | **0** |
| `DashboardStateSyncBatchOutputRoots` | **0** (renamed Apply) |
| Sole write | `DashboardStateApplyBatchOutputRoots` via Remember/Forget/load/choose/Batch paths |
| Build | `build.bat --cmake` Success |
| Hermetic | `ctest -L hermetic` **50/50** |
| Fake wiring | none |
| Frozen heads | MessageRoute/Messages/State/Editor nonblank flat |

## Deleted

- Window `m_preferredBatchOutputRoot`, `m_lastBatchOutputRoot`, `m_recentBatchOutputRoots`
- `SyncBatchOutputRootsMirror`
- `DashboardStateSyncBatchOutputRoots` → `DashboardStateApplyBatchOutputRoots`

## Residual (out of scope)

- `m_outputArtifactDefaults` dual-write → later D-B
- OLE drop target still Window-owned → later D-B
- PDF/folder dialog HWND UI still on Window

## NEXT

**D-B-4** after ack (output artifact defaults or OLE/dialog slice).
