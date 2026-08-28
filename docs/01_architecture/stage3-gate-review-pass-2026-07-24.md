# Stage 3 formal Gate review — PASS

Date: 2026-07-24
Review source anchor: `42dac4bed41574f65c16a984d278bce750b7eb73` —
`refactor(arch): remove AppHost service locator`

## Verdict

**Stage 3 Gate: PASS.** This is a formal Gate result, not a claim that Stage 4
or the total architecture refactor is complete. Stage 4 is now unblocked.

## Gate evidence

| Gate condition | Current proof | Verdict |
|---|---|---|
| Three dependency-cycle groups are zero | Fresh architecture audit reports `0` directed edges for screenshot↔OCR UI, net↔engine and batch↔document. | PASS |
| Settings repository has no UI/net/engine dependency | `src/core/Settings.cpp` imports only core/Win32/config dependencies; no Dashboard, screenshot, net or OCR-engine include remains. | PASS |
| Composition root is real | `AppHost`, `GetAppHost`, `AppHostServices`, Dashboard/Overlay registration and their facade test target are deleted. `main.cpp` bootstraps the neutral `PaddleVlServerService`; `OcrEngineFactory` constructs Paddle engine instances with that typed capability. | PASS |
| No hidden cycle restoration | Server contract is in `src/core`; net implements it and engine consumes it. Fresh audit remains at `0` forbidden edges. | PASS |

## Package evidence

| Package | Formal status | Basis |
|---|---|---|
| 3-A | EXIT | Fresh audit: all three GOAL cycle groups are zero. |
| 3-B | DONE | Settings repository hygiene; final Gate import check above. |
| 3-C | EXIT | Preview JS package review: dedicated security, Markdown, block and editor owners. |
| 3-D | EXIT | Engine/document package review; typed runtime boundaries and neutral document contracts. |
| 3-E | EXIT | Artifact/schema/projection owners are separate; Writer retains only durable transaction shell. |
| 3-F | EXIT | Typed Paddle-VL injection plus deletion of global AppHost service locator. |

## Verification

- `build.bat --cmake --stop-running`: PASS.
- `ctest -L hermetic`: `68/68` PASS. The removed test was the obsolete AppHost facade target; no behavior target was added or skipped.
- `scripts/architecture_audit.ps1`: PASS; first-party `100675`, Screenshot family `30116`, product `.cpp=124`, class-method `.inl=0`, forbidden edges `0`.
- No actual Overlay, WebView, model or network runtime was launched.

## Guardrails

- Keep all three audited cycle groups at zero.
- Do not recreate `AppHost`, global path registry, non-owning feature registry or a callback facade.
- Preserve `Settings.cpp` UI/net/engine include hygiene.
- Stage 4 work must preserve Screenshot family `<=30640`, source ceiling `<=101060`, and all Stage 2 no-return rules.

## Next work

Begin one Stage 4 delivery batch: `4-A` production asset ownership. Then complete
`4-B` model registry/dry-run and `4-C` workspace/canonical-document hygiene before
the final DoD audit.
