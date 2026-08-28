# Architecture refactor throughput calibration

Date: 2026-07-23

Status: Accepted process calibration. This changes execution mechanics only; it does not change
the GOAL, Stage order, Gate definitions, or product behavior contract.

## Evidence

The 13 real S-H toolbar cutovers from 8111d2ab through 8749e01 span 386.3 minutes. Their mean
interval is 32.2 minutes; the latest four intervals are 43.3, 40.2, 59.6, and 73.2 minutes.
The real gain is substantial: DrawScreenshotToolbar fell 2527 -> 166 and
HandleScreenshotToolbarCommand fell 1178 -> 553.

The locally measured verification cost is not the bottleneck: incremental build is about 4.4 s,
the full 69-test hermetic run about 1.6 s, and the architecture audit about 3.4 s. Do not remove
the full hermetic gate to chase throughput. The avoidable cost is repeated prose, repeated
history reading, duplicate audit output/reviews, and test-target proliferation.

## Decisions

1. **Keep behavioral gates.** Every src ownership cutover still gets one incremental build, one
   full ctest -L hermetic, one architecture metric audit, and git diff --cached --check.
2. **Run each gate once.** After final source changes, do not repeat build/CTest/audit unless the
   source changes again. Post-commit proof is git status --short plus git log -1; a second full
   audit is not required solely to observe the new commit hash.
3. **Use compact audit reporting.** Each slice records only the Gate/KPI summary. Full audit JSON
   inventories are required at package exit, Stage Gate review, KPI regression, audit-script change,
   or an explicit direction review. Until the script has a summary mode, run it once but do not
   paste its raw inventory into execution documents.
4. **Keep a hot board.** EXECUTION must stay at or below about 200 lines of active instructions.
   It contains current facts, KPI, WIP/pause, a compact ledger, prefill, and hard rules. Completed
   prose belongs in evidence/archive, not the default startup context.
5. **No three-way document duplication.** A normal partial code cutover updates its exact EXECUTION
   anchor/KPI and one compact ledger row in the same commit. It does not create a standalone
   evidence document unless it is an ADR, package exit, Stage Gate, or high-risk external behavior,
   history/Document transaction, or pixel/performance proof.
6. **AGENTS is stable bootstrap.** Ordinary partial cutovers do not update AGENTS. Update it only
   for a Stage/Gate/redline/reading-order change or a genuinely different pause constraint.
7. **Tests are risk-driven, not quota-driven.** Preserve existing tests. Add or extend a focused
   test for a new state transition, Document/History/EditSession transaction, dialog/capture
   boundary, or uncovered behavior branch. Do not add C++ tests merely to prove LOC/token deletion;
   use audit/static checks. Do not create one test executable per minor command branch.
8. **Avoid test-structure bloat.** Do not append unrelated domains to a very large generic test
   merely for convenience, and do not create a new target if an existing durable owner/domain
   contract can cover the behavior.
9. **Require a meaningful vertical.** A standalone partial cutover must either remove a material
   Host/God body (normally about 80 physical lines or more) or close an atomic risky boundary such
   as a Document/history transaction, dialog/capture lifecycle, or EditSession transition. This is
   not a license to pad a slice; it prevents 1-field/panel-forwarding commits.
10. **Review by signal, not commit count.** Do a lightweight KPI check after every slice. Do a full
    direction review after three new partial cutovers, at package exit, at a Gate claim, or whenever
    a required down-metric worsens.

## Required per-slice record

Before src changes, write at most 12 lines of prefill:

~~~text
ID / ownership domain:
Why one vertical:
Owner inputs and forbidden dependencies:
Host body / consumer to delete in this commit:
Explicit exclusions:
Behavior risk and focused-test decision:
Expected KPI delta / no-growth ceiling:
Verification commands:
~~~

At completion, add one compact ledger row with commit, domain, Host/God delta, family LOC,
hermetic result, and status. The absence of a new evidence file is normal for a partial cutover.

## Non-goals

- This does not weaken Stage 2/3/4 Gate rigor.
- This does not permit helper-only, rename-only, docs-only, or 1-field work to count as progress.
- This does not authorize combining unrelated command domains merely to increase commit size.
- This does not delete historical evidence; the S-H history index preserves lookup paths.
