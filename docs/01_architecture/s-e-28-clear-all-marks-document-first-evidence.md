# S-E-28 evidence: ClearAllMarks Document-first residual (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-28  
Prior: S-E-27 `d43aef72`

## Intent

**Ownership domain (single slice):** `ConfigClearAllMarks` Document-first. Document clear sole for existence; Host vector is GDI projection after Document. Delete history snapshots from Document product-read (CaptureBeforeSnapshot). Net-delete Host clear + post DocumentClear dual order + Host convert delete snaps.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Host `clear` then DocumentClear | DocumentClear first → Host clear |
| ClearAllMarks delete snaps Host convert | CaptureBeforeSnapshot Document product-read |

## Product-read / create contract

1. Capture delete snaps from Document (before clear) for each Host annotation
2. DocumentClear first
3. Host vector clear projection
4. clear select / text editing / pending text

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — ConfigClearAllMarks

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| ClearAllMarks Document-first | **on** |
| Stage 2 code commits | **~76** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: ClearAllMarks Document-first + delete snaps product-read. Not helper-only. Complements product delete Document-first (S-E-27). Live modify still Host mutate + DocumentReplace after.

## NEXT

Live modify Document-first residual or Geometry/Arrow ownership vertical deepen under over-警戒 discipline. 合域强制. §11.5 package exit still NOT closed. Host vector delete still blocked.
