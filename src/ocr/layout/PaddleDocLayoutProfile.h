#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <string>
#include "core/WideStringUtils.h"

enum class LayoutModelFamily {
    Auto,
    PPDocLayoutV3,
    PPDocLayoutV2,
    Unknown,
};

enum class LayoutThresholdProfile {
    Official,
    Balanced,
    Recall,
};

enum class LayoutMergeBboxMode {
    Keep,
    Large,
    Small,
};

struct PaddleDocLayoutProfile {
    LayoutModelFamily family = LayoutModelFamily::Unknown;
    LayoutThresholdProfile thresholdProfile = LayoutThresholdProfile::Official;
    std::array<float, 25> classThresholds{};
    // Geometry thresholds stay double precision so exact public boundaries
    // (0.60/0.98/0.82/0.93/0.90) do not drift upward after float promotion.
    double nmsSameClass = 0.60;
    double nmsCrossClass = 0.98;
    int minBoxEdge = 6;
    double landscapeImageAreaMax = 0.82;
    double portraitImageAreaMax = 0.93;
    double largeInnerCoverage = 0.90;
    bool strictScoreComparison = true;
    bool layoutNms = true;
    bool polygonExpected = false;
    bool rectMode = true;
    bool legacyPostprocess = false;
};

inline std::wstring PaddleDocLower(std::wstring value) {
    value = WideToLower(std::move(value)); // OWN-79
    return value;
}

inline LayoutModelFamily ParseLayoutModelFamily(const std::wstring& value) {
    std::wstring normalized = PaddleDocLower(value);
    if (normalized.empty() || normalized == L"auto") return LayoutModelFamily::Auto;
    if (normalized == L"pp_doclayout_v3" || normalized == L"pp-doclayoutv3" ||
        normalized == L"v3") {
        return LayoutModelFamily::PPDocLayoutV3;
    }
    if (normalized == L"pp_doclayout_v2" || normalized == L"pp-doclayoutv2" ||
        normalized == L"v2") {
        return LayoutModelFamily::PPDocLayoutV2;
    }
    return LayoutModelFamily::Unknown;
}

inline LayoutModelFamily DetectLayoutModelFamilyFromPath(const std::wstring& modelPath) {
    const std::wstring normalized = PaddleDocLower(modelPath);
    if (normalized.find(L"pp-doclayoutv3") != std::wstring::npos ||
        normalized.find(L"pp_doclayoutv3") != std::wstring::npos ||
        normalized.find(L"doclayoutv3") != std::wstring::npos) {
        return LayoutModelFamily::PPDocLayoutV3;
    }
    if (normalized.find(L"pp-doclayoutv2") != std::wstring::npos ||
        normalized.find(L"pp_doclayoutv2") != std::wstring::npos ||
        normalized.find(L"doclayoutv2") != std::wstring::npos) {
        return LayoutModelFamily::PPDocLayoutV2;
    }
    return LayoutModelFamily::Unknown;
}

inline LayoutModelFamily ResolveLayoutModelFamily(
    const std::wstring& configuredFamily,
    const std::wstring& modelPath)
{
    LayoutModelFamily configured = ParseLayoutModelFamily(configuredFamily);
    if (configured == LayoutModelFamily::PPDocLayoutV3 ||
        configured == LayoutModelFamily::PPDocLayoutV2) {
        return configured;
    }
    if (configured == LayoutModelFamily::Unknown) return LayoutModelFamily::Unknown;
    return DetectLayoutModelFamilyFromPath(modelPath);
}

inline const wchar_t* LayoutModelFamilyName(LayoutModelFamily family) {
    switch (family) {
    case LayoutModelFamily::Auto: return L"auto";
    case LayoutModelFamily::PPDocLayoutV3: return L"pp_doclayout_v3";
    case LayoutModelFamily::PPDocLayoutV2: return L"pp_doclayout_v2";
    default: return L"unknown";
    }
}

inline LayoutThresholdProfile ParseLayoutThresholdProfile(const std::wstring& value) {
    const std::wstring normalized = PaddleDocLower(value);
    if (normalized == L"balanced") return LayoutThresholdProfile::Balanced;
    if (normalized == L"recall") return LayoutThresholdProfile::Recall;
    return LayoutThresholdProfile::Official;
}

