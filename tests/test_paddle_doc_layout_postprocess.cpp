#include "ocr/layout/PaddleDocLayoutPostprocess.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

void ExpectNear(double actual, double expected, double epsilon, const std::string& message) {
    if (std::abs(actual - expected) > epsilon) {
        Fail(message + " actual=" + std::to_string(actual) +
            " expected=" + std::to_string(expected));
    }
}

PaddleDocLayoutCandidate Box(
    int classId,
    float score,
    double left,
    double top,
    double right,
    double bottom,
    size_t queryIndex,
    int order = 0)
{
    PaddleDocLayoutCandidate candidate;
    candidate.classId = classId;
    candidate.confidence = score;
    candidate.left = left;
    candidate.top = top;
    candidate.right = right;
    candidate.bottom = bottom;
    candidate.queryIndex = queryIndex;
    candidate.readingOrder = order;
    return candidate;
}

PaddleDocPostprocessOptions Options(
    LayoutModelFamily family = LayoutModelFamily::PPDocLayoutV3,
    int width = 1000,
    int height = 1000)
{
    PaddleDocPostprocessOptions options;
    options.profile = BuildPaddleDocLayoutProfile(
        family, LayoutThresholdProfile::Official);
    options.imageWidth = width;
    options.imageHeight = height;
    return options;
}

std::vector<PaddleDocLayoutCandidate> Run(
    std::vector<PaddleDocLayoutCandidate> candidates,
    PaddleDocPostprocessOptions options,
    PaddleDocPostprocessStats* stats = nullptr)
{
    return PostprocessPaddleDocLayoutCandidates(
        std::move(candidates), {}, options, stats);
}

void TestStrictThresholdAndPolicyTiming() {
    PaddleDocPostprocessStats stats;
    auto result = Run({
        Box(4, 0.300f, 0, 0, 100, 30, 0),
        Box(4, 0.307f, 0, 50, 100, 80, 1),
        Box(18, 0.900f, 0, 100, 100, 130, 2),
    }, Options(), &stats);
    Expect(result.size() == 1, "strict V3 threshold and reference removal");
    Expect(result[0].classId == 4, "content survives detector policy timing");
    Expect(stats.scorePassed == 2, "only score > 0.30 passes");
    Expect(stats.removedReference == 1, "reference removed in pipeline filter");
}

void TestNmsBoundariesAndTiePolicy() {
    auto options = Options();
    PaddleDocPostprocessStats stats;
    auto sameEquality = Run({
        Box(22, 0.9f, 0, 0, 99, 99, 1),
        Box(22, 0.8f, 0, 0, 59, 99, 2),
    }, options, &stats);
    Expect(stats.nmsKept == 1, "same-class inclusive IoU == 0.60 is suppressed");
    Expect(sameEquality.size() == 1, "same-class equality leaves one box");

    auto sameBelow = Run({
        Box(22, 0.9f, 0, 0, 99, 99, 1),
        Box(22, 0.8f, 0, 0, 58, 99, 2),
    }, options, &stats);
    Expect(stats.nmsKept == 2, "same-class inclusive IoU below 0.60 is retained");
    Expect(sameBelow.size() == 2, "V3 degraded polygon path conservatively keeps overlap");

    auto crossEquality = Run({
        Box(22, 0.9f, 0, 0, 99, 99, 1),
        Box(23, 0.8f, 0, 0, 97, 99, 2),
    }, options, &stats);
    Expect(stats.nmsKept == 1, "cross-class inclusive IoU == 0.98 is suppressed");
    Expect(crossEquality.size() == 1, "cross-class equality leaves one box");

    auto tied = Run({
        Box(22, 0.8f, 200, 0, 300, 100, 1),
        Box(22, 0.8f, 200, 0, 300, 100, 9),
    }, options, &stats);
    Expect(tied.size() == 1 && tied[0].queryIndex == 9,
        "exact-score tie uses queryIndex descending");
    Expect(stats.exactScoreTies == 1, "exact tie metric is deterministic");
}

void TestImageAreaFilter() {
    auto options = Options(LayoutModelFamily::PPDocLayoutV3, 100, 50);
    options.profile.layoutNms = false;

    auto equality = Run({
        Box(14, 0.9f, 0, 0, 82, 50, 0),
        Box(22, 0.9f, 90, 0, 100, 10, 1),
    }, options);
    Expect(equality.size() == 2, "landscape image area == 82% is retained");

    auto over = Run({
        Box(14, 0.9f, 0, 0, 83, 50, 0),
        Box(22, 0.9f, 90, 0, 100, 10, 1),
    }, options);
    Expect(over.size() == 1 && over[0].classId == 22,
        "landscape image area above 82% is removed");

    auto clamped = Run({
        Box(14, 0.9f, -10, 0, 82, 50, 0),
        Box(22, 0.9f, 90, 0, 100, 10, 1),
    }, options);
    Expect(clamped.size() == 2, "image area is measured after page clamp");

    auto singleton = Run({ Box(14, 0.9f, 0, 0, 100, 50, 0) }, options);
    Expect(singleton.size() == 1, "singleton oversized image bypasses filter");

    auto restore = Run({
        Box(14, 0.9f, 0, 0, 100, 50, 0),
        Box(14, 0.8f, -10, -10, 110, 60, 1),
    }, options);
    Expect(restore.size() == 2, "empty oversized-image result restores prior list");
}

