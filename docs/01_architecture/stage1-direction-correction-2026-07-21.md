# Stage 1 Direction Correction

Date: 2026-07-21

Review anchor: `16580b6a`

Status: **Accepted documentation correction; Stage 1 Gate REOPENED**

## Decision

The `stage-1-gate-complete` tag and the PASS verdict in `stage1-gate-independent-review.md` are retained as historical records, but they no longer authorize Stage 2 work. Stage 2 is paused after S-B-6. Existing product changes are not reverted.

The reason is not a change to the target architecture. The review applies the original GOAL and research acceptance criteria strictly. The former Gate review treated partial field migration as complete package ownership and then classified unmet hard conditions as non-blocking Host residuals without an ADR.

## What the recent work actually achieved

The 72 commits from `48bb8020` through `16580b6a` contain material improvements:

- numerous duplicate Window fields were deleted rather than merely mirrored;
- `DashboardMessageRoute.h` fell from about 5,060 to 154 physical LOC;
- `OcrDashboardWindow.Messages.inl` fell from 3,661 to 2,507 physical LOC;
- First-party LOC fell from 102,301 to 95,698;
- Dashboard family LOC fell from 35,896 to 29,446;
- Stage 0 reached a defensible Gate;
- S-B-1..6 are net-delete field-authority migrations and are retained.

These results are preparation and partial ownership cutover. They do not satisfy the original component and Host/TU acceptance criteria by themselves.

## Contradictions that invalidate the old Gate

| Package | Original requirement | Evidence at `16580b6a` | Strict result |
|---|---|---|---|
| D-A | Tests out of production source | Completed | **PASS** |
| D-B | Dialog/OLE components; Window receives typed result; smoke contracts | Components exist; package evidence says typed result is only “mostly”; manual smoke is not closed | **REVALIDATE** |
| D-C | Repository/Model/Cache/Projection; stable-key selection; Window only facade | Repository/Model exist, but Window owns both; selection remains index-based; `DashboardHistory.cpp` has 47 Window methods; no explicit Cache/ResultProjection component | **PARTIAL** |
| D-D | State/Controller/Command/Event; business collections leave Window | `DashboardState.h` is a 1,520-line aggregate; Controller/Command/Event are absent; business collections remain in Window | **PARTIAL** |
| D-E | Coordinator owns jobs, queues, generation and shutdown | No `DashboardBatchCoordinator.*`; Window owns `m_batchTasks`, `m_activePdfJobs`, drop/PDF/external OCR queues/maps | **PARTIAL** |
| D-F | SourceRail Model/Layout/Renderer/InputController/Cache | These components are absent; Window still owns selection/input/layout orchestration and reads job collections directly | **PARTIAL** |
| D-G | Canvas State/Controller/Renderer/HitTester and block ownership | Only view/hover/selection fields moved; `m_currentBlocks` and paint/hit-test logic remain in Window `.inl` files | **PARTIAL** |
| D-H | Preview Coordinator, typed protocol/version/token, asset policy | Components are absent; only a few flags moved; Window still directly owns preview host/orchestration | **PARTIAL** |
| D-I | Zero production class-method `.inl`; Window is Host; no Repository/Batch queue ownership | 11 Dashboard `.inl` files contain 254 `OcrDashboardWindow::` occurrences; Window still owns Repository/Batch queues; main handler is about 1,168 lines | **NOT PASSED** |

The old evidence explicitly documented several of these failures, then waived them:

- D-E called the remaining batch collections “Host job lifecycle collections”, although the research package assigns those collections to the Coordinator.
- D-F accepted key/selection migration without the SourceRail component boundaries.
- D-G accepted flags without Canvas controller/renderer/hit-test ownership.
- D-H accepted two flags without Preview Coordinator/protocol ownership.
- D-I accepted production class-method `.inl` as “one-TU Host navigation”, although both GOAL and research require zero.

An implementation review cannot weaken a Gate defined by GOAL. A real exception requires an ADR accepted before the Gate decision.

## Corrected accounting

- Stage 0 Gate: **PASS**.
- Stage 1 strict package exits: **1/9 confirmed** (D-A).
- D-B: **under strict revalidation**.
- D-C..D-H: **partial assets and field cutovers retained**.
- D-I: **not passed**.
- Dashboard field/mirror cutover slices: **44 retained as lower-level progress**, not package exits.
- Stage 2 S-B-1..6: **retained field-authority cutovers**, not an S-B package exit.
- Stage 2/3/4: **paused** until the corrected preceding Gate is passed.

No percentage is inferred from field counts. Progress is reported as completed Gates, strict package exits, remaining domain owners, class-method `.inl`, Host surface and cycle edges.

## Stage 2 follow-up

S-B-1..6 deleted real legacy fields and reduced `src` by about 209 net lines. However, their no-op `SyncScreenshot*Mirror` methods, declarations and call sites remain. ADR-001 requires the Sync pipeline to be deleted with the legacy owner. When Stage 2 is authorized again, the first slice is S-B-CLEANUP; S-B-7 follows only after that cleanup is reviewed.

## Tag and document handling

- Do not rewrite Git history and do not delete the historical tag.
- Any automation or AI must treat `stage-1-gate-complete` as superseded by this correction.
- The old package evidence remains useful for locating deleted fields and tests, but its package-exit verdicts are provisional/historical.
- A new Stage 1 Gate may be tagged separately after strict completion, for example `stage-1-gate-complete-v2`; do not move the old tag.

## Re-entry

The next action is D-B-R1 strict revalidation, followed by the original D-C→D-I package order. Exact execution constraints, file-growth guards and review cadence are in the live EXECUTION board.
