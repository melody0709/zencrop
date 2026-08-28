# S-E residual inventory: Host pure dual-authority methods complete

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E residual inventory (post S-E-4 `7cc49fac`)

## Intent

Inventory residual Host dual authority after S-E-1..4. Distinguish:
1. **dual-authority pure methods/mutations** (must delete for S-E progress) — target **0**
2. **Host runtime/event/GDI/settings I/O** — legit Host ownership until Document vertical cutover
3. **Document ownership + stable-id selection** — research §11.5 structural; multi-slice vertical work

## Dual-authority cutover complete (S-E-1..4)

| Slice | Deleted Host dual | Sole pure |
|---|---|---|
| S-E-1 | Host undo find/remove/insert/replace lambdas | `AnnotationLegacyDocument.h` mutations by id |
| S-E-2 | `SetSelectedScreenshotAnnotationIndex` | `ScreenshotEditorSetAnnotationCountAndSelect` |
| S-E-3 | `SetActiveScreenshotTool` | `ScreenshotEditorSelectToolWithHistory` |
| S-E-4 | 6× `Is*ColorTargetActive` + `MarkScreenshotToolSettingsDirty` | pure `ScreenshotEditorIs*ColorTargetActive` + `SyncToolSettingsDirty` |

Live product scan:
- Host pure-predicate color-target methods: **0**
- Host SetSelected / SetActiveTool: **0**
- Host undo mutation dual lambdas: **0** (thin projectors only)

## Remaining Host surfaces (runtime — not dual authority)

| Category | Surface | Why Host / later |
|---|---|---|
| Annotation runtime vector | `m_screenshotAnnotations` | Host collection; Document dual-write not started |
| History | `m_annotationHistory` + `m_annotationModifyBefore` | Host undo stacks; apply via pure mutations |
| Selection | pure `selectedAnnotationIndex` | index sole; stable id dual-write not started |
| Event handlers | LButton/MouseMove/Key/Char/Wheel | Host HWND session |
| Render/hit-test | Draw/HitTest/Bounds/Cursor | Host GDI; S-F renderer |
| Settings I/O | Load/Save/Flush dirty | Host disk; Flush keeps Save side-effect |
| Style apply | Apply/Load style, SetCustomColor, SetColorIndex | Host orchestration + pure style fields |
| Toolbar | Draw/HitTest/Handle/Run command | Host UI |

## Research §11.5 acceptance checklist (pragmatic status)

| Criterion | Status |
|---|---|
| Document holds items | **partial** — AnnotationModel exists (tests); Host vector still runtime sole |
| active/selected stable id | **partial** — history uses ids; selection still index |
| Document add/remove/replace/find/order | **partial** — pure legacy mutations + Model; not Host Document owner |
| History acts on Document | **partial** — history entries by id; apply mutates Host vector via pure |
| vector index short-lifecycle only | **NOT** — selection still long-lived index |
| selected index API gradually delete | **progress** — Host SetSelected method gone; index field remains |
| Legacy struct → migration namespace | **deferred** — still product primary type |
| RenderContext / renderer registry | **deferred** — S-F |
| Tool group vertical cutover | **NOT STARTED** — Geometry…AutoMosaic groups |
| No new fields on legacy struct | **policy** — enforce going forward |

## KPI

| Metric | After S-E-4 |
|---|---:|
| hermetic | **59/59** |
| Host pure dual-authority methods (S-E scope) | **0** |
| Stage 2 code commits | **~43** (near 警戒 45；合域强制) |
| OverlayWindow.h phys | **~230** (decl shrink via method deletes) |

## S-E package status

- **Host pure dual-authority method cutover: DONE** (S-E-1..4 + residual inventory)
- **Full S-E package exit (research §11.5 Document + vertical tool groups):** NOT closed — requires Document dual-write + stable-id selection + tool-group vertical chains + independent review
- **Next:** S-E-5 Document dual-write seed (Host hold AnnotationModel/Document + create/delete dual-write) **or** S-F renderer only after Document seam exists; prefer Document ownership next. Stage2 near 警戒 — **合域强制**.

## NEXT

S-E-5: AnnotationDocument ownership seam — Host holds Document dual-write on create/delete/clear; or residual inventory closed and open Document seed domain. **Must prefill domain list before src edits.**
