# Stage 2 formal Gate review — PASS

Date: 2026-07-24  
Review source anchor: `0b7dde59` — `refactor(arch): share annotation content dispatcher`  
Review docs head: `722bd105` (no `src/` change after the source anchor)

## Verdict

**Stage 2 Gate: PASS.** This is a formal Gate result, not a conditional result and
not a claim about Stage 3 or Stage 4.

Stage 3 remains **IN PROGRESS**: only 3-A and 3-B are done, 3-F is partial, and
3-C / 3-D / 3-E remain to be implemented. Stage 4 remains **BLOCKED** until the
Stage 3 formal Gate passes.

## Current evidence

| Gate condition | Current proof | Verdict |
|---|---|---|
| One annotation runtime authority | `m_annotationProjection` and `RebuildHostProjection` have zero production hits. `AnnotationDocument` is the committed model; `AnnotationEditSession` is the one active draft. | PASS |
| Shared Preview / Export renderer | Both consumers call `ScreenshotAnnotationRenderContentLocal` and `ScreenshotAnnotationRenderContentHighLightsLocal`; that owner contains vector and special-tool dispatch, coordinate/source-pixel policy, and Export phase ordering. | PASS |
| Toolbar single owner chain | Catalog, model, layout, render, hit-test, panel, color, slider, text/watermark and history mutations are outside the Host. The residual command path satisfies ADR-004. | PASS |
| Host residual is bounded | Fresh audit: `ToolbarRender / ToolbarCommand / AnnotationRender / Overlay MessageHandler = 166 / 368 / 312 / 948`, exactly within ADR-004 ceilings. `HandleScreenshotLButtonDown = 642` is event → hit → Document/EditSession transaction → capture/focus sequencing; its attempted policy extraction was rejected because it needs callback re-entry. | PASS |
| No production class-method `.inl` | Fresh audit: `0`. | PASS |
| Pixel / performance evidence | ADR-005 accepts real production renderer DIB/HDC goldens at 96/120/144/192 DPI and its warmup-5 / 31-sample P95 path. Full Overlay attachment is deliberately optional. | PASS |
| Family health | Fresh audit: Screenshot family `30137 <= 30640`; no current Color exception is in use. | PASS |

The source-anchor validation recorded `build`, hermetic `69/69`, and audit green.
The latest `LastTest.log` also records the existing test suite passing against this
source state. This review adds no target, test hook, or Host API and does not run
an actual Overlay window.

## Package review

| Package | Formal status | Basis |
|---|---|---|
| S-A | EXIT | ADR-005 renderer evidence |
| S-B | EXIT | former live Sync/dual-write state is sole on `EditorState` |
| S-C / S-G | EXIT | command classification plus catalog/model/layout/hit ownership is sole; remaining dialog/capture shell is ADR-004 Host work |
| S-D / S-F | EXIT | shared annotation-content dispatcher deleted Preview/Export duplicate consumers |
| S-E | EXIT | projection member and rebuild pipeline are deleted |
| S-H | EXIT | render and interaction ownership left the Host; remaining HWND/GDI lifecycle is bounded by ADR-004 |

## Guardrails that remain active

- `m_annotationProjection` and `RebuildHostProjection` must stay at zero.
- The four ADR-004 Host ceilings and Screenshot-family `<= 30640` remain hard
  no-growth checks. A growth or a business-owner return to Host triggers a direction
  review; it does not reopen arbitrary Stage 2 helper work.
- Do not introduce a callback facade, Host re-entry, test-only Host API, private
  access hack, or new Host test target to make the former residual look smaller.
- The eight remaining net/engine and batch/document include edges belong to Stage 3;
  they are not an unreviewed Stage 2 failure.

## Next work

Start exactly one Stage 3 delivery batch: **3-C Preview JS ownership mapping →
prefill → vertical cutover**. Mapping/prefill are not a completion point. No Stage 4
work may begin before the independent Stage 3 formal Gate passes.
