#include "LongShotStitcher.h"
#include "screenshot/ScreenshotUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

#if defined(ZENCROP_WITH_OPENCV_DBPOST) || defined(ZENCROP_WITH_OPENCV_LAYOUT)
#  define ZENCROP_LONGSHOT_HAS_OPENCV 1
#  include <opencv2/core.hpp>
#  include <opencv2/imgproc.hpp>
#else
#  define ZENCROP_LONGSHOT_HAS_OPENCV 0
#endif

namespace longshot {
namespace {

// The primary matcher stops at 2/3-frame displacement.
// ZenCrop's fast-wheel recovery may use a smaller remaining overlap, but only
// when several broad regions provide a much stronger, unique consensus.
constexpr double kFastScrollRecoveryMaxRatio = 0.9;
constexpr double kFastScrollRecoveryScoreReject = 0.15;

struct BgraFrame {
    int width = 0;
    int height = 0;
    std::vector<BYTE> bgra; // top-down BGRA
};

bool LoadBgra(HBITMAP bmp, BgraFrame& out) {
    out = {};
    if (!bmp) return false;
    BITMAP bm = {};
    if (!GetObjectW(bmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0) return false;
    out.width = bm.bmWidth;
    out.height = std::abs(bm.bmHeight);
    if (static_cast<std::size_t>(out.width) >
        (std::numeric_limits<std::size_t>::max)() /
            static_cast<std::size_t>(out.height) / 4u) {
        return false;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = out.width;
    bmi.bmiHeader.biHeight = -out.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    try {
        out.bgra.resize(static_cast<std::size_t>(out.width) * out.height * 4u);
    } catch (const std::bad_alloc&) {
        return false;
    }
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    int got = GetDIBits(dc, bmp, 0, out.height, out.bgra.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return got == out.height;
}

// matchImageFast: CV_8UC1, absdiff>10 → mismatch; score=mismatch/valid.
float MatchImageFast(
    const unsigned char* a, int aStride,
    const unsigned char* b, int bStride,
    const unsigned char* mask, int mStride,
    int w, int h, int rowShiftB) {
    long long valid = 0;
    long long mismatch = 0;
    for (int y = 0; y < h; ++y) {
        const int yb = y + rowShiftB;
        if (yb < 0 || yb >= h) continue;
        const unsigned char* pa = a + static_cast<size_t>(y) * aStride;
        const unsigned char* pb = b + static_cast<size_t>(yb) * bStride;
        const unsigned char* pmA = mask ? mask + static_cast<size_t>(y) * mStride : nullptr;
        const unsigned char* pmB = mask ? mask + static_cast<size_t>(yb) * mStride : nullptr;
        for (int x = 0; x < w; ++x) {
            // Compare the union of the masks at the two aligned rows, so a
            // changed pixel from either frame contributes.
            if (pmA && pmA[x] == 0 && pmB[x] == 0) continue;
            ++valid;
            const int d = static_cast<int>(pa[x]) - static_cast<int>(pb[x]);
            if (std::abs(d) > kPixelDiffMismatch) ++mismatch;
        }
    }
    if (valid == 0) return 3.4028235e+38f;
    return static_cast<float>(mismatch) / static_cast<float>(valid);
}

bool ResampleFeatureNearest(
    const std::vector<unsigned char>& source,
    int sourceWidth,
    int sourceHeight,
    int maxWidth,
    int maxHeight,
    std::vector<unsigned char>& destination,
    int& destinationWidth,
    int& destinationHeight) {
    destination.clear();
    destinationWidth = 0;
    destinationHeight = 0;
    if (sourceWidth <= 0 || sourceHeight <= 0 || maxWidth <= 0 || maxHeight <= 0 ||
        source.size() < static_cast<std::size_t>(sourceWidth) * sourceHeight) {
        return false;
    }

    destinationWidth = (std::min)(sourceWidth, maxWidth);
    destinationHeight = (std::min)(sourceHeight, maxHeight);
    try {
        destination.resize(
            static_cast<std::size_t>(destinationWidth) * destinationHeight);
    } catch (const std::bad_alloc&) {
        destinationWidth = 0;
        destinationHeight = 0;
        return false;
    }

    for (int y = 0; y < destinationHeight; ++y) {
        const int sourceY = (std::min)(sourceHeight - 1,
            static_cast<int>(static_cast<long long>(y) * sourceHeight / destinationHeight));
        for (int x = 0; x < destinationWidth; ++x) {
            const int sourceX = (std::min)(sourceWidth - 1,
                static_cast<int>(static_cast<long long>(x) * sourceWidth / destinationWidth));
            destination[static_cast<std::size_t>(y) * destinationWidth + x] =
                source[static_cast<std::size_t>(sourceY) * sourceWidth + sourceX];
        }
    }
    return true;
}

struct RegionDisplacementScore {
    int displacement = 0;
    int agreeingRegions = 0;
    double averageScore = (std::numeric_limits<double>::max)();
    std::uint64_t agreeingMask = 0;
    unsigned int agreeingCrossMask = 0;
    unsigned int agreeingMainMask = 0;
};

int CountBits(unsigned int value) {
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

bool HasSpatialSupport(const RegionDisplacementScore& score) {
    return score.agreeingRegions >= 3 &&
        (CountBits(score.agreeingCrossMask) >= 2 ||
         CountBits(score.agreeingMainMask) >= 2);
}

bool HasSpatialSupportForDisplacement(
    const RegionDisplacementScore& score, int mainSpan) {
    if (mainSpan <= 0 || score.displacement == 0) return false;
    const int standardLimit = static_cast<int>(mainSpan * kDispMaxRatio);
    if (std::abs(score.displacement) <= standardLimit) {
        return HasSpatialSupport(score);
    }
    const int recoveryLimit =
        static_cast<int>(mainSpan * kFastScrollRecoveryMaxRatio);
    return std::abs(score.displacement) <= recoveryLimit &&
        score.agreeingRegions >= 4 &&
        CountBits(score.agreeingCrossMask) >= 3 &&
        score.averageScore <= kFastScrollRecoveryScoreReject;
}

bool EvaluateRegionScores(
    const std::vector<unsigned char>& previous,
    const std::vector<unsigned char>& current,
    int width,
    int height,
    int displacement,
    int crossBandCount,
    int mainBandCount,
    int sampleX,
    int sampleY,
    std::vector<float>& scores) {
    const int regionCount = crossBandCount * mainBandCount;
    scores.assign(static_cast<std::size_t>(regionCount),
        (std::numeric_limits<float>::max)());
    if (width <= 0 || height <= 0 || crossBandCount < 2 || mainBandCount < 2 ||
        regionCount > 64 || displacement == 0 ||
        previous.size() < static_cast<std::size_t>(width) * height ||
        current.size() < static_cast<std::size_t>(width) * height) {
        return false;
    }

    const int firstPreviousRow = (std::max)(0, -displacement);
    const int lastPreviousRow = (std::min)(height, height - displacement);
    const int overlap = lastPreviousRow - firstPreviousRow;
    if (overlap <= 0) return false;

    sampleX = (std::max)(1, sampleX);
    sampleY = (std::max)(1, sampleY);
    std::vector<int> valid(static_cast<std::size_t>(regionCount), 0);
    std::vector<int> mismatch(static_cast<std::size_t>(regionCount), 0);
    for (int y = firstPreviousRow; y < lastPreviousRow; y += sampleY) {
        const int currentY = y + displacement;
        const int mainBand = (std::min)(mainBandCount - 1,
            static_cast<int>(static_cast<long long>(y) * mainBandCount / height));
        const unsigned char* previousRow = previous.data() +
            static_cast<std::size_t>(y) * width;
        const unsigned char* currentRow = current.data() +
            static_cast<std::size_t>(currentY) * width;
        const unsigned char* previousSameRow = previous.data() +
            static_cast<std::size_t>(currentY) * width;
        const unsigned char* currentSameRow = current.data() +
            static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; x += sampleX) {
            // A pixel participates in the comparison mask
            // when either aligned row changed at that same screen position.
            const bool activeAtPrevious = std::abs(
                static_cast<int>(previousRow[x]) - static_cast<int>(currentSameRow[x])) >
                kPixelDiffMismatch;
            const bool activeAtCurrent = std::abs(
                static_cast<int>(previousSameRow[x]) - static_cast<int>(currentRow[x])) >
                kPixelDiffMismatch;
            if (!activeAtPrevious && !activeAtCurrent) continue;

            const int crossBand = (std::min)(crossBandCount - 1,
                static_cast<int>(static_cast<long long>(x) * crossBandCount / width));
            const int region = mainBand * crossBandCount + crossBand;
            ++valid[static_cast<std::size_t>(region)];
            if (std::abs(static_cast<int>(previousRow[x]) -
                    static_cast<int>(currentRow[x])) > kPixelDiffMismatch) {
                ++mismatch[static_cast<std::size_t>(region)];
            }
        }
    }

    bool any = false;
    for (int mainBand = 0; mainBand < mainBandCount; ++mainBand) {
        const int mainStart = (std::max)(firstPreviousRow,
            mainBand * height / mainBandCount);
        const int mainEnd = (std::min)(lastPreviousRow,
            (mainBand + 1) * height / mainBandCount);
        if (mainEnd <= mainStart) continue;
        const int sampledRows = (std::max)(1,
            (mainEnd - mainStart + sampleY - 1) / sampleY);
        for (int crossBand = 0; crossBand < crossBandCount; ++crossBand) {
            const int crossStart = crossBand * width / crossBandCount;
            const int crossEnd = (crossBand + 1) * width / crossBandCount;
            const int sampledWidth = (std::max)(1,
                (crossEnd - crossStart + sampleX - 1) / sampleX);
            const int minimumValid = (std::max)(12,
                sampledWidth * sampledRows / 100);
            const int region = mainBand * crossBandCount + crossBand;
            const int count = valid[static_cast<std::size_t>(region)];
            if (count < minimumValid) continue;
            scores[static_cast<std::size_t>(region)] =
                static_cast<float>(mismatch[static_cast<std::size_t>(region)]) /
                static_cast<float>(count);
            any = true;
        }
    }
    return any;
}

// A browser viewport can contain independently animated video plus sticky
// sidebars. The evidence-backed whole-frame matcher intentionally rejects a
// frame when over 30% of all changed pixels disagree, even if several
// independent article regions still identify one exact scroll offset. Use
// this conservative 2-D consensus only as a fallback: several spatially
// independent textured regions must agree, and each must have a unique
// low-score displacement.
bool DetectDisplacementBySpatialConsensus(
    const std::vector<unsigned char>& previous,
    int width,
    int height,
    const std::vector<unsigned char>& current,
    std::vector<float>& table,
    int& outDisplacement) {
    outDisplacement = 0;
    if (width < 32 || height < 24 ||
        previous.size() < static_cast<std::size_t>(width) * height ||
        current.size() < static_cast<std::size_t>(width) * height) {
        return false;
    }

    std::vector<unsigned char> coarsePrevious;
    std::vector<unsigned char> coarseCurrent;
    int coarseWidth = 0;
    int coarseHeight = 0;
    int currentWidth = 0;
    int currentHeight = 0;
    // Preserve main-axis coordinates exactly. Scaling 1060 rows to 512 turns a
    // real 70px scroll into a fractional 33.8px displacement, so neither
    // neighboring integer candidate can align high-frequency page texture.
    // Reduce only the cross axis and sparsely sample rows during coarse search.
    if (!ResampleFeatureNearest(previous, width, height, 160, height,
            coarsePrevious, coarseWidth, coarseHeight) ||
        !ResampleFeatureNearest(current, width, height, 160, height,
            coarseCurrent, currentWidth, currentHeight) ||
        coarseWidth != currentWidth || coarseHeight != currentHeight) {
        return false;
    }

    const int crossBandCount = coarseWidth >= 64 ? 8 : 4;
    const int mainBandCount = coarseHeight >= 96 ? 6 : 4;
    const int regionCount = crossBandCount * mainBandCount;
    const int search =
        static_cast<int>(coarseHeight * kFastScrollRecoveryMaxRatio);
    if (search < 1) return false;
    const int displacementCount = search * 2 + 1;
    table.assign(static_cast<std::size_t>(displacementCount) * regionCount,
        (std::numeric_limits<float>::max)());

    std::vector<float> scores;
    const int coarseSampleY = (std::max)(1, (coarseHeight + 255) / 256);
    for (int displacement = -search; displacement <= search; ++displacement) {
        if (displacement == 0) continue;
        if (!EvaluateRegionScores(
                coarsePrevious, coarseCurrent, coarseWidth, coarseHeight,
                displacement, crossBandCount, mainBandCount, 1, coarseSampleY, scores)) {
            continue;
        }
        const std::size_t row =
            static_cast<std::size_t>(displacement + search) * regionCount;
        std::copy(scores.begin(), scores.end(), table.begin() + row);
    }

    struct RegionVote {
        int region = 0;
        int displacement = 0;
        float score = (std::numeric_limits<float>::max)();
    };
    std::vector<RegionVote> votes;
    votes.reserve(static_cast<std::size_t>(regionCount));
    for (int region = 0; region < regionCount; ++region) {
        float bestScore = (std::numeric_limits<float>::max)();
        int bestDisplacement = 0;
        for (int displacement = -search; displacement <= search; ++displacement) {
            if (displacement == 0) continue;
            const float score = table[
                static_cast<std::size_t>(displacement + search) * regionCount + region];
            if (score < bestScore) {
                bestScore = score;
                bestDisplacement = displacement;
            }
        }
        if (bestDisplacement == 0 || bestScore >= static_cast<float>(kScoreReject)) continue;

        float runnerUp = (std::numeric_limits<float>::max)();
        for (int displacement = -search; displacement <= search; ++displacement) {
            if (displacement == 0 || std::abs(displacement - bestDisplacement) <= 2) continue;
            runnerUp = (std::min)(runnerUp, table[
                static_cast<std::size_t>(displacement + search) * regionCount + region]);
        }
        // Reject repetitive/textureless regions whose best displacement is
        // not materially different from another distant candidate.
        if (runnerUp < static_cast<float>(kScoreReject) &&
            runnerUp - bestScore < 0.06f) {
            continue;
        }
        votes.push_back({ region, bestDisplacement, bestScore });
    }
    if (votes.size() < 3) return false;

    RegionDisplacementScore bestCluster;
    RegionDisplacementScore runnerCluster;
    for (const RegionVote& center : votes) {
        RegionDisplacementScore cluster;
        cluster.displacement = center.displacement;
        cluster.averageScore = 0.0;
        for (const RegionVote& vote : votes) {
            if (std::abs(vote.displacement - center.displacement) > 1) continue;
            ++cluster.agreeingRegions;
            cluster.averageScore += vote.score;
            cluster.agreeingMask |= std::uint64_t{1} << vote.region;
            cluster.agreeingCrossMask |= 1u << (vote.region % crossBandCount);
            cluster.agreeingMainMask |= 1u << (vote.region / crossBandCount);
        }
        if (cluster.agreeingRegions > 0) cluster.averageScore /= cluster.agreeingRegions;
        const auto better = [](const RegionDisplacementScore& a,
                               const RegionDisplacementScore& b) {
            return a.agreeingRegions > b.agreeingRegions ||
                (a.agreeingRegions == b.agreeingRegions &&
                 a.averageScore < b.averageScore);
        };
        if (better(cluster, bestCluster)) {
            if (std::abs(bestCluster.displacement - cluster.displacement) > 2) {
                runnerCluster = bestCluster;
            }
            bestCluster = cluster;
        } else if (std::abs(cluster.displacement - bestCluster.displacement) > 2 &&
                   better(cluster, runnerCluster)) {
            runnerCluster = cluster;
        }
    }
    if (!HasSpatialSupportForDisplacement(bestCluster, coarseHeight)) return false;
    if (runnerCluster.agreeingRegions == bestCluster.agreeingRegions &&
        runnerCluster.averageScore <= bestCluster.averageScore + 0.03) {
        return false;
    }

    const double mainScale = static_cast<double>(height) / coarseHeight;
    const int predicted = static_cast<int>(std::lround(bestCluster.displacement * mainScale));
    const int radius = (std::max)(2, static_cast<int>(std::ceil(mainScale)) * 2);
    const int fullSearch =
        static_cast<int>(height * kFastScrollRecoveryMaxRatio);
    RegionDisplacementScore bestFull;
    const int sampleX = (std::max)(1, width / 256);
    const int sampleY = (std::max)(1, height / 768);
    for (int displacement = (std::max)(-fullSearch, predicted - radius);
         displacement <= (std::min)(fullSearch, predicted + radius);
         ++displacement) {
        if (displacement == 0) continue;
        if (!EvaluateRegionScores(previous, current, width, height, displacement,
                crossBandCount, mainBandCount, sampleX, sampleY, scores)) {
            continue;
        }
        RegionDisplacementScore candidate;
        candidate.displacement = displacement;
        candidate.averageScore = 0.0;
        for (int region = 0; region < regionCount; ++region) {
            if ((bestCluster.agreeingMask & (std::uint64_t{1} << region)) == 0) continue;
            const float score = scores[static_cast<std::size_t>(region)];
            if (score >= static_cast<float>(kScoreReject)) continue;
            ++candidate.agreeingRegions;
            candidate.averageScore += score;
            candidate.agreeingMask |= std::uint64_t{1} << region;
            candidate.agreeingCrossMask |= 1u << (region % crossBandCount);
            candidate.agreeingMainMask |= 1u << (region / crossBandCount);
        }
        if (candidate.agreeingRegions > 0) {
            candidate.averageScore /= candidate.agreeingRegions;
        }
        if (candidate.agreeingRegions > bestFull.agreeingRegions ||
            (candidate.agreeingRegions == bestFull.agreeingRegions &&
             candidate.averageScore < bestFull.averageScore)) {
            bestFull = candidate;
        }
    }
    if (!HasSpatialSupportForDisplacement(bestFull, height)) return false;
    outDisplacement = bestFull.displacement;
    return true;
}

} // namespace

void LongShotStitcher::Reset() {
    m_image.Clear();
    m_prevFeature.clear();
    m_prevFeatW = 0;
    m_prevFeatH = 0;
    m_lastDispFull = 0;
    m_lastResultWasFirstFrame = false;
    m_trimAnchor = 0;
}

void LongShotStitcher::AdvanceTrimAnchorAfterAcceptedFrame() {
    // Advance the logical anchor by the accepted displacement.
    // A negative result means a prepend and is normalized to zero; this is
    // deliberately different from LongShotImage::ContactOffset(), whose raw
    // coordinate remains negative so feature matching can continue correctly.
    const long long next = static_cast<long long>(m_trimAnchor) + m_lastDispFull;
    if (next <= 0) {
        m_trimAnchor = 0;
    } else if (next >= (std::numeric_limits<int>::max)()) {
        m_trimAnchor = (std::numeric_limits<int>::max)();
    } else {
        m_trimAnchor = static_cast<int>(next);
    }
}

void LongShotStitcher::RebaseTrimAnchorAfterCrop(int cropStart, int captureSpan) {
    const int length = m_image.Length();
    if (length <= 0) {
        m_trimAnchor = 0;
        return;
    }

    // Rebase the logical anchor after a successful crop:
    //   keep head [0,end): max(0, end - captureSpan)
    //   keep tail [start,length): max(0, length - start) == new length.
    if (cropStart <= 0) {
        const long long rebased = static_cast<long long>(length) -
            static_cast<long long>((std::max)(0, captureSpan));
        m_trimAnchor = rebased > 0 ? static_cast<int>(rebased) : 0;
    } else {
        m_trimAnchor = length;
    }
}

void LongShotStitcher::SetDirection(Direction d) {
    if (d == m_dir) return;
    m_dir = d;
    Reset();
}

bool LongShotStitcher::BuildFeature(
    HBITMAP frame, std::vector<unsigned char>& out, int& w, int& h) const {
    out.clear();
    w = 0;
    h = 0;
    try {
        BgraFrame bgra;
        if (!LoadBgra(frame, bgra)) return false;

#if ZENCROP_LONGSHOT_HAS_OPENCV
        // Format_RGB32 memory is BGRA on little-endian → COLOR_BGRA2GRAY (=10).
        cv::Mat src(bgra.height, bgra.width, CV_8UC4, bgra.bgra.data());
        cv::Mat gray;
        cv::cvtColor(src, gray, cv::COLOR_BGRA2GRAY);

        // Optional transpose for horizontal long-shot: feature main axis stays "rows".
        if (m_dir == Direction::Horizontal) {
            cv::Mat t;
            cv::transpose(gray, t);
            gray = t;
        }

        // Clamp only the post-transpose feature columns. Rows are
        // the main axis and must be retained for the displacement scale-back.
        if (gray.cols > kFeatureMaxSide) {
            cv::Mat resized;
            cv::resize(gray, resized, cv::Size(kFeatureMaxSide, gray.rows),
                0.0, 0.0, cv::INTER_NEAREST);
            gray = resized;
        }

        if (!gray.isContinuous()) gray = gray.clone();
        w = gray.cols;
        h = gray.rows;
        out.assign(gray.datastart, gray.dataend);
        return w > 0 && h > 0;
#else
        // Grayscale fallback without OpenCV (BT.601 on BGRA).
        w = bgra.width;
        h = bgra.height;
        // Horizontal: treat columns as main later via indexing in detect.
        out.resize(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const BYTE* p = bgra.bgra.data() + (static_cast<size_t>(y) * w + x) * 4;
                // B,G,R
                const int g = (p[0] * 29 + p[1] * 150 + p[2] * 77) >> 8;
                out[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(g);
            }
        }
        // Keep the main axis on feature rows in both directions, matching the
        // OpenCV path and the signed row-shift matcher below.
        if (m_dir == Direction::Horizontal) {
            std::vector<unsigned char> transposed(static_cast<size_t>(w) * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    transposed[static_cast<size_t>(x) * h + y] =
                        out[static_cast<size_t>(y) * w + x];
                }
            }
            out.swap(transposed);
            std::swap(w, h);
        }
        // Match the OpenCV branch's 512-column-only clamp even when OpenCV is
        // unavailable.  The main-axis rows deliberately remain unchanged.
        if (w > kFeatureMaxSide) {
            const int oldW = w;
            const int newW = kFeatureMaxSide;
            std::vector<unsigned char> scaled(static_cast<size_t>(newW) * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < newW; ++x) {
                    const int sx = (std::min)(oldW - 1,
                        static_cast<int>((static_cast<long long>(x) * oldW) / newW));
                    scaled[static_cast<size_t>(y) * newW + x] =
                        out[static_cast<size_t>(y) * oldW + sx];
                }
            }
            out.swap(scaled);
            w = newW;
        }
        return true;
#endif
    } catch (...) {
        out.clear();
        w = 0;
        h = 0;
        return false;
    }
}

bool LongShotStitcher::DetectDisplacement(
    const std::vector<unsigned char>& prev, int pw, int ph,
    const std::vector<unsigned char>& cur, int cw, int ch,
    int& outDispFull) const {
    outDispFull = 0;
    try {
        if (pw != cw || ph != ch || pw <= 0 || ph <= 0) return false;
        if (prev.size() < static_cast<size_t>(pw) * ph || cur.size() < static_cast<size_t>(cw) * ch) {
            return false;
        }
        const std::size_t pixelCount = static_cast<std::size_t>(pw) * ph;
        if (std::equal(prev.begin(), prev.begin() + pixelCount, cur.begin())) {
        // A stationary viewport is a valid covered contact, not a match
        // failure. This also keeps the idle 100 ms timer from opening warning
        // dialogs or needlessly repainting the long-shot overlay.
            return true;
        }
        const auto trySpatialConsensus = [&]() {
            return DetectDisplacementBySpatialConsensus(
                prev, pw, ph, cur, m_consensusScoreTable, outDispFull);
        };

#if ZENCROP_LONGSHOT_HAS_OPENCV
    cv::Mat img1(ph, pw, CV_8UC1, const_cast<unsigned char*>(prev.data()));
    cv::Mat img2(ch, cw, CV_8UC1, const_cast<unsigned char*>(cur.data()));

    // ROI: absdiff → threshold(10,255,BINARY) → non-zero bbox.
    cv::Mat diff, bin;
    cv::absdiff(img1, img2, diff);
    cv::threshold(diff, bin, kRoiThresh, kRoiMaxVal, cv::THRESH_BINARY);
    std::vector<cv::Point> nz;
    cv::findNonZero(bin, nz);
    if (nz.empty()) return trySpatialConsensus();
    // OpenCV 5 moved some geometry helpers; compute bbox explicitly for portability.
    int minX = nz[0].x, minY = nz[0].y, maxX = nz[0].x, maxY = nz[0].y;
    for (const auto& p : nz) {
        minX = (std::min)(minX, p.x);
        minY = (std::min)(minY, p.y);
        maxX = (std::max)(maxX, p.x);
        maxY = (std::max)(maxY, p.y);
    }
    const cv::Rect roi(minX, minY, maxX - minX + 1, maxY - minY + 1);
    const double roiArea = static_cast<double>(roi.width) * roi.height;
    const double imgArea = static_cast<double>(pw) * ph;
    if (roiArea < imgArea * kOverlapMin) return trySpatialConsensus();

    cv::Mat crop1 = img1(roi).clone();
    cv::Mat crop2 = img2(roi).clone();
    // Use the thresholded absdiff ROI as the comparison mask. It is not merely
    // used to find the bounding box: each
    // candidate score excludes zero-mask pixels.
    cv::Mat cropMask = bin(roi).clone();
    // Direction mask: bidirectional (tryAddImage always passes 2).
    // Search along feature rows (main axis after optional transpose).
    const int span = crop1.rows;
    cv::Mat allow = cv::Mat::zeros(1, span * 2, CV_8UC1);
    const int center = span;
    const int maxDisplacement = static_cast<int>(span * kDispMaxRatio);
    for (int o = -maxDisplacement; o <= maxDisplacement; ++o) {
        if (o == 0) continue;
        const int idx = center + o;
        if (idx >= 0 && idx < allow.cols) allow.at<unsigned char>(0, idx) = 255;
    }

    // Coarse → fine: start width ≈ full/32, double each round.
    int levelW = (std::max)(1, crop1.cols >> 5);
    int levelH = (std::max)(1, crop1.rows);
    float bestScore = 3.4028235e+38f;
    int bestDisp = 0;

    auto evalLevel = [&](int tw, int th) {
        cv::Mat s1, s2, sm;
        cv::resize(crop1, s1, cv::Size(tw, th), 0, 0, cv::INTER_NEAREST);
        cv::resize(crop2, s2, cv::Size(tw, th), 0, 0, cv::INTER_NEAREST);
        cv::resize(cropMask, sm, cv::Size(tw, th), 0, 0, cv::INTER_NEAREST);
        // Match calcOffsetScoreMat: resize the compare mask and restore its
        // 0/255 binary form before passing it to matchImageFast.
        cv::threshold(sm, sm, kRoiThresh, kRoiMaxVal, cv::THRESH_BINARY);
        cv::Mat allowScaled;
        cv::resize(allow, allowScaled, cv::Size(th * 2, 1), 0, 0, cv::INTER_NEAREST);
        cv::threshold(allowScaled, allowScaled, kRoiThresh, kRoiMaxVal, cv::THRESH_BINARY);

        const int c = th; // center in scaled allow
        std::vector<std::pair<float, int>> cands;
        cands.reserve(static_cast<size_t>(th));
        const int levelMaxDisplacement = static_cast<int>(th * kDispMaxRatio);
        for (int d = -levelMaxDisplacement; d <= levelMaxDisplacement; ++d) {
            const int ai = c + d;
            if (ai < 0 || ai >= allowScaled.cols) continue;
            if (allowScaled.at<unsigned char>(0, ai) == 0) continue;
            // rowShift: compare s1[y] with s2[y+d]
            const float sc = MatchImageFast(
                s1.ptr<unsigned char>(0), static_cast<int>(s1.step),
                s2.ptr<unsigned char>(0), static_cast<int>(s2.step),
                sm.ptr<unsigned char>(0), static_cast<int>(sm.step),
                s1.cols, s1.rows, d);
            cands.emplace_back(sc, d);
        }
        if (cands.empty()) return;
        std::sort(cands.begin(), cands.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        // Prune: keep ~half best, zero rest in allow for next... simplified: take best.
        // The acceptance gate applies to the final-resolution layer. Keeping
        // a lower score from an earlier coarse layer can accept a displacement
        // that no longer matches once the discarded detail is restored.
        bestScore = cands.front().first;
        const double scaleY = static_cast<double>(crop1.rows) / th;
        bestDisp = static_cast<int>(std::lround(cands.front().second * scaleY));
        // Mask prune for finer levels: keep top half of candidates in allow domain.
        const int keep = (std::max)(1, static_cast<int>((1.0 - kPruneKeepRatio) * cands.size()));
        cv::Mat newAllow = cv::Mat::zeros(allow.size(), CV_8UC1);
        for (int i = 0; i < keep && i < static_cast<int>(cands.size()); ++i) {
            const int fullD = static_cast<int>(std::lround(
                cands[static_cast<size_t>(i)].second * (static_cast<double>(span) / th)));
            const int idx = center + fullD;
            if (idx >= 0 && idx < newAllow.cols) newAllow.at<unsigned char>(0, idx) = 255;
        }
        // Merge: only keep previously allowed that are in top set — or union with band.
        cv::bitwise_and(allow, newAllow, allow);
        // Ensure best survives
        const int bidx = center + bestDisp;
        if (bidx >= 0 && bidx < allow.cols) allow.at<unsigned char>(0, bidx) = 255;
    };

    while (true) {
        evalLevel(levelW, levelH);
        if (levelW >= crop1.cols && levelH >= crop1.rows) break;
        levelW = (std::min)(crop1.cols, levelW * 2);
        levelH = (std::min)(crop1.rows, levelH * 2);
    }

    if (bestScore >= static_cast<float>(kScoreReject)) return trySpatialConsensus();

    // Map feature displacement to full-frame main-axis pixels.
    // Feature was possibly scaled: ratio fullMain/featMain.
    // We don't have full frame here — caller scales using frame size vs feature h.
    outDispFull = bestDisp; // feature-space; scaled in TryAddImage
    return bestDisp != 0 || trySpatialConsensus();
#else
    // Fallback: signed fixed-overlap search. It remains less sophisticated
    // than the OpenCV pyramid, but preserves vertical/horizontal and reverse
    // semantics instead of silently accepting forward-only matches.
    const int search = static_cast<int>(ph * kDispMaxRatio);
    float best = 3.4028235e+38f;
    int bestD = 0;
    for (int d = -search; d <= search; ++d) {
        if (d == 0) continue;
        const float sc = MatchImageFast(
            prev.data(), pw, cur.data(), cw, nullptr, 0, pw, ph, d);
        // Compare overlapping region only via rowShift
        if (sc < best) {
            best = sc;
            bestD = d;
        }
    }
    if (best >= static_cast<float>(kScoreReject) || bestD == 0) {
        return trySpatialConsensus();
    }
    outDispFull = bestD;
    return true;
#endif
    } catch (...) {
        outDispFull = 0;
        return false;
    }
}

StitchCode LongShotStitcher::TryAddImage(HBITMAP frame, int maxLen, bool strictFlag) {
    m_lastResultWasFirstFrame = false;
    m_lastDispFull = 0;
    if (!frame) return StitchCode::InternalError;

    auto size = Screenshot::GetBitmapSize(frame);
    if (size.width <= 0 || size.height <= 0) {
        DeleteObject(frame);
        return StitchCode::InternalError;
    }

    const int frameMain = (m_dir == Direction::Vertical) ? size.height : size.width;
    const int frameCross = (m_dir == Direction::Vertical) ? size.width : size.height;

    // First frame — empty long image.
    if (m_image.Empty()) {
        std::vector<unsigned char> feat;
        int fw = 0, fh = 0;
        if (!BuildFeature(frame, feat, fw, fh)) {
            DeleteObject(frame);
            return StitchCode::InternalError;
        }
        m_prevFeature = std::move(feat);
        m_prevFeatW = fw;
        m_prevFeatH = fh;
        const StitchCode code = m_image.AddFirstFrame(frame, m_dir);
        m_lastResultWasFirstFrame = m_image.LastAddAccepted() &&
            code == StitchCode::AcceptedNoExpand;
        if (m_lastResultWasFirstFrame) m_trimAnchor = 0;
        return code;
    }

    std::vector<unsigned char> feat;
    int fw = 0, fh = 0;
    if (!BuildFeature(frame, feat, fw, fh)) {
        DeleteObject(frame);
        return StitchCode::MatchFail;
    }

    int matchDispFeat = 0;
    if (!DetectDisplacement(
            m_prevFeature, m_prevFeatW, m_prevFeatH, feat, fw, fh, matchDispFeat)) {
        if (!strictFlag && (maxLen < 1 || m_image.Length() < maxLen)) {
            DeleteObject(frame);
            return StitchCode::MatchFail;
        }
        DeleteObject(frame);
        return StitchCode::MaxLength;
    }

    // Scale feature displacement to full pixels.
    const int featMain = m_prevFeatH; // after transpose, rows = main
    double matchDispFullF = 0.0;
    if (featMain > 0) {
        matchDispFullF = (static_cast<double>(frameMain) / featMain) * matchDispFeat;
    }
    const int matchDispFull = static_cast<int>(matchDispFullF >= 0
        ? std::floor(matchDispFullF + 0.5)
        : std::ceil(matchDispFullF - 0.5));

    // The primary matcher remains limited to 2/3. A displacement above that
    // can only come from the stricter spatial fast-wheel recovery.
    if (std::abs(matchDispFull) >
        static_cast<int>(frameMain * kFastScrollRecoveryMaxRatio)) {
        if (!strictFlag && (maxLen < 1 || m_image.Length() < maxLen)) {
            DeleteObject(frame);
            return StitchCode::MatchFail;
        }
        DeleteObject(frame);
        return StitchCode::MaxLength;
    }

    // MatchImageFast compares prev[y] with cur[y + d]. Therefore d is the
    // inverse of the current frame's raw start delta. Keep one unnormalized
    // coordinate system and place the new contact relative to the previous
    // contact, rather than relative to Length() (which loses prepend history).
    const int previousStart = m_image.ContactOffset();
    const long long newStart64 = static_cast<long long>(previousStart) - matchDispFull;
    if (newStart64 > (std::numeric_limits<int>::max)() ||
        newStart64 < (std::numeric_limits<int>::min)()) {
        DeleteObject(frame);
        return StitchCode::MaxLength;
    }
    const int newOffset = static_cast<int>(newStart64);
    const int newLen = m_image.ProjectedLength(newOffset, frameMain);

    if (maxLen > 0 && newLen > maxLen) {
        DeleteObject(frame);
        return StitchCode::MaxLength;
    }

    // Expose direction in stitched-image coordinates to the AutoCrop state
    // machine: positive means the current viewport moved forward/downstream.
    m_lastDispFull = newOffset - previousStart;

    if (newLen <= m_image.Length()) {
        // Return code 2 for a full frame that stays inside the
        // existing long image.  It advances only the feature/raw contact; the
        // existing pixels are intentionally not repainted.
        DeleteObject(frame);
        m_image.NoteContactOnly(newOffset);
        m_prevFeature = std::move(feat);
        m_prevFeatW = fw;
        m_prevFeatH = fh;
        AdvanceTrimAnchorAfterAcceptedFrame();
        return StitchCode::AcceptedNoExpand;
    }

    // Create the contact from the leading half when prepending and the trailing
    // half when appending. The full frame stays
    // alive as the source bitmap; LongShotImage copies only the selected range
    // and writes its overlap into existing tiles, exactly like contactImage.
    int contactStart = newOffset;
    int contactMainLen = frameMain;
    int sourceMainStart = 0;
    const int halfMain = frameMain / 2;
    if (halfMain > 0) {
        if (m_lastDispFull < 0) {
            contactMainLen = halfMain;
        } else {
            sourceMainStart = frameMain - halfMain;
            contactStart = newOffset + sourceMainStart;
            contactMainLen = halfMain;
        }

        const long long contactEnd = static_cast<long long>(contactStart) + contactMainLen;
        // LongShotImage does not permit gaps. Normal scroll deltas keep the
        // half-contact adjacent to the existing coverage; retain the full-frame
        // fallback for an out-of-band jump so the tile transaction stays valid.
        if (contactStart > m_image.MaxMain() || contactEnd < m_image.MinMain()) {
            contactStart = newOffset;
            contactMainLen = frameMain;
            sourceMainStart = 0;
        }
    }

    StitchCode code = m_image.AddFrameAt(
        frame, contactStart, contactMainLen, frameCross, sourceMainStart, newOffset);
    if (!m_image.LastAddAccepted()) {
        // AddFrameAt reports InternalError for GDI/OOM failures. Do not move
        // the feature contact in that case: the next displacement must still be
        // relative to the last image that actually exists in the tile store.
        m_lastDispFull = 0;
        return code;
    }
    m_prevFeature = std::move(feat);
    m_prevFeatW = fw;
    m_prevFeatH = fh;
    AdvanceTrimAnchorAfterAcceptedFrame();
    return code;
}

} // namespace longshot
