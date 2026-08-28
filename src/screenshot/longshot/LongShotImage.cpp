#include "LongShotImage.h"
#include "screenshot/ScreenshotUtils.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace longshot {
namespace {

struct DibView {
    int width = 0;
    int height = 0;
    int stride = 0;
    const BYTE* bits = nullptr;
    std::vector<BYTE> owned;
};

bool PixelBytes(int width, int height, std::size_t& out) {
    out = 0;
    if (width <= 0 || height <= 0) return false;
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > (std::numeric_limits<std::size_t>::max)() / h / 4u) return false;
    out = w * h * 4u;
    return true;
}

bool LockDib(HBITMAP bmp, DibView& out) {
    out = {};
    if (!bmp) return false;
    BITMAP bm = {};
    if (!GetObjectW(bmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0) return false;
    out.width = bm.bmWidth;
    out.height = std::abs(bm.bmHeight);

    std::size_t bytes = 0;
    if (!PixelBytes(out.width, out.height, bytes)) return false;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = out.width;
    bmi.bmiHeader.biHeight = -out.height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    try {
        out.owned.resize(bytes);
    } catch (const std::bad_alloc&) {
        return false;
    }
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    const int got = GetDIBits(screen, bmp, 0, out.height, out.owned.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (got != out.height) return false;
    out.stride = out.width * 4;
    out.bits = out.owned.data();
    return true;
}

HBITMAP CreateDib(int width, int height, void** outBits) {
    if (outBits) *outBits = nullptr;
    std::size_t bytes = 0;
    if (!PixelBytes(width, height, bytes)) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        return nullptr;
    }
    if (outBits) *outBits = bits;
    return dib;
}

HBITMAP CreateDibFromPixels(int width, int height, const BYTE* pixels) {
    if (!pixels) return nullptr;
    std::size_t bytes = 0;
    if (!PixelBytes(width, height, bytes)) return nullptr;
    void* bits = nullptr;
    HBITMAP dib = CreateDib(width, height, &bits);
    if (!dib) return nullptr;
    std::memcpy(bits, pixels, bytes);
    return dib;
}

HBITMAP CreateMainAxisStrip(
    const DibView& source,
    Direction direction,
    int sourceMainStart,
    int stripMainLen,
    int expectedCross) {
    if (!source.bits || sourceMainStart < 0 || stripMainLen <= 0 || expectedCross <= 0) {
        return nullptr;
    }
    const int sourceMain = direction == Direction::Vertical ? source.height : source.width;
    const int sourceCross = direction == Direction::Vertical ? source.width : source.height;
    if (sourceCross != expectedCross || sourceMainStart > sourceMain ||
        stripMainLen > sourceMain - sourceMainStart) {
        return nullptr;
    }

    const int outW = direction == Direction::Vertical ? source.width : stripMainLen;
    const int outH = direction == Direction::Vertical ? stripMainLen : source.height;
    void* outBits = nullptr;
    HBITMAP strip = CreateDib(outW, outH, &outBits);
    if (!strip) return nullptr;

    auto* dst = static_cast<BYTE*>(outBits);
    if (direction == Direction::Vertical) {
        const BYTE* src = source.bits + static_cast<std::size_t>(sourceMainStart) * source.stride;
        std::memcpy(dst, src, static_cast<std::size_t>(stripMainLen) * source.stride);
    } else {
        const int bytesPerStripRow = stripMainLen * 4;
        for (int y = 0; y < source.height; ++y) {
            const BYTE* src = source.bits + static_cast<std::size_t>(y) * source.stride +
                static_cast<std::size_t>(sourceMainStart) * 4;
            BYTE* row = dst + static_cast<std::size_t>(y) * bytesPerStripRow;
            std::memcpy(row, src, bytesPerStripRow);
        }
    }
    return strip;
}

bool BlitMainAxisRange(
    HBITMAP destination,
    int destinationMainStart,
    HBITMAP source,
    int sourceMainStart,
    int mainLen,
    int crossLen,
    Direction direction) {
    if (!destination || !source || destinationMainStart < 0 || sourceMainStart < 0 ||
        mainLen <= 0 || crossLen <= 0) {
        return false;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC destinationDc = CreateCompatibleDC(screen);
    HDC sourceDc = CreateCompatibleDC(screen);
    if (!destinationDc || !sourceDc) {
        if (destinationDc) DeleteDC(destinationDc);
        if (sourceDc) DeleteDC(sourceDc);
        ReleaseDC(nullptr, screen);
        return false;
    }

    HGDIOBJ oldDestination = SelectObject(destinationDc, destination);
    HGDIOBJ oldSource = SelectObject(sourceDc, source);
    bool copied = false;
    if (oldDestination && oldDestination != HGDI_ERROR && oldSource && oldSource != HGDI_ERROR) {
        if (direction == Direction::Vertical) {
            copied = BitBlt(destinationDc, 0, destinationMainStart, crossLen, mainLen,
                sourceDc, 0, sourceMainStart, SRCCOPY) != FALSE;
        } else {
            copied = BitBlt(destinationDc, destinationMainStart, 0, mainLen, crossLen,
                sourceDc, sourceMainStart, 0, SRCCOPY) != FALSE;
        }
    }
    if (oldDestination && oldDestination != HGDI_ERROR) SelectObject(destinationDc, oldDestination);
    if (oldSource && oldSource != HGDI_ERROR) SelectObject(sourceDc, oldSource);
    DeleteDC(destinationDc);
    DeleteDC(sourceDc);
    ReleaseDC(nullptr, screen);
    return copied;
}

HBITMAP CreateTileDib(Direction direction, int crossLen, int capacityMain) {
    if (crossLen <= 0 || capacityMain <= 0) return nullptr;
    const int width = direction == Direction::Vertical ? crossLen : capacityMain;
    const int height = direction == Direction::Vertical ? capacityMain : crossLen;
    return CreateDib(width, height, nullptr);
}

} // namespace

LongShotImage::~LongShotImage() { FreeTiles(); }

void LongShotImage::FreeTiles() {
    for (auto& t : m_tiles) {
        if (t.bitmap) {
            DeleteObject(t.bitmap);
            t.bitmap = nullptr;
        }
    }
    m_tiles.clear();
    m_crossSize = 0;
    m_contactOffset = 0;
    m_minMain = 0;
    m_maxMain = 0;
    m_hasBounds = false;
    m_lastAddAccepted = false;
    m_storedBytes = 0;
}

void LongShotImage::Clear() { FreeTiles(); }

void LongShotImage::SwapState(LongShotImage& other) noexcept {
    using std::swap;
    swap(m_dir, other.m_dir);
    swap(m_crossSize, other.m_crossSize);
    swap(m_contactOffset, other.m_contactOffset);
    swap(m_minMain, other.m_minMain);
    swap(m_maxMain, other.m_maxMain);
    swap(m_hasBounds, other.m_hasBounds);
    swap(m_lastAddAccepted, other.m_lastAddAccepted);
    swap(m_storedBytes, other.m_storedBytes);
    m_tiles.swap(other.m_tiles);
}

int LongShotImage::Length() const {
    return m_hasBounds ? m_maxMain - m_minMain : 0;
}

int LongShotImage::ProjectedLength(int frameStart, int frameMain) const {
    if (frameMain <= 0) return Length();
    const long long frameEnd = static_cast<long long>(frameStart) + frameMain;
    if (frameEnd > (std::numeric_limits<int>::max)() ||
        frameEnd < (std::numeric_limits<int>::min)()) {
        return (std::numeric_limits<int>::max)();
    }
    if (!m_hasBounds) return frameMain;
    const long long minMain = (std::min)(static_cast<long long>(m_minMain),
        static_cast<long long>(frameStart));
    const long long maxMain = (std::max)(static_cast<long long>(m_maxMain), frameEnd);
    const long long length = maxMain - minMain;
    return length > (std::numeric_limits<int>::max)()
        ? (std::numeric_limits<int>::max)()
        : static_cast<int>(length);
}

int LongShotImage::MaxTileMainForCross(int crossLen) const {
    if (crossLen <= 0) return 0;
    const std::size_t bytesPerMainPixel = static_cast<std::size_t>(crossLen) * 4u;
    if (bytesPerMainPixel == 0) return 0;
    const std::size_t budgetMain = kTileByteBudget / bytesPerMainPixel;
    if (budgetMain == 0) return 1;
    return static_cast<int>((std::min)(budgetMain,
        static_cast<std::size_t>((std::numeric_limits<int>::max)())));
}

bool LongShotImage::AddStripFromFrame(
    HBITMAP frame,
    int sourceMainStart,
    int stripMainLen,
    int outputMainStart,
    int crossLen) {
    if (!frame || stripMainLen <= 0 || crossLen <= 0) return false;

    DibView source;
    if (!LockDib(frame, source)) return false;
    const int sourceMain = m_dir == Direction::Vertical ? source.height : source.width;
    const int sourceCross = m_dir == Direction::Vertical ? source.width : source.height;
    if (sourceMainStart < 0 || sourceMainStart > sourceMain ||
        stripMainLen > sourceMain - sourceMainStart || sourceCross != crossLen) {
        return false;
    }

    const std::size_t bytesPerMainPixel = static_cast<std::size_t>(crossLen) * 4u;
    const int maxTileMain = MaxTileMainForCross(crossLen);
    if (bytesPerMainPixel == 0 || maxTileMain <= 0) return false;

    int consumed = 0;
    while (consumed < stripMainLen) {
        const int part = (std::min)(maxTileMain, stripMainLen - consumed);
        HBITMAP strip = CreateMainAxisStrip(
            source, m_dir, sourceMainStart + consumed, part, crossLen);
        if (!strip) return false;
        Tile tile;
        tile.bitmap = strip;
        tile.offset = outputMainStart + consumed;
        tile.mainLen = part;
        tile.crossLen = crossLen;
        tile.capacityMain = part;
        tile.dataStart = 0;
        try {
            m_tiles.push_back(tile);
        } catch (const std::bad_alloc&) {
            DeleteObject(strip);
            return false;
        }
        m_storedBytes += static_cast<std::size_t>(part) * bytesPerMainPixel;
        consumed += part;
    }
    return true;
}

bool LongShotImage::MergeAdjacentTiles(std::size_t leftIndex) {
    if (leftIndex + 1 >= m_tiles.size()) return false;
    Tile& left = m_tiles[leftIndex];
    Tile& right = m_tiles[leftIndex + 1];
    if (!left.bitmap || !right.bitmap || left.crossLen <= 0 ||
        left.crossLen != right.crossLen || left.mainLen <= 0 || right.mainLen <= 0) {
        return false;
    }

    const long long leftEnd = static_cast<long long>(left.offset) + left.mainLen;
    if (leftEnd != right.offset) return false;
    const long long total64 = static_cast<long long>(left.mainLen) + right.mainLen;
    const int maxTileMain = MaxTileMainForCross(left.crossLen);
    if (total64 <= 0 || total64 > maxTileMain) return false;
    const int total = static_cast<int>(total64);

    const int leftCapacity = left.capacityMain > 0 ? left.capacityMain : left.mainLen;
    const int rightCapacity = right.capacityMain > 0 ? right.capacityMain : right.mainLen;
    const int leftStart = left.dataStart;
    const int rightStart = right.dataStart;

    // Prefer appending to the left tile; this is the normal forward-scroll path.
    if (leftStart >= 0 && leftStart <= leftCapacity &&
        left.mainLen <= leftCapacity - leftStart &&
        right.mainLen <= leftCapacity - leftStart - left.mainLen &&
        BlitMainAxisRange(left.bitmap, leftStart + left.mainLen,
            right.bitmap, rightStart, right.mainLen, left.crossLen, m_dir)) {
        DeleteObject(right.bitmap);
        left.mainLen = total;
        left.capacityMain = leftCapacity;
        m_tiles.erase(m_tiles.begin() + static_cast<std::ptrdiff_t>(leftIndex + 1));
        return true;
    }

    // Repeated reverse scroll prepends to the right tile without allocating when
    // that tile retained leading capacity after its last growth.
    if (rightStart >= left.mainLen && rightStart <= rightCapacity &&
        right.mainLen <= rightCapacity - rightStart &&
        BlitMainAxisRange(right.bitmap, rightStart - left.mainLen,
            left.bitmap, leftStart, left.mainLen, left.crossLen, m_dir)) {
        right.offset = left.offset;
        right.mainLen = total;
        right.capacityMain = rightCapacity;
        right.dataStart = rightStart - left.mainLen;
        DeleteObject(left.bitmap);
        m_tiles.erase(m_tiles.begin() + static_cast<std::ptrdiff_t>(leftIndex));
        return true;
    }

    // Grow geometrically, but never beyond the per-tile 128 MiB budget.
    const int largestCapacity = (std::max)(leftCapacity, rightCapacity);
    long long grown = static_cast<long long>(largestCapacity) * 2;
    if (grown < total) grown = total;
    if (grown > maxTileMain) grown = maxTileMain;
    const int newCapacity = static_cast<int>(grown);
    if (newCapacity < total) return false;
    const int newDataStart = (newCapacity - total) / 2;

    HBITMAP merged = CreateTileDib(m_dir, left.crossLen, newCapacity);
    if (!merged) return false;
    if (!BlitMainAxisRange(merged, newDataStart,
            left.bitmap, leftStart, left.mainLen, left.crossLen, m_dir) ||
        !BlitMainAxisRange(merged, newDataStart + left.mainLen,
            right.bitmap, rightStart, right.mainLen, left.crossLen, m_dir)) {
        DeleteObject(merged);
        return false;
    }

    DeleteObject(left.bitmap);
    DeleteObject(right.bitmap);
    left.bitmap = merged;
    left.mainLen = total;
    left.capacityMain = newCapacity;
    left.dataStart = newDataStart;
    m_tiles.erase(m_tiles.begin() + static_cast<std::ptrdiff_t>(leftIndex + 1));
    return true;
}

void LongShotImage::CoalesceAdjacentTiles() {
    if (m_tiles.size() < 2) return;
    std::sort(m_tiles.begin(), m_tiles.end(),
        [](const Tile& a, const Tile& b) { return a.offset < b.offset; });

    // Coalescing is an optimization after the frame has been committed. If GDI
    // allocation fails, retain valid exact strips and retry on a later frame.
    std::size_t index = 0;
    while (index + 1 < m_tiles.size()) {
        if (MergeAdjacentTiles(index)) {
            // A merge can make the resulting tile adjacent to its predecessor.
            // Revisit that one pair, rather than restarting a full scan.
            if (index > 0) --index;
        } else {
            ++index;
        }
    }
}

StitchCode LongShotImage::AddFirstFrame(HBITMAP frame, Direction dir) {
    FreeTiles();
    if (!frame) return StitchCode::InternalError;
    const auto size = Screenshot::GetBitmapSize(frame);
    if (size.width <= 0 || size.height <= 0) {
        DeleteObject(frame);
        return StitchCode::InternalError;
    }

    m_dir = dir;
    const int mainLen = dir == Direction::Vertical ? size.height : size.width;
    const int crossLen = dir == Direction::Vertical ? size.width : size.height;
    m_crossSize = crossLen;
    if (!AddStripFromFrame(frame, 0, mainLen, 0, crossLen)) {
        DeleteObject(frame);
        FreeTiles();
        return StitchCode::InternalError;
    }
    DeleteObject(frame);
    m_contactOffset = 0;
    m_minMain = 0;
    m_maxMain = mainLen;
    m_hasBounds = true;
    m_lastAddAccepted = true;
    return StitchCode::AcceptedNoExpand;
}

StitchCode LongShotImage::AddFrameAt(
    HBITMAP frame,
    int contactStart,
    int contactMainLen,
    int crossLen,
    int sourceMainStart,
    std::optional<int> rawFrameStart) {
    m_lastAddAccepted = false;
    if (!frame || contactMainLen <= 0 || crossLen <= 0 || !m_hasBounds ||
        m_crossSize <= 0 || crossLen != m_crossSize) {
        if (frame) DeleteObject(frame);
        return StitchCode::InternalError;
    }

    const auto sourceSize = Screenshot::GetBitmapSize(frame);
    const int sourceMain = m_dir == Direction::Vertical ? sourceSize.height : sourceSize.width;
    const int sourceCross = m_dir == Direction::Vertical ? sourceSize.width : sourceSize.height;
    if (sourceMain <= 0 || sourceCross != crossLen || sourceMainStart < 0 ||
        sourceMainStart > sourceMain || contactMainLen > sourceMain - sourceMainStart) {
        DeleteObject(frame);
        return StitchCode::InternalError;
    }

    const long long contactEnd64 = static_cast<long long>(contactStart) + contactMainLen;
    if (contactEnd64 > (std::numeric_limits<int>::max)() ||
        contactEnd64 < (std::numeric_limits<int>::min)()) {
        DeleteObject(frame);
        return StitchCode::InternalError;
    }
    const int contactEnd = static_cast<int>(contactEnd64);
    const int oldMin = m_minMain;
    const int oldMax = m_maxMain;
    const int oldRawContact = m_contactOffset;
    const int newRawContact = rawFrameStart.value_or(contactStart);
    const std::size_t oldTileCount = m_tiles.size();
    const std::size_t oldBytes = m_storedBytes;

    auto rollback = [&]() {
        while (m_tiles.size() > oldTileCount) {
            Tile& tile = m_tiles.back();
            if (tile.bitmap) DeleteObject(tile.bitmap);
            m_tiles.pop_back();
        }
        m_storedBytes = oldBytes;
    };

    const bool extendsHead = contactStart < oldMin;
    const bool extendsTail = contactEnd > oldMax;
    if (!extendsHead && !extendsTail) {
        // Code 2 represents a completely covered frame. It commits
        // the feature contact but deliberately does not repaint its tiles.
        DeleteObject(frame);
        m_contactOffset = newRawContact;
        m_lastAddAccepted = true;
        return StitchCode::AcceptedNoExpand;
    }

    struct PendingOverwrite {
        std::size_t tileIndex = 0;
        HBITMAP backup = nullptr;
        int tileMainStart = 0;
        int frameMainStart = 0;
        int mainLen = 0;
    };
    std::vector<PendingOverwrite> overwrites;
    try {
        overwrites.reserve(oldTileCount);
    } catch (const std::bad_alloc&) {
        DeleteObject(frame);
        return StitchCode::InternalError;
    }
    auto discardOverwrites = [&]() {
        for (const auto& pending : overwrites) {
            if (pending.backup) DeleteObject(pending.backup);
        }
        overwrites.clear();
    };

    // Contact-image updates write through every already
    // covered intersection before it appends/prepends the uncovered strip.
    // Back up only the ranges that will be overwritten. Cloning a complete
    // geometrically-reserved tile made each 100ms frame copy tens of megabytes
    // after the ~3,000px growth boundary, and one transient GDI allocation
    // failure then left matching permanently anchored to an old frame.
    const int overlapStart = (std::max)(contactStart, oldMin);
    const int overlapEnd = (std::min)(contactEnd, oldMax);
    if (overlapStart < overlapEnd) {
        for (std::size_t index = 0; index < oldTileCount; ++index) {
            const Tile& tile = m_tiles[index];
            const long long tileEnd64 = static_cast<long long>(tile.offset) + tile.mainLen;
            if (!tile.bitmap || tile.mainLen <= 0 || tile.crossLen != crossLen ||
                tileEnd64 > (std::numeric_limits<int>::max)()) {
                discardOverwrites();
                DeleteObject(frame);
                return StitchCode::InternalError;
            }
            const int tileStart = tile.offset;
            const int tileEnd = static_cast<int>(tileEnd64);
            const int writeStart = (std::max)(overlapStart, tileStart);
            const int writeEnd = (std::min)(overlapEnd, tileEnd);
            if (writeStart >= writeEnd) continue;

            const int writeLen = writeEnd - writeStart;
            const int tileMainStart = tile.dataStart + (writeStart - tileStart);
            const int frameMainStart = sourceMainStart + (writeStart - contactStart);
            HBITMAP backup = CreateTileDib(m_dir, crossLen, writeLen);
            if (!backup || !BlitMainAxisRange(
                    backup, 0, tile.bitmap, tileMainStart, writeLen, crossLen, m_dir)) {
                if (backup) DeleteObject(backup);
                discardOverwrites();
                DeleteObject(frame);
                return StitchCode::InternalError;
            }
            try {
                overwrites.push_back(
                    { index, backup, tileMainStart, frameMainStart, writeLen });
            } catch (const std::bad_alloc&) {
                DeleteObject(backup);
                discardOverwrites();
                DeleteObject(frame);
                return StitchCode::InternalError;
            }
        }
    }

    if (extendsHead) {
        const int leadingEnd = (std::min)(contactEnd, oldMin);
        const int leadingLen = leadingEnd - contactStart;
        if (leadingLen > 0) {
            if (!AddStripFromFrame(frame, sourceMainStart, leadingLen, contactStart, crossLen)) {
                rollback();
                discardOverwrites();
                DeleteObject(frame);
                return StitchCode::InternalError;
            }
        }
    }
    if (extendsTail) {
        const int trailingStart = (std::max)(contactStart, oldMax);
        const int trailingLen = contactEnd - trailingStart;
        if (trailingLen > 0) {
            const int sourceStart = sourceMainStart + (trailingStart - contactStart);
            if (!AddStripFromFrame(frame, sourceStart, trailingLen, trailingStart, crossLen)) {
                rollback();
                discardOverwrites();
                DeleteObject(frame);
                return StitchCode::InternalError;
            }
        }
    }

    std::size_t overwrittenCount = 0;
    for (; overwrittenCount < overwrites.size(); ++overwrittenCount) {
        const PendingOverwrite& pending = overwrites[overwrittenCount];
        Tile& tile = m_tiles[pending.tileIndex];
        if (!BlitMainAxisRange(
                tile.bitmap, pending.tileMainStart,
                frame, pending.frameMainStart,
                pending.mainLen, crossLen, m_dir)) {
            // Restore every range already changed. Geometry/feature contact is
            // still uncommitted, so the next timer tick can safely retry.
            for (std::size_t restore = 0; restore <= overwrittenCount; ++restore) {
                const PendingOverwrite& previous = overwrites[restore];
                Tile& previousTile = m_tiles[previous.tileIndex];
                BlitMainAxisRange(
                    previousTile.bitmap, previous.tileMainStart,
                    previous.backup, 0,
                    previous.mainLen, crossLen, m_dir);
            }
            rollback();
            discardOverwrites();
            DeleteObject(frame);
            return StitchCode::InternalError;
        }
    }
    discardOverwrites();
    DeleteObject(frame);

    m_contactOffset = newRawContact;
    m_lastAddAccepted = true;
    m_minMain = (std::min)(oldMin, contactStart);
    m_maxMain = (std::max)(oldMax, contactEnd);
    CoalesceAdjacentTiles();
    return newRawContact < oldRawContact
        ? StitchCode::ExtendedReverse
        : StitchCode::ExtendedForward;
}

void LongShotImage::NoteContactOnly(int rawFrameStart) {
    // Geometry is unchanged; LongShotStitcher still updates its feature contact.
    m_contactOffset = rawFrameStart;
    m_lastAddAccepted = m_hasBounds;
}

HBITMAP LongShotImage::Materialize() const {
    if (m_tiles.empty() || !m_hasBounds) return nullptr;

    const int mainLen = Length();
    const int cross = m_crossSize;
    if (mainLen <= 0 || cross <= 0) return nullptr;

    const int outW = m_dir == Direction::Vertical ? cross : mainLen;
    const int outH = m_dir == Direction::Vertical ? mainLen : cross;
    std::size_t canvasBytes = 0;
    if (!PixelBytes(outW, outH, canvasBytes)) return nullptr;

    std::vector<BYTE> canvas;
    try {
        canvas.assign(canvasBytes, 0);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }

    for (const auto& tile : m_tiles) {
        DibView view;
        // A missing/unreadable tile must fail the export transaction. Silently
        // skipping it produces a plausible-looking image with blank bands.
        if (!LockDib(tile.bitmap, view)) return nullptr;
        const int viewMain = m_dir == Direction::Vertical ? view.height : view.width;
        const int viewCross = m_dir == Direction::Vertical ? view.width : view.height;
        const int dataStart = tile.dataStart;
        if (tile.mainLen <= 0 || tile.crossLen != cross || viewCross != cross ||
            dataStart < 0 || dataStart > viewMain || tile.mainLen > viewMain - dataStart) {
            return nullptr;
        }
        const int tileMain0 = tile.offset - m_minMain;
        if (m_dir == Direction::Vertical) {
            if (tileMain0 < 0 || tileMain0 > outH || tile.mainLen > outH - tileMain0) {
                return nullptr;
            }
            for (int main = 0; main < tile.mainLen; ++main) {
                const int outputY = tileMain0 + main;
                const BYTE* src = view.bits +
                    static_cast<std::size_t>(dataStart + main) * view.stride;
                BYTE* dst = canvas.data() + static_cast<std::size_t>(outputY) * outW * 4u;
                std::memcpy(dst, src, static_cast<std::size_t>(cross) * 4u);
                for (int x = 0; x < cross; ++x) {
                    if (dst[x * 4 + 3] == 0) dst[x * 4 + 3] = 255;
                }
            }
        } else {
            if (tileMain0 < 0 || tileMain0 > outW || tile.mainLen > outW - tileMain0) return nullptr;
            for (int crossPos = 0; crossPos < cross; ++crossPos) {
                const BYTE* src = view.bits + static_cast<std::size_t>(crossPos) * view.stride +
                    static_cast<std::size_t>(dataStart) * 4u;
                BYTE* dst = canvas.data() +
                    (static_cast<std::size_t>(crossPos) * outW + tileMain0) * 4u;
                std::memcpy(dst, src, static_cast<std::size_t>(tile.mainLen) * 4u);
                for (int main = 0; main < tile.mainLen; ++main) {
                    if (dst[main * 4 + 3] == 0) dst[main * 4 + 3] = 255;
                }
            }
        }
    }

    return CreateDibFromPixels(outW, outH, canvas.data());
}

HBITMAP LongShotImage::RenderPreview(
    int maxWidth, int maxHeight, int* outWidth, int* outHeight) const {
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;
    if (m_tiles.empty() || !m_hasBounds || maxWidth <= 0 || maxHeight <= 0) return nullptr;

    const int mainLen = Length();
    const int cross = m_crossSize;
    if (mainLen <= 0 || cross <= 0) return nullptr;
    const int fullWidth = m_dir == Direction::Vertical ? cross : mainLen;
    const int fullHeight = m_dir == Direction::Vertical ? mainLen : cross;

    int previewWidth = fullWidth;
    int previewHeight = fullHeight;
    if (previewWidth > maxWidth || previewHeight > maxHeight) {
        const std::int64_t widthLimitedHeight =
            static_cast<std::int64_t>(fullHeight) * maxWidth;
        const std::int64_t heightLimitedWidth =
            static_cast<std::int64_t>(fullWidth) * maxHeight;
        if (widthLimitedHeight <= heightLimitedWidth) {
            previewWidth = maxWidth;
            previewHeight = static_cast<int>((widthLimitedHeight + fullWidth / 2) / fullWidth);
        } else {
            previewHeight = maxHeight;
            previewWidth = static_cast<int>((heightLimitedWidth + fullHeight / 2) / fullHeight);
        }
        previewWidth = (std::max)(1, (std::min)(previewWidth, maxWidth));
        previewHeight = (std::max)(1, (std::min)(previewHeight, maxHeight));
    }

    void* previewBits = nullptr;
    HBITMAP preview = CreateDib(previewWidth, previewHeight, &previewBits);
    if (!preview || !previewBits) {
        if (preview) DeleteObject(preview);
        return nullptr;
    }
    auto* previewPixels = static_cast<DWORD*>(previewBits);
    std::fill_n(previewPixels,
        static_cast<std::size_t>(previewWidth) * previewHeight, 0xff181818u);

    HDC screen = GetDC(nullptr);
    HDC destinationDc = screen ? CreateCompatibleDC(screen) : nullptr;
    HDC sourceDc = screen ? CreateCompatibleDC(screen) : nullptr;
    if (!screen || !destinationDc || !sourceDc) {
        if (sourceDc) DeleteDC(sourceDc);
        if (destinationDc) DeleteDC(destinationDc);
        if (screen) ReleaseDC(nullptr, screen);
        DeleteObject(preview);
        return nullptr;
    }

    HGDIOBJ oldDestination = SelectObject(destinationDc, preview);
    bool rendered = oldDestination && oldDestination != HGDI_ERROR;
    if (rendered) {
        SetStretchBltMode(destinationDc, HALFTONE);
        SetBrushOrgEx(destinationDc, 0, 0, nullptr);
    }

    const int previewMain = m_dir == Direction::Vertical ? previewHeight : previewWidth;
    for (const Tile& tile : m_tiles) {
        if (!rendered) break;
        BITMAP bitmap = {};
        const int objectBytes = GetObjectW(tile.bitmap, sizeof(bitmap), &bitmap);
        const int bitmapHeight = std::abs(bitmap.bmHeight);
        const int bitmapMain = m_dir == Direction::Vertical ? bitmapHeight : bitmap.bmWidth;
        const int bitmapCross = m_dir == Direction::Vertical ? bitmap.bmWidth : bitmapHeight;
        const long long logicalStart64 =
            static_cast<long long>(tile.offset) - static_cast<long long>(m_minMain);
        const long long logicalEnd64 = logicalStart64 + tile.mainLen;
        if (objectBytes != sizeof(bitmap) || !tile.bitmap || tile.mainLen <= 0 ||
            tile.crossLen != cross || bitmapCross != cross || tile.dataStart < 0 ||
            tile.dataStart > bitmapMain || tile.mainLen > bitmapMain - tile.dataStart ||
            logicalStart64 < 0 || logicalEnd64 > mainLen || logicalEnd64 <= logicalStart64) {
            rendered = false;
            break;
        }

        const int logicalStart = static_cast<int>(logicalStart64);
        const int logicalEnd = static_cast<int>(logicalEnd64);
        int destinationStart = static_cast<int>(
            static_cast<std::int64_t>(logicalStart) * previewMain / mainLen);
        int destinationEnd = static_cast<int>(
            (static_cast<std::int64_t>(logicalEnd) * previewMain + mainLen - 1) / mainLen);
        destinationStart = (std::max)(0, (std::min)(destinationStart, previewMain));
        destinationEnd = (std::max)(destinationStart + 1,
            (std::min)(destinationEnd, previewMain));
        if (destinationStart >= previewMain || destinationEnd <= destinationStart) continue;

        HGDIOBJ oldSource = SelectObject(sourceDc, tile.bitmap);
        if (!oldSource || oldSource == HGDI_ERROR) {
            rendered = false;
            break;
        }
        if (m_dir == Direction::Vertical) {
            rendered = StretchBlt(
                destinationDc,
                0, destinationStart, previewWidth, destinationEnd - destinationStart,
                sourceDc,
                0, tile.dataStart, tile.crossLen, tile.mainLen,
                SRCCOPY) != FALSE;
        } else {
            rendered = StretchBlt(
                destinationDc,
                destinationStart, 0, destinationEnd - destinationStart, previewHeight,
                sourceDc,
                tile.dataStart, 0, tile.mainLen, tile.crossLen,
                SRCCOPY) != FALSE;
        }
        SelectObject(sourceDc, oldSource);
    }

    if (oldDestination && oldDestination != HGDI_ERROR) {
        SelectObject(destinationDc, oldDestination);
    }
    DeleteDC(sourceDc);
    DeleteDC(destinationDc);
    ReleaseDC(nullptr, screen);
    if (!rendered) {
        DeleteObject(preview);
        return nullptr;
    }

    // Screen-capture DIBs do not guarantee a meaningful alpha byte. The live
    // preview is composited into a layered window, so make every preview pixel
    // explicitly opaque after all GDI operations have completed.
    GdiFlush();
    const std::size_t pixelCount =
        static_cast<std::size_t>(previewWidth) * previewHeight;
    for (std::size_t i = 0; i < pixelCount; ++i) previewPixels[i] |= 0xff000000u;

    if (outWidth) *outWidth = previewWidth;
    if (outHeight) *outHeight = previewHeight;
    return preview;
}

bool LongShotImage::CropMainAxis(int start, int end) {
    if (m_tiles.empty()) return false;
    const int len = Length();
    if (len <= 0 || start < 0 || end > len || end <= start) return false;
    if (start == 0 && end == len) return true;

    const int oldMinMain = m_minMain;
    const int oldContactOffset = m_contactOffset;
    const long long rawStart64 = static_cast<long long>(oldMinMain) + start;
    const long long rawEnd64 = static_cast<long long>(oldMinMain) + end;
    if (rawStart64 < (std::numeric_limits<int>::min)() ||
        rawEnd64 > (std::numeric_limits<int>::max)()) {
        return false;
    }
    const int rawStart = static_cast<int>(rawStart64);

    // Build a replacement tile store before mutating this object. AutoCrop can
    // run on a very large image; a GDI/allocation failure must preserve the
    // existing long screenshot rather than turn a recoverable trim failure into
    // data loss.
    const int cross = m_crossSize;
    if (cross <= 0) return false;
    LongShotImage replacement;
    replacement.m_dir = m_dir;
    replacement.m_crossSize = cross;
    try {
        replacement.m_tiles.reserve(m_tiles.size());
    } catch (const std::bad_alloc&) {
        return false;
    }

    for (const Tile& tile : m_tiles) {
        const int capacityMain = tile.capacityMain > 0 ? tile.capacityMain : tile.mainLen;
        const long long tileEnd64 = static_cast<long long>(tile.offset) + tile.mainLen;
        if (!tile.bitmap || tile.mainLen <= 0 || tile.crossLen != cross ||
            tile.dataStart < 0 || tile.dataStart > capacityMain ||
            tile.mainLen > capacityMain - tile.dataStart ||
            tileEnd64 > (std::numeric_limits<int>::max)() ||
            tileEnd64 < (std::numeric_limits<int>::min)()) {
            return false;
        }

        const int tileEnd = static_cast<int>(tileEnd64);
        const int copyStart = (std::max)(rawStart, tile.offset);
        const int copyEnd = (std::min)(static_cast<int>(rawEnd64), tileEnd);
        if (copyEnd <= copyStart) continue;

        const int copyLen = copyEnd - copyStart;
        const int sourceMainStart = tile.dataStart + (copyStart - tile.offset);
        if (!replacement.AddStripFromFrame(
                tile.bitmap, sourceMainStart, copyLen, copyStart, cross)) {
            return false;
        }
    }
    if (replacement.m_tiles.empty()) return false;

    std::sort(replacement.m_tiles.begin(), replacement.m_tiles.end(),
        [](const Tile& a, const Tile& b) { return a.offset < b.offset; });
    replacement.m_contactOffset = oldContactOffset;
    replacement.m_minMain = rawStart;
    replacement.m_maxMain = static_cast<int>(rawEnd64);
    replacement.m_hasBounds = true;
    replacement.m_lastAddAccepted = m_lastAddAccepted;
    SwapState(replacement);
    return true;
}

} // namespace longshot
