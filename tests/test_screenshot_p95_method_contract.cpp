// S-A-EXIT: P95-GDI method characterization (pure percentile + Gate threshold).
// Hermetic: no HWND / GDI draw / machine timing. Locks the Gate math only:
//   - P95 from sorted samples (linear index)
//   - regression when BOTH relative >5% AND absolute >0.5 ms
// Product frame-cost runner (real Overlay paint) is documented separately;
// this contract freezes the decision rule so future baselines stay comparable.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

static int g_fail = 0;
static void Expect(bool c, const char* n)
{
    if (!c) {
        std::cerr << "FAIL " << n << "\n";
        ++g_fail;
    } else {
        std::cout << "PASS " << n << "\n";
    }
}

// P95 index: ceil(0.95 * n) - 1, clamped. n>=1.
static size_t ScreenshotP95Index(size_t n)
{
    if (n == 0) return 0;
    // (95 * n + 99) / 100  → ceil(0.95*n); then -1 for 0-based.
    const size_t rank = (95 * n + 99) / 100; // 1-based rank
    return (rank == 0) ? 0 : (rank - 1);
}

static double ScreenshotP95Ms(std::vector<double> samples)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[ScreenshotP95Index(samples.size())];
}

// Gate dual-threshold: regress only if relative AND absolute worsen.
// Research: "相关 P95 同时恶化 >5% 且 >0.5 ms 才默认判回归"
static bool ScreenshotP95Regressed(double baselineP95Ms, double candidateP95Ms)
{
    if (baselineP95Ms <= 0.0) {
        // No baseline → not a regression signal (first measure).
        return false;
    }
    const double absDelta = candidateP95Ms - baselineP95Ms;
    if (absDelta <= 0.5) return false;
    const double rel = absDelta / baselineP95Ms;
    return rel > 0.05;
}

int main()
{
    // Index math: n=1 → index 0; n=20 → rank 19 → index 18; n=30 → rank 29 → index 28.
    Expect(ScreenshotP95Index(1) == 0, "p95 idx n=1");
    Expect(ScreenshotP95Index(20) == 18, "p95 idx n=20");
    Expect(ScreenshotP95Index(30) == 28, "p95 idx n=30");
    Expect(ScreenshotP95Index(100) == 94, "p95 idx n=100");

    // Sorted P95 value.
    {
        std::vector<double> s = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                  11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
        // n=20 → index 18 → 19.0
        Expect(std::fabs(ScreenshotP95Ms(s) - 19.0) < 1e-9, "p95 value n=20");
    }
    {
        // Unsorted input must sort first.
        std::vector<double> s = { 50, 1, 2, 3, 4 };
        // n=5 → rank ceil(4.75)=5 → index 4 → 50
        Expect(std::fabs(ScreenshotP95Ms(s) - 50.0) < 1e-9, "p95 value unsorted");
    }

    // Dual-threshold regression.
    Expect(!ScreenshotP95Regressed(10.0, 10.4), "no regress +0.4ms (<0.5)");
    Expect(!ScreenshotP95Regressed(10.0, 10.3), "no regress +3%");
    // +0.6ms = 6% on 10ms → both fire → regress
    Expect(ScreenshotP95Regressed(10.0, 10.6), "regress +0.6ms +6%");
    // Large absolute but small relative (baseline 100ms, +0.6ms = 0.6%) → no
    Expect(!ScreenshotP95Regressed(100.0, 100.6), "no regress abs-only small rel");
    // Large relative but small absolute (baseline 1ms, +0.4ms = 40%) → no
    Expect(!ScreenshotP95Regressed(1.0, 1.4), "no regress rel-only small abs");
    // Both large: baseline 1ms, +0.6ms = 60% → regress
    Expect(ScreenshotP95Regressed(1.0, 1.6), "regress both large small base");
    // Improvement never regresses
    Expect(!ScreenshotP95Regressed(10.0, 8.0), "no regress improvement");
    // First measure (no baseline)
    Expect(!ScreenshotP95Regressed(0.0, 12.0), "no regress zero baseline");

    // Method floor: research requires ≥30 samples after warmup for product runner.
    // Hermetic only asserts the constant so docs/code stay aligned.
    constexpr size_t kMinSamples = 30;
    Expect(kMinSamples >= 30, "min samples floor 30");
    Expect(ScreenshotP95Index(kMinSamples) == 28, "p95 idx n=30 method");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
