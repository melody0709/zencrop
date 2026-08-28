# Architecture refactor — final DoD review

Date: 2026-07-24
Status: **PASS**

## Scope and source anchor

Review source anchor: `83646066dc076e9032407aaceb56a2ce8dcd2f69` —
`refactor(core): centralize model registry`.

The later `31eb306` documentation-hygiene commit and this review are docs-only;
they do not change that verified source tree.

## Gate and DoD verdict

| Requirement | Current proof | Verdict |
|---|---|---|
| Stage 0 build authority | CMake sole product authority; `build.bat --cmake --stop-running` PASS; runtime staging mismatch `0`. | PASS |
| Stage 1 Dashboard ownership | D-A…D-I `9/9 confirmed`; production class-method `.inl = 0`. | PASS |
| Stage 2 Screenshot ownership | Formal review accepts single Document/EditSession authority, shared renderer, ADR-004 bounded Host residual and ADR-005 production DIB/HDC evidence. | PASS |
| Stage 3 dependency architecture | Formal review records all three GOAL cycle groups `0`, Settings hygiene and removed AppHost locator/composition root. | PASS |
| Stage 4-A production asset | `PATH_TABLE.tsv` sole staged source is `src/assets/icons/PATH_TABLE.tsv`; production code does not search private research paths. | PASS |
| Stage 4-B model locations | `core/OcrModelRegistry.h` is production owner used by manager, Doc engine and PP-OCR factory; `ZenCrop.exe --model-dry-run <output.json>` only reads settings/filesystem metadata. | PASS |
| Stage 4-C canonical docs | `AGENTS.md`, GOAL and EXECUTION have one live status; GOAL baseline state is explicitly historical; dated correction/ADR records link to later Gate verdicts; archives retained. | PASS |

## Final quantitative evidence

Fresh validation at the source anchor:

- `build.bat --cmake --stop-running`: PASS.
- `ctest -L hermetic`: **68/68 PASS**.
- `scripts/architecture_audit.ps1`: PASS.
- First-party source: `100798 <= 101060` hard ceiling.
- Screenshot family: `30116 <= 30640`; Dashboard family: `30999`.
- CMake product `.cpp`: `124`; production class-method `.inl`: `0`.
- Forbidden include edges and all three audited cycle groups: `0`.
- `GetAppHost`, `AppHostServices`, `m_annotationProjection` and `RebuildHostProjection` production searches: `0`.

No Overlay, WebView, model server, model inference or network runtime was started.

## Explicit non-claims

- D18 still excludes physical deletion/deduplication of OCR model files.
- Real Overlay Host-GDI attachment remains an optional release/manual check under ADR-005, not an architecture Gate blocker.
- A later feature or behavior change needs its own scope and verification; it does not reopen this completed refactor by default.

## Verdict

All ordered Stage 4 packages exited, every Stage Gate has formal evidence, and
the GOAL Stage 4 / total DoD conditions are met. The GOAL static architecture
refactor is **complete**. The separately tracked R5 structural-remediation package
also exited on 2026-07-24; its deletion/guard evidence is in the
[R5 exit review](architecture-refactor-r5-structural-remediation-exit-2026-07-24.md).
Neither verdict proves runtime/release behavior; that remains a separate scope.
