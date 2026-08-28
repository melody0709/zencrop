# OCR Dashboard Overlay Baseline

更新日期：2026-07-11

## Harness

`test_dashboard_optimization_contract` uses fixed seeds and three distributions (sparse grid, nested/overlap, polygon-like random geometry) at 100/300 representative and 1000 stress sizes. It compares every cached issue result with the legacy detector and checks id lookup, reading order, duplicate-id policy, CRLF/LF canonicalization, repeated headings, emoji/UTF-16 ranges, stale revisions and invalid ranges.

Reference run: Windows 11 Pro build 26200, AMD Ryzen 7 9700X (8C/16T), AMD Radeon Graphics driver 32.0.21043.12001, x64 Release (`cl 19.44.35217`), warm local process. Times below are gross microseconds from the hermetic model harness.

| Blocks | Distribution | Index build | Legacy issue hot path ×20 | Cached hot path ×20 |
|---:|---|---:|---:|---:|
| 100 | sparse | 105 µs | 856 µs | <1 µs |
| 100 | nested | 7 µs | 12 µs | <1 µs |
| 100 | random | 33 µs | 569 µs | <1 µs |
| 300 | sparse | 320 µs | 6,085 µs | 2 µs |
| 300 | nested | 27 µs | 37 µs | 2 µs |
| 300 | random | 160 µs | 2,823 µs | 2 µs |
| 1000 | sparse | 3,668 µs | 74,538 µs | 7 µs |
| 1000 | nested | 94 µs | 124 µs | 7 µs |
| 1000 | random | 840 µs | 12,168 µs | 7 µs |

## Decision

Stage 1-B passes the pure-model exit condition: the worst 1000-block hot path falls by well over 80%, ordinary paint no longer invokes overlap detection per block, and reading order is not sorted per paint. Hit-test remains linear and actual GDI+ paint still redraws the full image.

The fixture-backed HWND harness also sends 110 synchronous `WM_PAINT` messages (10 warm-up + 100 recorded) against the 300-block Dashboard client: median **5.799 ms**, p95 **6.247 ms**, max **6.837 ms** at the contract window's 1200×750 design geometry. This is a real GDI+/HWND paint measurement, but not a substitute for the pending 4K/mixed-DPI device matrix.

Stage 1-C is **not entered from model data alone**. Spatial grid and base-image/clip caching require recorded HWND paint p95 on the target 1080p/4K display matrix. If representative 100/300-block paint meets 16.7 ms at 1080p and 33.3 ms at 4K, stop; 1000-block stress alone does not justify extra structures.
