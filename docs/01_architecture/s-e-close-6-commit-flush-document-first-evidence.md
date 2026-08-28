# S-E-CLOSE-6 evidence: EditSession commit-flush Document-first

Date: 2026-07-23  
Package: Stage 2 S-E EditSession / projection residual  
Slice: S-E-CLOSE-6  
Prior: S-H-CLOSE-9 `d7ae6aa0`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** EditSession commit-flush dual-write onto projection.  
Delete live-drag / ApplyStyle / text-modify commit paths that assign draft → `m_annotationProjection[i]` **before** Document CommitModify.  
Sole: Document-first CommitModify from draft (local copy) → history → Clear session → RebuildHostProjection.

Not helper-only: projection dual-assign authority at commit flush deleted for live-drag + ApplyStyle + text modify.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| LButtonUp move/rotate/resize: draft → projection[i] → CommitModify | draft local copy → CommitModify → RebuildHostProjection |
| ApplyStyle (non mid-edit): draft → projection[i] → CommitModify | same Document-first + rebuild |
| Text CommitModify (edit existing): draft flush projection → CommitModify | draft local → CommitModify → rebuild |

## Residual (still allowed this slice)

1. Pending-create layout seed: one projection slot write before DocumentReplace + rebuild (create path; rebuild follows)
2. Projection member remains as rebuild cache (read/layout/hit-test Host index) — not deleted this knife
3. ToolbarInteraction watermark text path may still touch projection (inventory residual)

## Product-read / write contract

1. Live-drag mid-edit: draft sole mutate (unchanged S-E-CLOSE-2)
2. Live-drag commit:  
   `ann = HasDraft ? Draft : projection[selected]`  
   `CommitModify(document, ann, …)`  
   `pushModify(before, after)`  
   `Clear(session)`  
   `RebuildHostProjection(document, projection)`
3. ApplyStyle owning transaction: same Document-first + rebuild
4. Projection no longer receives draft assign **before** Document write on these paths

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.cpp` — live-drag commit helper + text commit
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.cpp` — ApplyStyle flush

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| live-drag commit projection dual-assign | **0** |
| ApplyStyle commit projection dual-assign | **0** |
| text modify commit projection dual-assign | **0** |
| Document-first commit + rebuild | **ON** |
| `m_annotationProjection` member | residual rebuild cache |
| Stage 2 code commits | **~123** (user override 硬停 120) |

## Granularity note

One domain: EditSession commit-flush dual-write delete (live-drag + ApplyStyle + text modify).  
Projection member delete is later multi-slice. Next: residual inventory of remaining projection mutates **or** S-A/S-G. Docs same commit. No pin.

## NEXT

S-E residual inventory of remaining projection mutates **or** S-A characterization nails **or** S-C/S-G. Prefer high-value only under override budget.
