# S-E-30 evidence: text commit modify CommitModify residual (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-30  
Prior: S-E-29 `1af1277c`

## Intent

**Ownership domain (single slice):** Residual text modify commit dual pattern in `CommitScreenshotTextEdit`. Switch to `ScreenshotAnnotationDocumentCommitModify` sole. Net-delete DocumentReplace + TakeSnapshotById + convert recovery dual at text modify path.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| text modify DocumentReplace + TakeSnapshot + convert recovery | `ScreenshotAnnotationDocumentCommitModify` |

pendingCreate still DocumentReplace + TakeSnapshot for pushCreate (create path; not modify). Idempotent residual DocumentReplace after text commit kept (covers non-modify paths).

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — text modify commit

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| text commit modify CommitModify | **on** |
| Stage 2 code commits | **~78** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: text modify commit residual dual delete. Complements S-E-29 live modify commit sole. Recovery convert remains only inside CommitModify / CaptureBeforeSnapshot / create recovery.

## NEXT

Geometry/Arrow ownership vertical or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
