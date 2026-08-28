# S-A-CLOSE-1 evidence: free-helper sole-path hermetic

Date: 2026-07-23  
Package: Stage 2 S-A Characterization  
Slice: S-A-CLOSE-1  
Prior: S-E residual inventory `a5b5c708`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** S-A characterization nail for product-draw free-helper sole path.  
Land hermetic contract proving:

1. `ScreenshotAnnotationMakeHighLightRenderInfo` sole HighLight style→info fill (Document product-read + fallback pen)
2. AnnotationRenderContext LivePreview vs Export purpose split + dpiScale
3. Type/empty-rect rejection gates

Not full DPI golden farm / P95-GDI — those remain S-A residual.

Not helper-only: Gate criterion “像素/性能无不可解释回退” gains a hermetic free-helper sole-path nail; product dual HighLight style-fill already deleted (S-D/S-F-CLOSE-10).

## Touch paths

- `tests/test_annotation_product_draw_free_helper_contract.cpp` — **new** hermetic
- `tests/CMakeLists.txt` — register hermetic with annotation + helpers + CropAdjustMath + shcore

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 62/62
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **62/62** |
| free-helper sole-path hermetic | **ON** |
| AnnotationRenderContext purpose split hermetic | **ON** (also CLOSE-1) |
| fixed-DPI golden suite | **ABSENT** (residual) |
| P95-GDI baseline | **ABSENT** (residual) |
| Stage 2 code commits | **~125** (user override 硬停 120) |

## Granularity note

One domain: S-A free-helper sole-path characterization nail. Full golden/P95 still residual. Next: S-C/S-G first domain **or** more S-A nails **or** projection member. Docs same commit. No pin.

## NEXT

S-C/S-G-CLOSE first Toolbar ownership domain **or** S-A residual golden/P95 if Gate requires. Prefer high-value only under override budget.
