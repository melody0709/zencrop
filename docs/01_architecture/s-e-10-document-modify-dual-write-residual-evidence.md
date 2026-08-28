# S-E-10 evidence: Document dual-write residual modify commits (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-10  
Prior: residual inventory `9d6c31c4` / S-E-9 `e86873bd`

## Intent

**Ownership domain (single slice):** Close residual dual-write hole after S-E-9 cutover — product **in-place modify** paths mutated Host vector + history but did **not** dual-write DocumentReplace. Not a new TTL chain; residual of Document dual-write domain.

## Deleted dual authority gap

| Path | Before | After |
|---|---|---|
| text commit (non-remove) | Host only | + DocumentReplace |
| move annotation mouse-up | Host + history | + DocumentReplace |
| rotate annotation mouse-up | Host + history | + DocumentReplace |
| resize annotation mouse-up | Host + history | + DocumentReplace |
| ApplyActiveScreenshotStyleToSelection | Host + history | + DocumentReplace |
| watermark content dialog modify | Host + history | + DocumentReplace |
| watermark ensure+text path | Host only | + DocumentReplace |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl`
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl`

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Live product scan: every `pushModify` paired with `DocumentReplaceFromLegacy`.

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| pushModify without DocumentReplace | **0** |
| product mutation full rebuild | **0** |
| Stage 2 code commits | **~49** (过警戒 45；合域强制；硬停 55) |

## Granularity note

One domain: all residual in-place modify dual-write holes. Not per-site slices.

## NEXT

S-E package-exit partial (infra + dual-write residual closed) or Geometry/Arrow vertical under 合域强制. Host vector still GDI sole; Document product reads still 0; §11.5 NOT closed.