inline const wchar_t* LayoutThresholdProfileName(LayoutThresholdProfile profile) {
    switch (profile) {
    case LayoutThresholdProfile::Balanced: return L"balanced";
    case LayoutThresholdProfile::Recall: return L"recall";
    default: return L"official";
    }
}

inline PaddleDocLayoutProfile BuildPaddleDocLayoutProfile(
    LayoutModelFamily family,
    LayoutThresholdProfile thresholdProfile)
{
    PaddleDocLayoutProfile profile;
    profile.family = family;
    profile.thresholdProfile = thresholdProfile;
    profile.classThresholds.fill(0.50f);

    if (family == LayoutModelFamily::PPDocLayoutV3) {
        profile.polygonExpected = true;
        profile.rectMode = false;
        float general = 0.30f;
        float table = 0.30f;
        float title = 0.30f;
        if (thresholdProfile == LayoutThresholdProfile::Balanced) {
            general = 0.30f;
            table = 0.40f;
            title = 0.30f;
        } else if (thresholdProfile == LayoutThresholdProfile::Recall) {
            general = 0.20f;
            table = 0.35f;
            title = 0.25f;
        }
        profile.classThresholds.fill(general);
        if (thresholdProfile != LayoutThresholdProfile::Official) {
            profile.classThresholds[21] = table;
            profile.classThresholds[6] = title;
            profile.classThresholds[17] = title;
        }
        return profile;
    }

    if (family == LayoutModelFamily::PPDocLayoutV2) {
        profile.polygonExpected = false;
        profile.rectMode = true;
        if (thresholdProfile == LayoutThresholdProfile::Official) {
            profile.classThresholds.fill(0.50f);
            for (int classId : { 5, 6, 15, 17, 22, 23 }) {
                profile.classThresholds[(size_t)classId] = 0.40f;
            }
            profile.classThresholds[20] = 0.45f;
        } else if (thresholdProfile == LayoutThresholdProfile::Balanced) {
            profile.classThresholds.fill(0.30f);
            profile.classThresholds[21] = 0.40f;
            profile.classThresholds[6] = 0.30f;
            profile.classThresholds[17] = 0.30f;
        } else {
            profile.classThresholds.fill(0.20f);
            profile.classThresholds[21] = 0.35f;
            profile.classThresholds[6] = 0.25f;
            profile.classThresholds[17] = 0.25f;
        }
        return profile;
    }

    // Unknown custom models keep the legacy parameter/postprocess behavior until
    // the user explicitly identifies a supported family.
    profile.family = LayoutModelFamily::Unknown;
    profile.polygonExpected = false;
    profile.rectMode = true;
    profile.legacyPostprocess = true;
    profile.strictScoreComparison = false;
    if (thresholdProfile == LayoutThresholdProfile::Official) {
        // Preserve the historical "official-like" thresholds only for an
        // explicitly unknown/legacy model. Known V3 never receives this map.
        profile.classThresholds.fill(0.50f);
        for (int classId : { 5, 6, 15, 17, 22, 23 }) {
            profile.classThresholds[(size_t)classId] = 0.40f;
        }
        profile.classThresholds[20] = 0.45f;
    } else if (thresholdProfile == LayoutThresholdProfile::Balanced) {
        profile.classThresholds.fill(0.30f);
        profile.classThresholds[21] = 0.40f;
        profile.classThresholds[6] = 0.30f;
        profile.classThresholds[17] = 0.30f;
    } else if (thresholdProfile == LayoutThresholdProfile::Recall) {
        profile.classThresholds.fill(0.20f);
        profile.classThresholds[21] = 0.35f;
        profile.classThresholds[6] = 0.25f;
        profile.classThresholds[17] = 0.25f;
    }
    return profile;
}

inline float PaddleDocClassThreshold(const PaddleDocLayoutProfile& profile, int classId) {
    if (classId < 0 || classId >= (int)profile.classThresholds.size()) {
        return std::numeric_limits<float>::infinity();
    }
    return profile.classThresholds[(size_t)classId];
}

