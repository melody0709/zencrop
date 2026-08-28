#pragma once

#include "LongShotTypes.h"
#include "LongShotImage.h"
#include <vector>

namespace longshot {

// Long-shot frame stitching and displacement matching.
// Holds previous feature contact and long image tiles.
class LongShotStitcher {
public:
    void Reset();
    void SetDirection(Direction d);
    Direction GetDirection() const { return m_dir; }

    // maxLen: hard cap (2000000 from timer). 0 on first frame.
    // strictFlag: the maximum-length condition was already reached.
    StitchCode TryAddImage(HBITMAP frame, int maxLen, bool strictFlag);

    LongShotImage& Image() { return m_image; }
    const LongShotImage& Image() const { return m_image; }
    int Length() const { return m_image.Length(); }
    // Last successful main-axis displacement in full-frame pixels (0 if none/fail).
    // Positive = expand forward; negative = prepend / reverse scroll.
    int LastDispFull() const { return m_lastDispFull; }
    // Code 2 is shared by first-frame and fully-covered-success paths.
    // Keep their local distinction out of the public return protocol so callers
    // that need a first-frame reset can query it explicitly.
    bool LastResultWasFirstFrame() const { return m_lastResultWasFirstFrame; }
    // This is a logical contact/trim anchor: forward
    // movement accumulates it, reverse movement clamps it at zero, and a
    // covered frame may move it without changing Image().Length().
    int TrimAnchor() const { return m_trimAnchor; }

    // Adjust the logical anchor after a successful trim. The
    // caller supplies the applied [cropStart, cropEnd) and the capture span;
    // Image() already contains the cropped length when this is called.
    void RebaseTrimAnchorAfterCrop(int cropStart, int captureSpan);

private:
    Direction m_dir = Direction::Vertical;
    LongShotImage m_image;
    // Previous feature grayscale (owned raw buffer w*h).
    std::vector<unsigned char> m_prevFeature;
    int m_prevFeatW = 0;
    int m_prevFeatH = 0;
    int m_lastDispFull = 0;
    bool m_lastResultWasFirstFrame = false;
    int m_trimAnchor = 0;
    // Matcher-only reuse buffer. It is never part of the stitch state.
    mutable std::vector<float> m_consensusScoreTable;

    void AdvanceTrimAnchorAfterAcceptedFrame();

    bool BuildFeature(HBITMAP frame, std::vector<unsigned char>& out, int& w, int& h) const;
    // Returns feature-space displacement on the main-axis rows. The caller maps
    // it back to full-frame pixels after the original 512-column clamp.
    // false → match failed.
    bool DetectDisplacement(
        const std::vector<unsigned char>& prev, int pw, int ph,
        const std::vector<unsigned char>& cur, int cw, int ch,
        int& outDispFull) const;
};

} // namespace longshot
