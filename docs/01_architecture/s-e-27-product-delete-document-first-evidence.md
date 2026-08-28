# S-E-27 evidence: product delete Document-first (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-27  
Prior: residual inventory `16a86f1e` / S-E-26 `bc88bf63`

## Intent

**Ownership domain (single slice):** Product **delete** Document-first. `DeleteSelectedScreenshotAnnotation` + empty-text erase in `CommitScreenshotTextEdit` use Document remove sole for existence; Host vector is GDI projection after Document. Net-delete Host-first `erase` + post DocumentRemove dual order.

Also delete-history snapshot from Document product-read (S-E-21 CaptureBeforeSnapshot).

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Host `erase` then DocumentRemoveById (DeleteSelected) | `ScreenshotAnnotationDocumentRemoveAndProject` Document first |
| Host `erase` then DocumentRemoveById (empty text commit) | DocumentRemoveAndProject Document first |
| delete history snapshot Host convert | CaptureBeforeSnapshot Document product-read |

## Product-read / create contract

1. DeleteSelected: resolve selected → CaptureBeforeSnapshot → pushDelete → DocumentRemoveAndProject → clear select
2. Empty text commit: DocumentRemoveAndProject → project select/textEditing from mutation
3. Host vector remains GDI runtime sole collection

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — DeleteSelected + empty text erase

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| product delete Document-first | **on** |
| Stage 2 code commits | **~75** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: product delete Document-first + delete history snapshot product-read. Not helper-only. Complements history remove Document-first (S-E-26). Live modify still Host mutate + DocumentReplace after (order residual).

## NEXT

Live modify Document-first residual (DocumentReplace before history after-snap already; Host still mutates live), or Geometry/Arrow ownership vertical deepen under over-警戒 discipline. 合域强制. §11.5 package exit still NOT closed. Host vector delete still blocked.
