# D-C-S2 — History Cache Ownership Pure Boundary Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior: D-C-S1 `5d5cb046` (stable-key selection sole)

## Purpose

Close research §12.3 residual: cache image ownership/reference decision lived inside
`OcrDashboardWindow` methods in `DashboardHistory.cpp`.

Introduce pure `DashboardHistoryCache` so Window only applies `DeleteFileW` under OCR image dir.

## Change

| Item | Detail |
|---|---|
| `DashboardHistoryCache.h` | Pure API: `CanonicalizePath`, `IsPathUnderDirectory`, `NormalizePath`, `CountRefs`, `ShouldDeleteUnreferenced`, `CollectUnreferencedPaths` |
| `DashboardHistoryCache.cpp` | Sole OS canonicalize (`GetFullPathNameW` + `PathCanonicalizeW` + lower) |
| `DeleteCacheImageIfUnreferenced` | Thin: pure ShouldDelete + `DeleteFileW` |
| `DeleteCacheImagesForItems` | Thin: pure CollectUnreferenced + `DeleteFileW` loop |
| `DeleteHistoryItemsByIndices` | Snapshot removed items; post-save delete via `DeleteCacheImagesForItems` (no inline ref loop) |
| Local static path helpers | Thin wrappers → Cache (SameDashboardPath / other call sites unchanged) |
| `test_dashboard_history_cache_contract` | Hermetic: under-dir, free/referenced, ownedCacheFiles protection, exclude index |

### Semantics preserved

1. Single-image unreferenced delete still uses **imagePath-only** ref count (`DashboardHistoryModelCountImageRefs`) — matches legacy `DeleteCacheImageIfUnreferenced`.
2. Multi-item delete considers **imagePath + ownedCacheFiles** for candidates and retained refs.
3. Paths outside OCR image dir never deleted.
4. Save-failure path still restores history before any cache delete.

### Not claimed

- Full D-C package exit (ResultProjection, bulk Window method removal still open).
- Window methods count drop (still ~47; decision bodies thinned, not deleted).

## KPI

| Metric | Before S2 | After S2 |
|---|---:|---:|
| `DashboardHistory.cpp` physical | 2,728 | **2,683** |
| `DashboardHistoryCache.*` | 0 | **147** (h+cpp) |
| Messages / Route / State nonblank | 2416 / 145 / 1335 | unchanged |
| ScreenshotEditorState nonblank | 1467 | unchanged |
| hermetic | 50/50 | **51/51** (+cache contract) |

## Verdict

**D-C still PARTIAL.** Cache ownership pure boundary is real progress toward §12.3
("删除 cache image 的引用/所有权判断独立").

## NEXT

1. D-C-S3: reduce Window methods in `DashboardHistory.cpp` further, or extract ResultProjection.
2. Do not claim D-C package exit until Window methods significantly reduced + ResultProjection + delete-safety contracts green under independent review.