void TestClassModeContainment() {
    auto options = Options();
    options.profile.layoutNms = false;

    auto equality = Run({
        Box(3, 0.9f, 0, 0, 90, 100, 0),
        Box(22, 0.8f, 0, 0, 100, 100, 1),
    }, options);
    Expect(equality.size() == 1 && equality[0].classId == 3,
        "large outer class deletes inner at exact 0.90 coverage");

    auto below = Run({
        Box(3, 0.9f, 0, 0, 89, 100, 0),
        Box(22, 0.8f, 0, 0, 100, 100, 1),
    }, options);
    Expect(below.size() == 2, "large outer class retains inner below 0.90 coverage");

    auto unionKeep = Run({
        Box(22, 0.9f, 0, 0, 100, 100, 0),
        Box(23, 0.8f, 0, 0, 80, 80, 1),
    }, options);
    Expect(unionKeep.size() == 2, "union/keep class does not run containment removal");
}

void TestReferenceMinEdgeAndInlineFormula() {
    auto options = Options();
    options.profile.layoutNms = false;
    PaddleDocPostprocessStats stats;
    auto edges = Run({
        Box(18, 0.9f, 0, 0, 20, 20, 0),
        Box(22, 0.9f, 30, 0, 36, 6, 1),
        Box(22, 0.9f, 50, 0, 55, 6, 2),
    }, options, &stats);
    Expect(edges.size() == 1 && edges[0].queryIndex == 1,
        "edge == 6 retained while edge < 6 and reference are removed");
    Expect(stats.removedReference == 1 && stats.removedMinEdge == 1,
        "reference/min-edge reasons remain separate");

    auto inlineText = Run({
        Box(15, 0.9f, 0, 0, 100, 20, 0),
        Box(22, 0.9f, 40, 0, 120, 20, 1),
    }, options, &stats);
    Expect(inlineText.size() == 1 && inlineText[0].classId == 22,
        "inline formula is removed when bbox small-overlap > 0.5");

    auto inlinePair = Run({
        Box(15, 0.9f, 0, 0, 100, 20, 0),
        Box(15, 0.8f, 40, 0, 120, 20, 1),
    }, options, &stats);
    Expect(inlinePair.empty(), "two overlapping inline formulas are both removed");

    auto boundary = Run({
        Box(15, 0.9f, 0, 0, 100, 20, 0),
        Box(22, 0.9f, 50, 0, 150, 20, 1),
    }, options);
    Expect(boundary.size() == 2, "inline overlap == 0.5 is retained");
}

void TestProtectedMatrixAndDegradedMode() {
    auto v2 = Options(LayoutModelFamily::PPDocLayoutV2);
    v2.profile.layoutNms = false;

    auto protectedPair = Run({
        Box(14, 0.9f, 0, 0, 100, 100, 0),
        Box(3, 0.8f, 0, 0, 80, 80, 1),
    }, v2);
    Expect(protectedPair.size() == 2, "image+chart protected pair is retained");

    auto tableProtected = Run({
        Box(21, 0.9f, 0, 0, 100, 100, 0),
        Box(14, 0.8f, 0, 0, 80, 80, 1),
    }, v2);
    Expect(tableProtected.size() == 2, "table+protected class pair is retained");

    auto tableText = Run({
        Box(21, 0.9f, 0, 0, 100, 100, 0),
        Box(22, 0.8f, 0, 0, 80, 80, 1),
    }, v2);
    Expect(tableText.size() == 1 && tableText[0].classId == 21,
        "table+ordinary class is not protected");

    auto v3 = Options();
    v3.profile.layoutNms = false;
    PaddleDocPostprocessStats stats;
    auto degraded = Run({
        Box(22, 0.9f, 0, 0, 100, 100, 0),
        Box(23, 0.8f, 0, 0, 80, 80, 1),
    }, v3, &stats);
    Expect(degraded.size() == 2, "V3 missing masks conservatively keeps general overlap");
    Expect(stats.v3PolygonDegraded && stats.polygonFallbacks == 2,
        "V3 missing masks reports explicit degraded mode");
    Expect(!stats.polygonRuntimeAvailable,
        "OpenCV-disabled target reports polygon runtime unavailable");
}

void TestPolygonGeometryAndOffsets() {
    const std::vector<PaddleDocPointF> concave = {
        {0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4},
    };
    const std::vector<PaddleDocPointF> unit = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
    };
    double ratio = 0.0;
    Expect(PaddleDocPolygonSmallOverlap(concave, unit, ratio),
        "concave polygon overlap is supported by Clipper2");
    ExpectNear(ratio, 1.0, 1e-9, "unit square is fully inside concave polygon");

    const std::vector<PaddleDocPointF> bowTie = {
        {0, 0}, {2, 2}, {0, 2}, {2, 0},
    };
    Expect(!PaddleDocPolygonSmallOverlap(bowTie, unit, ratio),
        "self-intersecting ring enters topology fallback");

    auto options = Options();
    options.offsetX = 7;
    options.offsetY = 11;
    options.readingOrderBase = 100;
    auto offset = Run({ Box(22, 0.9f, 1, 2, 20, 30, 0, 3) }, options);
    Expect(offset.size() == 1, "offset fixture survives");
    Expect(offset[0].left == 8 && offset[0].top == 13 &&
        offset[0].right == 27 && offset[0].bottom == 41,
        "tile offset is applied to bbox exactly once");
    Expect(offset[0].polygon.size() == 4 &&
        offset[0].polygon[0].x == 8 && offset[0].polygon[0].y == 13,
        "tile offset is applied to polygon exactly once");
    Expect(offset[0].readingOrder == 103, "reading-order base is applied once");
}

} // namespace

int main() {
    TestStrictThresholdAndPolicyTiming();
    TestNmsBoundariesAndTiePolicy();
    TestImageAreaFilter();
    TestClassModeContainment();
    TestReferenceMinEdgeAndInlineFormula();
    TestProtectedMatrixAndDegradedMode();
    TestPolygonGeometryAndOffsets();
    std::cout << "Paddle Doc layout postprocess contract passed.\n";
    return 0;
}
