#pragma once

#include "LongShotTypes.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace longshot {

// Owns tile list + materialize. Thread-hostile (UI thread only).
class LongShotImage {
public:
    LongShotImage() = default;
    ~LongShotImage();
    LongShotImage(const LongShotImage&) = delete;
    LongShotImage& operator=(const LongShotImage&) = delete;

    void Clear();
    bool Empty() const { return m_tiles.empty(); }
    int Length() const; // main-axis logical length
    int MinMain() const { return m_hasBounds ? m_minMain : 0; }
    int MaxMain() const { return m_hasBounds ? m_maxMain : 0; }
    int CrossSize() const { return m_crossSize; }
    Direction Dir() const { return m_dir; }
    void SetDirection(Direction d) { m_dir = d; }
    std::size_t StoredBytes() const { return m_storedBytes; }
    // Distinguishes an accepted contact frame from an I/O or allocation failure
    // without overloading stitch code 0 (reverse extension success).
    bool LastAddAccepted() const { return m_lastAddAccepted; }

    // First frame: becomes sole tile at offset 0. Returns AcceptedNoExpand.
    StitchCode AddFirstFrame(HBITMAP frame, Direction dir);

    // Contact a range of a captured frame at its logical main-axis start.
    // Supply a full frame for the first contact, then use
    // the leading/trailing half as a legacy contact image for extensions.
    // sourceMainStart selects that range inside frame; rawFrameStart keeps the
    // feature contact in full-frame coordinates for the next displacement.
    // The consumed frame is always released by this call.
    StitchCode AddFrameAt(
        HBITMAP frame,
        int contactStart,
        int contactMainLen,
        int crossLen,
        int sourceMainStart = 0,
        std::optional<int> rawFrameStart = std::nullopt);

    // Span that would result from placing a frame at frameStart. This is used
    // for the hard length gate before allocating any tile.
    int ProjectedLength(int frameStart, int frameMain) const;

    // Update feature contact only (no geometry extend) — caller owns frame still.
    void NoteContactOnly(int rawFrameStart);

    // Flatten tiles into a single 32bpp top-down DIB. Caller owns result.
    HBITMAP Materialize() const;

    // Render the complete stitched image into a bounded 32bpp top-down DIB
    // without first materializing the full image. Caller owns the result.
    // This keeps the live preview cost bounded even for multi-million-pixel
    // long shots.
    HBITMAP RenderPreview(
        int maxWidth, int maxHeight, int* outWidth = nullptr, int* outHeight = nullptr) const;

    // Keep main-axis [start, end) of the stitched image.
    // end exclusive; if end<=start or out of range returns false and leaves tiles unchanged.
    bool CropMainAxis(int start, int end);

    // Raw start of the last accepted contact frame, not normalized output
    // coordinates. The next matched frame is placed relative to this value.
    int ContactOffset() const { return m_contactOffset; }
    const std::vector<Tile>& Tiles() const { return m_tiles; }

private:
    Direction m_dir = Direction::Vertical;
    int m_crossSize = 0;
    int m_contactOffset = 0;
    int m_minMain = 0;
    int m_maxMain = 0;
    bool m_hasBounds = false;
    bool m_lastAddAccepted = false;
    std::size_t m_storedBytes = 0;
    std::vector<Tile> m_tiles;

    void FreeTiles();
    void SwapState(LongShotImage& other) noexcept;
    int MaxTileMainForCross(int crossLen) const;
    bool AddStripFromFrame(
        HBITMAP frame,
        int sourceMainStart,
        int stripMainLen,
        int outputMainStart,
        int crossLen);
    // Best-effort post-commit compaction. Allocation failure leaves the
    // already-valid strips intact, so stitch geometry never becomes partial.
    void CoalesceAdjacentTiles();
    bool MergeAdjacentTiles(std::size_t leftIndex);
};

} // namespace longshot
