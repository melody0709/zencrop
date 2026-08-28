#pragma once

// LongShot limits and stitching thresholds.
// Do not merge gate numbers.

#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace longshot {

inline constexpr int kTimerMs = 100;
inline constexpr int kMatchFailThrottleMs = 2999;
inline constexpr int kWheelTierPx[] = {300, 500, 700, 900};
inline constexpr int kWheelUnits[] = {25, 35, 45, 55, 65};
inline constexpr int kPixelDiffMismatch = 10;
inline constexpr int kFeatureMaxSide = 512;       // 0x200
inline constexpr int kSuperLongWarnPx = 28000;
inline constexpr int kSaveSuperLongPx = 29000;
inline constexpr int kPinDisablePx = 28937;       // 0x7149
inline constexpr int kJpgDialogLimitPx = 65001;   // 0xfde9
inline constexpr int kJpgQuickSaveLimitPx = 65000;
inline constexpr int kStitchHardMaxPx = 2000000;
// Per-tile allocation budget: 128 MiB.
// This is a per-tile byte budget; large strips are split along the main axis.
inline constexpr std::size_t kTileByteBudget = 0x08000000ull;
inline constexpr double kScoreReject = 0.3;
inline constexpr double kOverlapMin = 0.3;
inline constexpr double kDispMaxRatio = 2.0 / 3.0;
inline constexpr double kPruneKeepRatio = 0.5;
inline constexpr double kRoiThresh = 10.0;        // cv::threshold
inline constexpr double kRoiMaxVal = 255.0;

// Factory mask: QColor(0,0,0,120) / #78000000
inline constexpr BYTE kMaskAlpha = 120;
inline constexpr COLORREF kMaskRgb = RGB(0, 0, 0);
inline constexpr COLORREF kSelectionBorder = RGB(255, 255, 255);

// tryAddImage return codes. Values 0–4 are observable by the
// LongShot widget and must remain numerically stable.
enum class StitchCode : int {
    ExtendedReverse = 0,
    ExtendedForward = 1,
    AcceptedNoExpand = 2, // first frame or a fully covered contact frame
    MaxLength = 3,
    MatchFail = 4,
    // Transport/allocation failure. Keep it outside the stable
    // public 0–4 protocol so a failed GDI write is never reported as a
    // successful reverse extension (code 0).
    InternalError = -1,
};

enum class Direction : int {
    Vertical = 0,
    Horizontal = 1,
};

// The snapshot is the trim anchor, not the stitched image
// length. Keeping the policy free of HWND/GDI state makes its two direction
// change branches testable without manufacturing a LongShotSession window.
struct LongShotAutoCropState {
    int scrollTrend = 0;         // 0 unknown, +1 forward, -1 reverse
    int trimAnchorSnapshot = 0;  // Prior logical trim anchor.
};

struct LongShotAutoCropPlan {
    int cropStart = 0;
    int cropEnd = 0; // exclusive, in the current materialized main-axis space

    bool HasCrop() const { return cropEnd > cropStart; }
};

// Pure policy for auto-cropping when the scroll direction changes. ZenCrop
// applies it whenever the user enables AutoCrop.
inline LongShotAutoCropPlan PlanLongShotAutoCrop(
    LongShotAutoCropState& state,
    StitchCode code,
    bool firstFrame,
    bool enabled,
    int trimAnchor,
    int actualLength,
    int captureSpan)
{
    LongShotAutoCropPlan plan;
    const auto snapshot = [&]() { state.trimAnchorSnapshot = trimAnchor; };

    // The initial code-2 result is not a fully-covered contact. It clears
    // trend exactly like the first-frame path.
    if (firstFrame) {
        state.scrollTrend = 0;
        snapshot();
        return plan;
    }

    // With AutoCrop disabled, preserve the current trend but still reach
    // the snapshot refresh path.
    if (!enabled || actualLength <= 0 || captureSpan <= 0) {
        snapshot();
        return plan;
    }

    const std::int64_t spanDelta =
        static_cast<std::int64_t>(actualLength) - static_cast<std::int64_t>(captureSpan);
    const std::int64_t absSpanDelta = spanDelta < 0 ? -spanDelta : spanDelta;
    if (absSpanDelta < 5 && state.scrollTrend != 0) {
        // Do not return here: the same timer tick may establish a new trend.
        state.scrollTrend = 0;
        snapshot();
    }

    if (state.scrollTrend == 0) {
        if (code == StitchCode::ExtendedReverse) {
            state.scrollTrend = -1;
        } else if (code == StitchCode::ExtendedForward) {
            state.scrollTrend = 1;
        }
        snapshot();
        return plan;
    }

    const std::int64_t anchorDelta =
        static_cast<std::int64_t>(trimAnchor) - static_cast<std::int64_t>(state.trimAnchorSnapshot);
    const int anchorDirection = anchorDelta > 0 ? 1 : (anchorDelta < 0 ? -1 : 0);

    // A code-2 contact with an unchanged anchor is a no-op; a changed anchor is handled
    // by the same direction test rather than being mistaken for a
    // fresh first frame.
    int expectedOppositeDirection = 0;
    if (state.scrollTrend == 1) {
        expectedOppositeDirection = -1;
        if (code != StitchCode::ExtendedReverse &&
            (anchorDirection == 0 || anchorDirection == state.scrollTrend)) {
            snapshot();
            return plan;
        }
    } else if (state.scrollTrend == -1) {
        expectedOppositeDirection = 1;
        if (code != StitchCode::ExtendedForward &&
            (anchorDirection == 0 || anchorDirection == state.scrollTrend)) {
            snapshot();
            return plan;
        }
    } else {
        snapshot();
        return plan;
    }

    const std::int64_t headEnd =
        static_cast<std::int64_t>(trimAnchor) + static_cast<std::int64_t>(captureSpan);
    if (state.scrollTrend == 1 && expectedOppositeDirection == -1 &&
        headEnd > 0 && headEnd < actualLength) {
        plan.cropStart = 0;
        plan.cropEnd = static_cast<int>(headEnd);
        return plan;
    }
    if (state.scrollTrend == -1 && expectedOppositeDirection == 1 && trimAnchor > 0) {
        plan.cropStart = trimAnchor;
        plan.cropEnd = actualLength;
        return plan;
    }

    snapshot();
    return plan;
}

// Behavior applied immediately after entering LongShot mode.
enum class AfterInitAction : int {
    DoNotStart = 0,
    VerticalAuto = 1,
    HorizontalAuto = 2,
    ShowStartStop = 3,
};

inline int WheelUnitsForSpan(int logicalSpanPx) {
    if (logicalSpanPx < kWheelTierPx[0]) return kWheelUnits[0];
    if (logicalSpanPx < kWheelTierPx[1]) return kWheelUnits[1];
    if (logicalSpanPx < kWheelTierPx[2]) return kWheelUnits[2];
    if (logicalSpanPx < kWheelTierPx[3]) return kWheelUnits[3];
    return kWheelUnits[4];
}

struct Tile {
    HBITMAP bitmap = nullptr;
    int offset = 0;   // main-axis start in stitched logical space
    int mainLen = 0;  // used height if vertical, used width if horizontal
    int crossLen = 0; // width if vertical, height if horizontal
    // The backing DIB can reserve space on both sides of the used range. This
    // lets consecutive small scroll deltas coalesce into one GDI bitmap rather
    // than consuming one GDI handle per captured strip.
    int capacityMain = 0;
    int dataStart = 0; // physical main-axis offset of the used range in bitmap
};

} // namespace longshot
