#include "ocr/layout/PaddleDocLayoutProfile.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << "\n";
    return 1;
}

bool Near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main() {
    if (ResolveLayoutModelFamily(L"auto", L"C:\\models\\PP-DocLayoutV3.onnx") !=
        LayoutModelFamily::PPDocLayoutV3) {
        return Fail("auto V3 family resolution");
    }
    if (ResolveLayoutModelFamily(L"v2", L"custom.onnx") !=
        LayoutModelFamily::PPDocLayoutV2) {
        return Fail("explicit V2 family resolution");
    }
    if (ResolveLayoutModelFamily(L"auto", L"custom.onnx") !=
        LayoutModelFamily::Unknown) {
        return Fail("unknown family must not be guessed");
    }

    const auto v3 = BuildPaddleDocLayoutProfile(
        LayoutModelFamily::PPDocLayoutV3,
        LayoutThresholdProfile::Official);
    if (!v3.polygonExpected || v3.rectMode || v3.legacyPostprocess) {
        return Fail("V3 capability flags");
    }
    for (int classId = 0; classId < 25; ++classId) {
        if (!Near(PaddleDocClassThreshold(v3, classId), 0.30f, 1e-6)) {
            return Fail("V3 official threshold must be scalar 0.30");
        }
    }
    if (PaddleDocScorePasses(v3, 22, 0.30f)) {
        return Fail("V3 threshold comparison must be strict");
    }
    if (!PaddleDocScorePasses(v3, 22, 0.307f) ||
        PaddleDocScorePasses(v3, 22, 0.299f)) {
        return Fail("V3 threshold boundary");
    }
    if (!Near(v3.nmsSameClass, 0.60f, 1e-6) ||
        !Near(v3.nmsCrossClass, 0.98f, 1e-6) ||
        v3.minBoxEdge != 6 ||
        !Near(v3.landscapeImageAreaMax, 0.82f, 1e-6) ||
        !Near(v3.portraitImageAreaMax, 0.93f, 1e-6)) {
        return Fail("V3 official constants");
    }

    const auto v2 = BuildPaddleDocLayoutProfile(
        LayoutModelFamily::PPDocLayoutV2,
        LayoutThresholdProfile::Official);
    for (int classId : { 5, 6, 15, 17, 22, 23 }) {
        if (!Near(PaddleDocClassThreshold(v2, classId), 0.40f, 1e-6)) {
            return Fail("V2 0.40 class map");
        }
    }
    if (!Near(PaddleDocClassThreshold(v2, 20), 0.45f, 1e-6) ||
        !Near(PaddleDocClassThreshold(v2, 11), 0.50f, 1e-6)) {
        return Fail("V2 seal/formula_number thresholds");
    }
    if (v2.polygonExpected || !v2.rectMode) {
        return Fail("V2 must be a normal rect-capable profile");
    }

    for (int classId : { 3, 5, 6, 15, 17 }) {
        if (PaddleDocMergeBboxMode(classId) != LayoutMergeBboxMode::Large) {
            return Fail("large outer-class map");
        }
    }
    if (PaddleDocMergeBboxMode(22) != LayoutMergeBboxMode::Keep) {
        return Fail("text union means keep");
    }

    if (PaddleDocRoundNearestEven(10.4) != 10 ||
        PaddleDocRoundNearestEven(10.5) != 10 ||
        PaddleDocRoundNearestEven(11.5) != 12 ||
        PaddleDocRoundNearestEven(10.6) != 11 ||
        PaddleDocRoundNearestEven(-1.5) != -2 ||
        PaddleDocRoundNearestEven(-2.5) != -2) {
        return Fail("nearest-even rounding");
    }

    if (!ValidatePaddleDocBboxCount(0, 300, 300, true) ||
        !ValidatePaddleDocBboxCount(300, 300, 300, true) ||
        ValidatePaddleDocBboxCount(-1, 300, 300, true) ||
        ValidatePaddleDocBboxCount(301, 300, 300, true) ||
        ValidatePaddleDocBboxCount(10, 300, 9, true) ||
        !ValidatePaddleDocBboxCount(10, 300, 0, false)) {
        return Fail("bbox_num prefix validation");
    }

    // Inclusive NMS sees a shared boundary pixel while the later geometric
    // overlap helpers intentionally treat the same edge as zero area.
    const double inclusive = PaddleDocInclusiveIou(0, 0, 10, 10, 10, 0, 20, 10);
    const double exclusive = PaddleDocExclusiveSmallOverlap(0, 0, 10, 10, 10, 0, 20, 10);
    if (!(inclusive > 0.0) || !Near(exclusive, 0.0)) {
        return Fail("inclusive NMS and exclusive overlap must remain distinct");
    }
    if (!Near(PaddleDocInclusiveIou(0, 0, 0, 0, 0, 0, 0, 0), 1.0)) {
        return Fail("inclusive 1px box IoU");
    }
    if (!Near(PaddleDocInnerCoverage(0, 0, 10, 10, 0, 0, 9, 10), 0.90)) {
        return Fail("large containment 0.90 boundary");
    }

    std::cout << "Paddle Doc layout profile contract passed.\n";
    return 0;
}
