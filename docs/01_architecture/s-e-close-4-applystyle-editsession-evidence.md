# S-E-CLOSE-4 evidence: ApplyStyle via EditSession transaction

Date: 2026-07-22  
Package: Stage 2 S-E-CLOSE (EditSession / draft transaction)  
Slice: S-E-CLOSE-4  
Prior: S-E-CLOSE-3 `f0097c47`

## Intent

**Ownership domain (single slice):** `ApplyActiveScreenshotStyleToSelection` via EditSession.  
Selection style-apply no longer mid-mutates `m_annotationProjection` as authority.  
Transaction: BeginModify (seed draft) → mutate draft → flush draft → Document CommitModify → Clear.  
If session already active for same id (text mid-edit), reuse draft and **do not** commit/clear (mid-edit stays open).

Not helper-only: product style-apply write authority cutover.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ApplyStyle `auto& ann = m_annotationProjection[i]` mid-mutate | `AnnotationEditSessionDraft` |
| ApplyStyle local `beforeSnap` field | `AnnotationEditSessionBefore` |
| ApplyStyle immediate projection write authority | draft sole; flush only at commit |

## Product-read / write contract

1. No mid-edit session for selected id:  
   `BeginModify(session, before, &hostAnn)` → mutate draft →  
   `projection[i] = draft` → `CommitModify` → `pushModify(before, after)` → `Clear`
2. Mid-edit session already active for same id:  
   reuse draft; mutate only; **return without Clear** (text commit owns session end)
3. Watermark ensure / create paths unchanged (create authority separate)

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — ApplyActiveScreenshotStyleToSelection

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| ApplyStyle via EditSession | **on** |
| text mid-edit draft | **on** (S-E-CLOSE-3) |
| live-drag draft | **on** (S-E-CLOSE-2) |
| EditSession before owner | **on** (S-E-CLOSE-1) |
| full projection member residual | still present (read/rebuild/create/draw) |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~102** (ADR-003 硬停 120 final) |

## Granularity note

One domain: selection style-apply transaction via EditSession. Complements CLOSE-1..3. Docs same commit. No pin.

## NEXT

S-E-CLOSE-5: residual projection mutate (create/draw freehand path, watermark dialog text) **or** selectedAnnotationIndex/editingTextIndex field delete **or** projection member delete prep after remaining mutate inventory. Prefer stay on EditSession vertical until full projection authority gone.
