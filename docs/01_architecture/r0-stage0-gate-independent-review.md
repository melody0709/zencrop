# Stage 0 Gate — Independent Review

Date: 2026-07-20  
Reviewer: independent AI session (not implementer of R0-S0A/S0B/S0C/FIX-1)  
Tree under review: docs pin `1e929ad8` / code `24b5d62d` (R0-FIX-1)  
Evidence pack: `docs/01_architecture/r0-s0c-stage0-gate-evidence.md`

## Verdict

**PASS — Stage 0 Gate complete.**

Authorize:

1. Tag `stage-0-gate-complete` at `1e929ad8` (docs pin over R0-FIX-1 code)
2. Open Stage 1 **D-B Import/Dialogs** first vertical slice

## Re-verification (this session)

| Claim | Method | Result |
|---|---|---|
| CMake sole product compile authority | `architecture_audit.ps1` | `soleAuthorityCMake=True`; build.bat cpp=0; onlyInCMake/onlyInBuildBat=0/0 |
| `--cl` removed | run `build.bat --cl` | exit **2**; message points to CMake docs |
| Hermetic via CTest | `ctest -L hermetic` in `build/cmake` | **50/50** Passed (1.09s); OpenCV 3 under hermetic |
| Test inventory complete | `tests/test_inventory.json` + `tests/test_*.cpp` | **72/72** files; status 50 hermetic + 1 runtime + 21 inventory_skip |
| CTest registration | `ctest -N` | Total Tests: **72**; inventory label 21 |
| Runtime staging | audit + filesystem sample | mismatches **0**; ZenCrop.exe, WebView2Loader/onnxruntime/opencv_world500 dlls, PATH_TABLE.tsv, webview_assets/imagecodecs/ocr_templates present |
| Frozen-head guardrail | git show `48bb8020` vs HEAD nonblank LOC | Messages.inl 3660, MessageRoute 5060, DashboardState 1320, EditorState 1467 — **unchanged** (evidence pack used nonblank; physical = 3784/6101/1520/1659, also flat) |
| Cycle edges | audit forbidden edges | **16** directed edges / 3 groups — not Stage0 exit; Stage1+ |
| R0-FIX-1 residual | history repo UTF + LOC delta | Dashboard family +12 physical lines (UTF fix only); not Gate blocker; already landed |

The fresh audit artifact was archived in the private research workspace.

## KPI metric note (non-blocking)

EXECUTION / S0C evidence “physical LOC” for frozen heads match **nonblank** line counts, not raw physical lines.  
Raw physical held flat across Stage0; nonblank held flat. **No growth.** Future slices: prefer audit script / raw physical, or label “nonblank” explicitly — do not redefine baseline numbers in prose alone.

## Gate checklist (GOAL §7 Stage 0)

- [x] audit repeatable  
- [x] CMake sole compile authority (no direct-cl product list)  
- [x] hermetic tests via CTest  
- [x] runtime staging consistent  
- [x] unrelated dirty not mixed into Stage0 product commits (path-explicit adds; tree clean at review)  
- [x] 0-B / 0-C closed (R0-S0A / R0-S0B)  
- [x] **Independent reviewer confirms evidence**  
- [x] tag then D-B ownership cutover allowed  

## Non-blocking residuals (Stage1+)

- 3 cycle groups / 16 forbidden edges  
- MessageRoute / DashboardState / EditorState freeze debt  
- inventory_skip 21 + runtime defaults SKIP  
- audit built-in test heuristic labels ≠ inventory JSON (JSON + CTest labels authoritative)

## Next slice (authorized)

**D-B-1** (first vertical under D-B Import/Dialogs): PDF import **session prefs** sole authority cutover — delete Window `m_lastPdf*` / related dual-write Sync when DashboardState already holds mirror; Window receives/writes typed prefs only via state. Further D-B slices: folder prefs, output root picker, OLE drop target, full dialog class extraction.