inline bool PaddleDocScorePasses(
    const PaddleDocLayoutProfile& profile,
    int classId,
    float score)
{
    const float threshold = PaddleDocClassThreshold(profile, classId);
    return profile.strictScoreComparison ? score > threshold : score >= threshold;
}

inline LayoutMergeBboxMode PaddleDocMergeBboxMode(int classId) {
    switch (classId) {
    case 3:  // chart
    case 5:  // display_formula
    case 6:  // doc_title
    case 15: // inline_formula
    case 17: // paragraph_title
        return LayoutMergeBboxMode::Large;
    default:
        return LayoutMergeBboxMode::Keep;
    }
}

inline bool ValidatePaddleDocBboxCount(
    int64_t bboxCount,
    size_t boxCapacity,
    size_t maskCapacity,
    bool masksRequired)
{
    if (bboxCount < 0) return false;
    const size_t count = (size_t)bboxCount;
    if (count > boxCapacity) return false;
    if (masksRequired && count > maskCapacity) return false;
    return true;
}

inline int64_t PaddleDocRoundNearestEven(double value) {
    if (!std::isfinite(value)) return 0;
    const double lower = std::floor(value);
    const double fraction = value - lower;
    if (fraction < 0.5) return (int64_t)lower;
    if (fraction > 0.5) return (int64_t)(lower + 1.0);
    const int64_t lowerInt = (int64_t)lower;
    return (lowerInt % 2 == 0) ? lowerInt : lowerInt + 1;
}

inline double PaddleDocInclusiveIou(
    double ax1, double ay1, double ax2, double ay2,
    double bx1, double by1, double bx2, double by2)
{
    const double iw = (std::max)(0.0, (std::min)(ax2, bx2) - (std::max)(ax1, bx1) + 1.0);
    const double ih = (std::max)(0.0, (std::min)(ay2, by2) - (std::max)(ay1, by1) + 1.0);
    const double inter = iw * ih;
    const double areaA = (std::max)(0.0, ax2 - ax1 + 1.0) * (std::max)(0.0, ay2 - ay1 + 1.0);
    const double areaB = (std::max)(0.0, bx2 - bx1 + 1.0) * (std::max)(0.0, by2 - by1 + 1.0);
    const double denominator = areaA + areaB - inter;
    return denominator > 0.0 ? inter / denominator : 0.0;
}

inline double PaddleDocExclusiveArea(double x1, double y1, double x2, double y2) {
    return (std::max)(0.0, x2 - x1) * (std::max)(0.0, y2 - y1);
}

inline double PaddleDocExclusiveIntersection(
    double ax1, double ay1, double ax2, double ay2,
    double bx1, double by1, double bx2, double by2)
{
    const double iw = (std::max)(0.0, (std::min)(ax2, bx2) - (std::max)(ax1, bx1));
    const double ih = (std::max)(0.0, (std::min)(ay2, by2) - (std::max)(ay1, by1));
    return iw * ih;
}

inline double PaddleDocExclusiveSmallOverlap(
    double ax1, double ay1, double ax2, double ay2,
    double bx1, double by1, double bx2, double by2)
{
    const double inter = PaddleDocExclusiveIntersection(
        ax1, ay1, ax2, ay2, bx1, by1, bx2, by2);
    const double smallArea = (std::min)(
        PaddleDocExclusiveArea(ax1, ay1, ax2, ay2),
        PaddleDocExclusiveArea(bx1, by1, bx2, by2));
    return smallArea > 0.0 ? inter / smallArea : 0.0;
}

inline double PaddleDocInnerCoverage(
    double innerX1, double innerY1, double innerX2, double innerY2,
    double outerX1, double outerY1, double outerX2, double outerY2)
{
    const double innerArea = PaddleDocExclusiveArea(innerX1, innerY1, innerX2, innerY2);
    if (innerArea <= 0.0) return 0.0;
    return PaddleDocExclusiveIntersection(
        innerX1, innerY1, innerX2, innerY2,
        outerX1, outerY1, outerX2, outerY2) / innerArea;
}
