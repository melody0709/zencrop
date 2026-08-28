#include "ocr/layout/PaddleDocLayoutPostprocess.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kMaskSize = 200;
constexpr size_t kPlaneSize = (size_t)kMaskSize * kMaskSize;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

PaddleDocLayoutCandidate Box(
    int classId,
    double left,
    double top,
    double right,
    double bottom,
    size_t queryIndex,
    int order)
{
    PaddleDocLayoutCandidate candidate;
    candidate.classId = classId;
    candidate.confidence = 0.9f;
    candidate.left = left;
    candidate.top = top;
    candidate.right = right;
    candidate.bottom = bottom;
    candidate.queryIndex = queryIndex;
    candidate.readingOrder = order;
    return candidate;
}

PaddleDocPostprocessOptions Options(int width = 800, int height = 800) {
    PaddleDocPostprocessOptions options;
    options.profile = BuildPaddleDocLayoutProfile(
        LayoutModelFamily::PPDocLayoutV3,
        LayoutThresholdProfile::Official);
    options.imageWidth = width;
    options.imageHeight = height;
    options.profile.layoutNms = false;
    return options;
}

void FillRect(
    std::vector<int32_t>& masks,
    size_t plane,
    int left,
    int top,
    int right,
    int bottom)
{
    left = (std::max)(0, (std::min)(kMaskSize, left));
    top = (std::max)(0, (std::min)(kMaskSize, top));
    right = (std::max)(0, (std::min)(kMaskSize, right));
    bottom = (std::max)(0, (std::min)(kMaskSize, bottom));
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            masks[plane * kPlaneSize + (size_t)y * kMaskSize + x] = 1;
        }
    }
}

PaddleDocMaskTensorView View(const std::vector<int32_t>& masks, size_t count) {
    return { masks.data(), count, kMaskSize, kMaskSize };
}

const PaddleDocLayoutCandidate& ByQuery(
    const std::vector<PaddleDocLayoutCandidate>& candidates,
    size_t queryIndex)
{
    auto found = std::find_if(
        candidates.begin(), candidates.end(),
        [queryIndex](const auto& candidate) {
            return candidate.queryIndex == queryIndex;
        });
    if (found == candidates.end()) Fail("query not found: " + std::to_string(queryIndex));
    return *found;
}

void TestQueryMaskAssociationAndAutoShape() {
    std::vector<int32_t> masks(2 * kPlaneSize, 0);
    FillRect(masks, 0, 0, 0, 50, 50);
    // L-shaped mask in query 1's [100,200)x[100,200) crop.
    FillRect(masks, 1, 105, 105, 190, 140);
    FillRect(masks, 1, 105, 105, 140, 190);

    PaddleDocPostprocessStats stats;
    auto result = PostprocessPaddleDocLayoutCandidates({
        // Reading order deliberately reverses query order.
        Box(23, 400, 400, 800, 800, 1, 0),
        Box(22, 0, 0, 200, 200, 0, 1),
    }, View(masks, 2), Options(), &stats);

    Expect(result.size() == 2, "both synthetic-mask candidates survive");
    const auto& rect = ByQuery(result, 0);
    const auto& shaped = ByQuery(result, 1);
    Expect(rect.polygonFromMask && rect.polygon.size() == 4,
        "full crop normalizes to rect while retaining mask provenance");
    Expect(shaped.polygonFromMask && shaped.polygon.size() > 4,
        "L mask remains a non-rect auto polygon");
    for (const auto& point : shaped.polygon) {
        Expect(point.x >= 400.0f && point.y >= 400.0f,
            "query 1 consumes plane 1 and keeps its coordinate offset");
    }
    Expect(stats.polygonFallbacks == 0 && !stats.v3PolygonDegraded,
        "valid masks do not enter degraded mode");
    Expect(stats.polygonRuntimeAvailable, "OpenCV polygon runtime is reported");
}

void TestLargestExternalContour() {
    std::vector<int32_t> masks(kPlaneSize, 0);
    FillRect(masks, 0, 1, 1, 6, 6);
    FillRect(masks, 0, 50, 50, 150, 150);
    auto result = PostprocessPaddleDocLayoutCandidates(
        { Box(22, 0, 0, 800, 800, 0, 0) },
        View(masks, 1), Options());
    Expect(result.size() == 1 && result[0].polygonFromMask,
        "largest-contour fixture produces a real polygon");
    float minX = 1000.0f;
    float minY = 1000.0f;
    for (const auto& point : result[0].polygon) {
        minX = (std::min)(minX, point.x);
        minY = (std::min)(minY, point.y);
    }
    Expect(minX > 100.0f && minY > 100.0f,
        "small disconnected contour is ignored in favor of largest contour");
}

void TestPolygonAwareOverlap() {
    std::vector<int32_t> separatedMasks(2 * kPlaneSize, 0);
    FillRect(separatedMasks, 0, 0, 0, 40, 100);
    FillRect(separatedMasks, 1, 60, 0, 100, 100);
    auto options = Options();
    auto separated = PostprocessPaddleDocLayoutCandidates({
        Box(22, 0, 0, 400, 400, 0, 0),
        Box(23, 0, 0, 400, 400, 1, 1),
    }, View(separatedMasks, 2), options);
    Expect(separated.size() == 2,
        "bbox overlap 1 with polygon overlap 0 retains both text blocks");

    std::vector<int32_t> coincidentMasks(2 * kPlaneSize, 0);
    FillRect(coincidentMasks, 0, 0, 0, 100, 100);
    FillRect(coincidentMasks, 1, 0, 0, 100, 100);
    PaddleDocPostprocessStats stats;
    auto coincident = PostprocessPaddleDocLayoutCandidates({
        Box(22, 0, 0, 400, 400, 0, 0),
        Box(23, 0, 0, 400, 400, 1, 1),
    }, View(coincidentMasks, 2), options, &stats);
    Expect(coincident.size() == 1 && coincident[0].queryIndex == 0,
        "equal-area bbox+polygon overlap deletes later candidate");
    Expect(stats.removedGeneralOverlap == 1,
        "polygon-confirmed deletion has its own reason metric");
}

void TestEmptyMaskDegradedFallback() {
    std::vector<int32_t> masks(kPlaneSize, 0);
    PaddleDocPostprocessStats stats;
    auto result = PostprocessPaddleDocLayoutCandidates(
        { Box(22, 0, 0, 400, 400, 0, 0) },
        View(masks, 1), Options(), &stats);
    Expect(result.size() == 1 && !result[0].polygonFromMask,
        "empty mask falls back to bbox polygon");
    Expect(result[0].polygon.size() == 4,
        "empty-mask fallback persists a four-point rect");
    Expect(stats.polygonFallbacks == 1 && stats.v3PolygonDegraded,
        "empty mask reports V3 polygon degradation");
}

} // namespace

int main() {
    TestQueryMaskAssociationAndAutoShape();
    TestLargestExternalContour();
    TestPolygonAwareOverlap();
    TestEmptyMaskDegradedFallback();
    std::cout << "Paddle Doc layout mask/polygon contract passed.\n";
    return 0;
}
