// Deterministic LongShot contracts: constants, signed placement, tile storage,
// materialized pixels, and main-axis crop. Uses only synthetic GDI DIBs.

#include "screenshot/longshot/LongShotStitcher.h"
#include "screenshot/longshot/LongShotExport.h"
#include "screenshot/longshot/LongShotScrollInjector.h"
#include "screenshot/longshot/LongShotTypes.h"
#include "screenshot/ScreenshotUtils.h"

#include <cstdint>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>
#include <windows.h>
#include <wincodec.h>

// Stub only what LongShotImage/Stitcher need from ScreenshotUtils.
namespace Screenshot {
BitmapSize GetBitmapSize(HBITMAP hBitmap) {
    BitmapSize size;
    if (!hBitmap) return size;
    BITMAP bm = {};
    if (GetObjectW(hBitmap, sizeof(bm), &bm)) {
        size.width = bm.bmWidth;
        size.height = std::abs(bm.bmHeight);
    }
    return size;
}

bool SaveBitmapToFile(
    HBITMAP,
    const std::wstring&,
    ScreenshotFormat,
    int,
    std::wstring*,
    bool) {
    // WebP/AVIF fallback is not exercised in this contract target.
    return false;
}
} // namespace Screenshot

static int g_failures = 0;

static_assert(!std::is_copy_constructible_v<longshot::LongShotImage>);
static_assert(!std::is_copy_assignable_v<longshot::LongShotImage>);

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::printf("  FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, (int)(b), (int)(a)); \
        ++g_failures; \
    } \
} while (0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++g_failures; \
    } \
} while (0)

static HBITMAP CreateDib(int w, int h, DWORD** outPixels = nullptr) {
    if (outPixels) *outPixels = nullptr;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        return nullptr;
    }
    if (outPixels) *outPixels = static_cast<DWORD*>(bits);
    return dib;
}

static DWORD DocumentPixel(int mainCoord, int crossCoord) {
    std::uint32_t value = static_cast<std::uint32_t>(mainCoord) * 0x9e3779b9u;
    value ^= static_cast<std::uint32_t>(crossCoord) * 0x85ebca6bu;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    const BYTE b = static_cast<BYTE>(value);
    const BYTE g = static_cast<BYTE>(value >> 8);
    const BYTE r = static_cast<BYTE>(value >> 16);
    return 0xff000000u | (static_cast<DWORD>(r) << 16) |
        (static_cast<DWORD>(g) << 8) | b;
}

static HBITMAP MakeDocumentFrame(
    longshot::Direction direction, int cross, int main, int documentMainStart) {
    const int width = direction == longshot::Direction::Vertical ? cross : main;
    const int height = direction == longshot::Direction::Vertical ? main : cross;
    DWORD* pixels = nullptr;
    HBITMAP dib = CreateDib(width, height, &pixels);
    if (!dib) return nullptr;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int mainCoord = direction == longshot::Direction::Vertical
                ? documentMainStart + y
                : documentMainStart + x;
            const int crossCoord = direction == longshot::Direction::Vertical ? x : y;
            pixels[static_cast<size_t>(y) * width + x] = DocumentPixel(mainCoord, crossCoord);
        }
    }
    return dib;
}

static HBITMAP MakeSolidBitmap(int w, int h, BYTE b, BYTE g, BYTE r) {
    DWORD* pixels = nullptr;
    HBITMAP dib = CreateDib(w, h, &pixels);
    if (!dib) return nullptr;
    const DWORD color = 0xff000000u | (static_cast<DWORD>(r) << 16) |
        (static_cast<DWORD>(g) << 8) | b;
    for (int i = 0; i < w * h; ++i) pixels[i] = color;
    return dib;
}

// A mostly-static viewport with two changing edge bands. Their union spans the
// full ROI while leaving most pixels static, exercising the absdiff comparison
// mask used by the offset scorer.
static HBITMAP MakeMaskedPatternFrame(int cross, int main, int documentMainStart) {
    DWORD* pixels = nullptr;
    HBITMAP dib = CreateDib(cross, main, &pixels);
    if (!dib) return nullptr;
    const DWORD background = 0xff202020u;
    for (int i = 0; i < cross * main; ++i) pixels[i] = background;
    for (int y = 0; y < main; ++y) {
        const int documentMain = documentMainStart + y;
        for (int x = 0; x < 12; ++x) {
            pixels[static_cast<size_t>(y) * cross + x] = DocumentPixel(documentMain, x);
            pixels[static_cast<size_t>(y) * cross + (cross - 12 + x)] =
                DocumentPixel(documentMain, cross - 12 + x);
        }
    }
    return dib;
}

static HBITMAP MakeMixedViewportFrame(
    longshot::Direction direction,
    int cross,
    int main,
    int documentMainStart,
    int dynamicSeed) {
    const int width = direction == longshot::Direction::Vertical ? cross : main;
    const int height = direction == longshot::Direction::Vertical ? main : cross;
    DWORD* pixels = nullptr;
    HBITMAP dib = CreateDib(width, height, &pixels);
    if (!dib) return nullptr;
    const int scrollingEnd = cross * 3 / 10;
    const int fixedEnd = cross * 2 / 5;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int mainPos = direction == longshot::Direction::Vertical ? y : x;
            const int crossPos = direction == longshot::Direction::Vertical ? x : y;
            DWORD pixel = 0;
            if (crossPos < scrollingEnd) {
                // The document body moves with the viewport.
                pixel = DocumentPixel(documentMainStart + mainPos, crossPos);
            } else if (crossPos < fixedEnd) {
                // A sticky sidebar stays at the same screen coordinates.
                pixel = DocumentPixel(50000 + mainPos, crossPos);
            } else {
                // Video/animated-ad content changes independently every frame.
                pixel = DocumentPixel(dynamicSeed * 10000 + mainPos, crossPos);
            }
            pixels[static_cast<size_t>(y) * width + x] = pixel;
        }
    }
    return dib;
}

static HBITMAP MakeMainAxisVideoFrame(
    longshot::Direction direction,
    int cross,
    int main,
    int documentMainStart,
    int dynamicSeed,
    bool stickyVideo = false) {
    const int width = direction == longshot::Direction::Vertical ? cross : main;
    const int height = direction == longshot::Direction::Vertical ? main : cross;
    DWORD* pixels = nullptr;
    HBITMAP dib = CreateDib(width, height, &pixels);
    if (!dib) return nullptr;
    const int videoDocumentEnd = main * 3 / 4;
    const int fixedSidebarStart = cross * 17 / 20;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int mainPos = direction == longshot::Direction::Vertical ? y : x;
            const int crossPos = direction == longshot::Direction::Vertical ? x : y;
            const int documentMain = documentMainStart + mainPos;
            DWORD pixel = 0;
            if (crossPos >= fixedSidebarStart) {
                // Sticky side chrome remains at the same screen coordinates.
                pixel = DocumentPixel(60000 + mainPos, crossPos);
            } else if ((stickyVideo ? mainPos : documentMain) < videoDocumentEnd) {
                // A document-embedded video scrolls with the page while its
                // pixels change independently between captured frames.
                pixel = DocumentPixel(dynamicSeed * 10000 + documentMain, crossPos);
            } else {
                pixel = DocumentPixel(documentMain, crossPos);
            }
            pixels[static_cast<size_t>(y) * width + x] = pixel;
        }
    }
    return dib;
}

static bool ReadPixels(HBITMAP bitmap, int& width, int& height, std::vector<DWORD>& pixels) {
    width = 0;
    height = 0;
    pixels.clear();
    BITMAP bm = {};
    if (!bitmap || !GetObjectW(bitmap, sizeof(bm), &bm)) return false;
    width = bm.bmWidth;
    height = std::abs(bm.bmHeight);
    if (width <= 0 || height <= 0) return false;
    pixels.resize(static_cast<size_t>(width) * height);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    const int copied = GetDIBits(screen, bitmap, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    return copied == height;
}

static void AssertMaterializedDocument(
    HBITMAP bitmap,
    longshot::Direction direction,
    int cross,
    int main,
    int documentMainStart) {
    int width = 0;
    int height = 0;
    std::vector<DWORD> pixels;
    ASSERT_TRUE(ReadPixels(bitmap, width, height, pixels));
    ASSERT_EQ(width, direction == longshot::Direction::Vertical ? cross : main);
    ASSERT_EQ(height, direction == longshot::Direction::Vertical ? main : cross);
    if (width <= 0 || height <= 0 || pixels.empty()) return;
    for (int crossCoord = 0; crossCoord < cross; ++crossCoord) {
        for (int mainCoord = 0; mainCoord < main; ++mainCoord) {
            const int x = direction == longshot::Direction::Vertical ? crossCoord : mainCoord;
            const int y = direction == longshot::Direction::Vertical ? mainCoord : crossCoord;
            const DWORD actual = pixels[static_cast<size_t>(y) * width + x];
            const DWORD expected = DocumentPixel(documentMainStart + mainCoord, crossCoord);
            ASSERT_TRUE(actual == expected);
        }
    }
}

static std::wstring ParentPath(std::wstring path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

static std::wstring ArtifactPath(const wchar_t* fileName) {
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring buildDir = ParentPath(ParentPath(ParentPath(std::wstring(executable))));
    const std::wstring artifacts = buildDir + L"\\artifacts";
    const std::wstring tests = artifacts + L"\\tests";
    CreateDirectoryW(artifacts.c_str(), nullptr);
    CreateDirectoryW(tests.c_str(), nullptr);
    return tests + L"\\" + fileName;
}

static bool ReadWicBgra(
    const std::wstring& path, int& width, int& height, std::vector<BYTE>& pixels) {
    width = 0;
    height = 0;
    pixels.clear();
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    UINT w = 0;
    UINT h = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);
    if (SUCCEEDED(hr) && (w == 0 || h == 0)) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        pixels.resize(static_cast<size_t>(w) * h * 4u);
        hr = converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(pixels.size()), pixels.data());
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (uninitialize) CoUninitialize();
    if (FAILED(hr)) {
        pixels.clear();
        return false;
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

static void AssertEncodedDocument(
    const std::wstring& path,
    longshot::Direction direction,
    int cross,
    int main,
    int documentMainStart) {
    int width = 0;
    int height = 0;
    std::vector<BYTE> pixels;
    ASSERT_TRUE(ReadWicBgra(path, width, height, pixels));
    ASSERT_EQ(width, direction == longshot::Direction::Vertical ? cross : main);
    ASSERT_EQ(height, direction == longshot::Direction::Vertical ? main : cross);
    if (width <= 0 || height <= 0 || pixels.empty()) return;
    int mismatchCount = 0;
    for (int crossCoord = 0; crossCoord < cross; ++crossCoord) {
        for (int mainCoord = 0; mainCoord < main; ++mainCoord) {
            const int x = direction == longshot::Direction::Vertical ? crossCoord : mainCoord;
            const int y = direction == longshot::Direction::Vertical ? mainCoord : crossCoord;
            const BYTE* p = pixels.data() + (static_cast<size_t>(y) * width + x) * 4u;
            const DWORD actual = (static_cast<DWORD>(p[3]) << 24) |
                (static_cast<DWORD>(p[2]) << 16) |
                (static_cast<DWORD>(p[1]) << 8) | p[0];
            const DWORD expected = DocumentPixel(documentMainStart + mainCoord, crossCoord);
            if (actual != expected) {
                if (mismatchCount < 3) {
                    std::printf("  PNG mismatch main=%d cross=%d expected=%08x actual=%08x\n",
                        mainCoord, crossCoord, expected, actual);
                }
                ++mismatchCount;
            }
        }
    }
    ASSERT_EQ(mismatchCount, 0);
}

// The streamed exporter deliberately reads our DIB-section tiles directly.
// Keep a separate contract for that storage view so an export failure can be
// distinguished from an encoder failure.
static void AssertTileStorageDocument(const longshot::LongShotImage& image) {
    ASSERT_TRUE(GdiFlush() != FALSE);
    int mismatchCount = 0;
    for (const longshot::Tile& tile : image.Tiles()) {
        DIBSECTION section = {};
        ASSERT_EQ(GetObjectW(tile.bitmap, sizeof(section), &section), sizeof(section));
        if (!section.dsBm.bmBits || section.dsBm.bmBitsPixel != 32 || tile.mainLen <= 0) continue;
        const int width = section.dsBm.bmWidth;
        const int height = std::abs(section.dsBm.bmHeight);
        const int stride = section.dsBm.bmWidthBytes;
        // LongShotImage owns these tiles and creates their direct storage
        // top-down.  GetObjectW is allowed to normalize dsBmih.biHeight, so
        // do not use it to reinterpret that storage contract.
        constexpr bool topDown = true;
        if (width <= 0 || height <= 0 || stride < width * 4) continue;
        for (int main = 0; main < tile.mainLen; ++main) {
            for (int cross = 0; cross < tile.crossLen; ++cross) {
                const int x = image.Dir() == longshot::Direction::Vertical ? cross : tile.dataStart + main;
                const int y = image.Dir() == longshot::Direction::Vertical ? tile.dataStart + main : cross;
                const int storageY = topDown ? y : height - y - 1;
                const BYTE* p = static_cast<const BYTE*>(section.dsBm.bmBits) +
                    static_cast<size_t>(storageY) * stride + static_cast<size_t>(x) * 4u;
                const DWORD actual = (static_cast<DWORD>(p[3]) << 24) |
                    (static_cast<DWORD>(p[2]) << 16) | (static_cast<DWORD>(p[1]) << 8) | p[0];
                const DWORD expected = DocumentPixel(tile.offset + main, cross);
                if (actual != expected) {
                    if (mismatchCount < 3) {
                        std::printf("  tile mismatch main=%d cross=%d expected=%08x actual=%08x\n",
                            tile.offset + main, cross, expected, actual);
                    }
                    ++mismatchCount;
                }
            }
        }
    }
    ASSERT_EQ(mismatchCount, 0);
}

static void test_gates() {
    ASSERT_EQ(longshot::kTimerMs, 100);
    ASSERT_EQ(longshot::kMatchFailThrottleMs, 2999);
    ASSERT_EQ(longshot::WheelUnitsForSpan(299), 25);
    ASSERT_EQ(longshot::WheelUnitsForSpan(300), 35);
    ASSERT_EQ(longshot::WheelUnitsForSpan(500), 45);
    ASSERT_EQ(longshot::WheelUnitsForSpan(700), 55);
    ASSERT_EQ(longshot::WheelUnitsForSpan(900), 65);
    ASSERT_EQ(longshot::kSuperLongWarnPx, 28000);
    ASSERT_EQ(longshot::kSaveSuperLongPx, 29000);
    ASSERT_EQ(longshot::kPinDisablePx, 28937);
    ASSERT_EQ(longshot::kJpgDialogLimitPx, 65001);
    ASSERT_EQ(longshot::kJpgQuickSaveLimitPx, 65000);
    ASSERT_EQ(longshot::kStitchHardMaxPx, 2000000);
    ASSERT_TRUE(longshot::kTileByteBudget == 0x08000000ull);
    ASSERT_TRUE(longshot::kSuperLongWarnPx < longshot::kPinDisablePx);
    ASSERT_TRUE(longshot::kPinDisablePx < longshot::kSaveSuperLongPx);
    ASSERT_EQ(longshot::ScrollDeltaForDirection(true, true, 25), -25);
    ASSERT_EQ(longshot::ScrollDeltaForDirection(true, false, 25), 25);
    ASSERT_EQ(longshot::ScrollDeltaForDirection(false, true, 25), 25);
    ASSERT_EQ(longshot::ScrollDeltaForDirection(false, false, 25), -25);
    ASSERT_EQ(static_cast<int>(longshot::StitchCode::ExtendedReverse), 0);
    ASSERT_EQ(static_cast<int>(longshot::StitchCode::ExtendedForward), 1);
    ASSERT_EQ(static_cast<int>(longshot::StitchCode::AcceptedNoExpand), 2);
    ASSERT_EQ(static_cast<int>(longshot::StitchCode::MaxLength), 3);
    ASSERT_EQ(static_cast<int>(longshot::StitchCode::MatchFail), 4);
}

static void test_top_down_dib_read_contract() {
    HBITMAP dib = MakeDocumentFrame(longshot::Direction::Vertical, 1, 3, 0);
    ASSERT_TRUE(dib != nullptr);
    int width = 0;
    int height = 0;
    std::vector<DWORD> pixels;
    ASSERT_TRUE(ReadPixels(dib, width, height, pixels));
    ASSERT_EQ(width, 1);
    ASSERT_EQ(height, 3);
    if (pixels.size() == 3u) {
        ASSERT_TRUE(pixels[0] == DocumentPixel(0, 0));
        ASSERT_TRUE(pixels[1] == DocumentPixel(1, 0));
        ASSERT_TRUE(pixels[2] == DocumentPixel(2, 0));
    }
    if (dib) DeleteObject(dib);
}

static void test_legacy_half_contact_overwrite() {
    constexpr int kCross = 8;
    constexpr int kFrameMain = 100;
    constexpr DWORD kOld = 0xff1e140au;
    constexpr DWORD kNew = 0xff463c32u;

    longshot::LongShotImage forward;
    ASSERT_EQ(static_cast<int>(forward.AddFirstFrame(
        MakeSolidBitmap(kCross, kFrameMain, 0x0a, 0x14, 0x1e),
        longshot::Direction::Vertical)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    // The forward legacy contact is source [50,100), placed at [90,140).
    // Its overlap [90,100) must overwrite the existing tile before append.
    ASSERT_EQ(static_cast<int>(forward.AddFrameAt(
        MakeSolidBitmap(kCross, kFrameMain, 0x32, 0x3c, 0x46),
        90, 50, kCross, 50, 40)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(forward.Length(), 140);
    HBITMAP full = forward.Materialize();
    int width = 0;
    int height = 0;
    std::vector<DWORD> pixels;
    ASSERT_TRUE(ReadPixels(full, width, height, pixels));
    ASSERT_EQ(width, kCross);
    ASSERT_EQ(height, 140);
    if (width == kCross && height == 140) {
        for (int y = 0; y < height; ++y) {
            ASSERT_TRUE(pixels[static_cast<size_t>(y) * width] == (y < 90 ? kOld : kNew));
        }
    }
    if (full) DeleteObject(full);

    // A fully covered contact returns code 2 and must not repaint tiles.
    ASSERT_EQ(static_cast<int>(forward.AddFrameAt(
        MakeSolidBitmap(kCross, kFrameMain, 0xff, 0x00, 0x00),
        20, 60, kCross, 20, 20)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    full = forward.Materialize();
    ASSERT_TRUE(ReadPixels(full, width, height, pixels));
    if (width == kCross && height == 140) {
        ASSERT_TRUE(pixels[static_cast<size_t>(20) * width] == kOld);
        ASSERT_TRUE(pixels[static_cast<size_t>(100) * width] == kNew);
    }
    if (full) DeleteObject(full);

    longshot::LongShotImage reverse;
    ASSERT_EQ(static_cast<int>(reverse.AddFirstFrame(
        MakeSolidBitmap(kCross, kFrameMain, 0x0a, 0x14, 0x1e),
        longshot::Direction::Vertical)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    // The reverse legacy contact is source [0,50), placed at [-40,10).
    ASSERT_EQ(static_cast<int>(reverse.AddFrameAt(
        MakeSolidBitmap(kCross, kFrameMain, 0x32, 0x3c, 0x46),
        -40, 50, kCross, 0, -40)),
        static_cast<int>(longshot::StitchCode::ExtendedReverse));
    full = reverse.Materialize();
    ASSERT_TRUE(ReadPixels(full, width, height, pixels));
    ASSERT_EQ(width, kCross);
    ASSERT_EQ(height, 140);
    if (width == kCross && height == 140) {
        for (int y = 0; y < height; ++y) {
            ASSERT_TRUE(pixels[static_cast<size_t>(y) * width] == (y < 50 ? kNew : kOld));
        }
    }
    if (full) DeleteObject(full);
}

static void test_vertical_forward_pixels() {
    constexpr int kCross = 96;
    constexpr int kFrameMain = 160;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_TRUE(stitcher.LastResultWasFirstFrame());
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_TRUE(!stitcher.LastResultWasFirstFrame());
    ASSERT_EQ(stitcher.LastDispFull(), 40);
    ASSERT_EQ(stitcher.Length(), 200);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.LastDispFull(), 40);
    ASSERT_EQ(stitcher.Length(), 240);
    ASSERT_TRUE(stitcher.Image().StoredBytes() == static_cast<size_t>(kCross) * 240u * 4u);
    HBITMAP full = stitcher.Image().Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Vertical, kCross, 240, 0);
        DeleteObject(full);
    }
}

static void test_vertical_reverse_pixels() {
    constexpr int kCross = 96;
    constexpr int kFrameMain = 160;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 120), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedReverse));
    ASSERT_EQ(stitcher.LastDispFull(), -40);
    ASSERT_EQ(stitcher.Length(), 200);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedReverse));
    ASSERT_EQ(stitcher.LastDispFull(), -40);
    ASSERT_EQ(stitcher.Length(), 240);
    HBITMAP full = stitcher.Image().Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Vertical, kCross, 240, 40);
        DeleteObject(full);
    }
}

static void test_horizontal_forward_pixels() {
    constexpr int kCross = 64;
    constexpr int kFrameMain = 160;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Horizontal);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.Length(), 240);
    HBITMAP full = stitcher.Image().Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Horizontal, kCross, 240, 0);
        DeleteObject(full);
    }
}

static void test_large_valid_displacement() {
    constexpr int kCross = 96;
    constexpr int kFrameMain = 180;
    constexpr int kStep = 105; // > 1/2 frame, but below the evidence-backed 2/3 gate.

    longshot::LongShotStitcher vertical;
    vertical.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(vertical.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(vertical.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, kStep), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(vertical.LastDispFull(), kStep);
    ASSERT_EQ(vertical.Length(), kFrameMain + kStep);

    longshot::LongShotStitcher horizontal;
    horizontal.SetDirection(longshot::Direction::Horizontal);
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, kStep), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(horizontal.LastDispFull(), kStep);
    ASSERT_EQ(horizontal.Length(), kFrameMain + kStep);

    constexpr int kFastStep = 160; // 88.9% frame: fast wheel, but still 20px overlap.
    longshot::LongShotStitcher fastWheel;
    fastWheel.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(fastWheel.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(fastWheel.TryAddImage(
        MakeDocumentFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, kFastStep), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(fastWheel.LastDispFull(), kFastStep);
    ASSERT_EQ(fastWheel.Length(), kFrameMain + kFastStep);

    longshot::LongShotStitcher fastHorizontal;
    fastHorizontal.SetDirection(longshot::Direction::Horizontal);
    ASSERT_EQ(static_cast<int>(fastHorizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(fastHorizontal.TryAddImage(
        MakeDocumentFrame(
            longshot::Direction::Horizontal, kCross, kFrameMain, kFastStep), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(fastHorizontal.LastDispFull(), kFastStep);

    // Once a complete viewport has been skipped there is no pixel evidence to
    // reconstruct the missing strip; recovery must reject rather than guess.
    longshot::LongShotStitcher noOverlap;
    noOverlap.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(noOverlap.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(noOverlap.TryAddImage(
        MakeDocumentFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, kFrameMain), 1000, false)),
        static_cast<int>(longshot::StitchCode::MatchFail));
}

static void test_diff_masked_displacement() {
    constexpr int kCross = 128;
    constexpr int kFrameMain = 192;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMaskedPatternFrame(kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMaskedPatternFrame(kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.LastDispFull(), 40);
    ASSERT_EQ(stitcher.Length(), kFrameMain + 40);
}

static void test_mixed_fixed_dynamic_regions() {
    // Real browser captures commonly combine a scrolling article, a sticky
    // sidebar, and video/animated-ad pixels.  A whole-frame mismatch ratio is
    // above 0.3 even at the correct offset; independent scrolling bands still
    // provide an unambiguous displacement consensus.
    constexpr int kCross = 240;
    constexpr int kFrameMain = 192;
    constexpr int kStep = 40;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMixedViewportFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, 0, 1), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMixedViewportFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, kStep, 2), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.LastDispFull(), kStep);
    ASSERT_EQ(stitcher.Length(), kFrameMain + kStep);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMixedViewportFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, kStep * 2, 3), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.LastDispFull(), kStep);
    ASSERT_EQ(stitcher.Length(), kFrameMain + kStep * 2);

    longshot::LongShotStitcher horizontal;
    horizontal.SetDirection(longshot::Direction::Horizontal);
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeMixedViewportFrame(
            longshot::Direction::Horizontal, kCross, kFrameMain, 0, 11), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeMixedViewportFrame(
            longshot::Direction::Horizontal, kCross, kFrameMain, kStep, 12), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(horizontal.LastDispFull(), kStep);
    ASSERT_EQ(horizontal.Length(), kFrameMain + kStep);
}

static void test_main_axis_video_region() {
    // A wide embedded video can contaminate every cross-axis stripe at once.
    // The static article region below it must still provide enough spatially
    // independent evidence to continue stitching.
    constexpr int kCross = 240;
    constexpr int kFrameMain = 192;
    constexpr int kStep = 32;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, 0, 1), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, kStep, 2), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.LastDispFull(), kStep);
    ASSERT_EQ(stitcher.Length(), kFrameMain + kStep);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Vertical, kCross, kFrameMain, kStep * 2, 3), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.LastDispFull(), kStep);
    ASSERT_EQ(stitcher.Length(), kFrameMain + kStep * 2);

    longshot::LongShotStitcher horizontal;
    horizontal.SetDirection(longshot::Direction::Horizontal);
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Horizontal, kCross, kFrameMain, 0, 11), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Horizontal, kCross, kFrameMain, kStep, 12), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(horizontal.LastDispFull(), kStep);

    constexpr int kRealCross = 1240;
    constexpr int kRealMain = 1060;
    constexpr int kRealStep = 70;
    longshot::LongShotStitcher realistic;
    realistic.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(realistic.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Vertical, kRealCross, kRealMain, 0, 21), 5000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(realistic.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Vertical, kRealCross, kRealMain, kRealStep, 22), 5000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(realistic.LastDispFull(), kRealStep);

    longshot::LongShotStitcher stickyVideo;
    stickyVideo.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stickyVideo.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Vertical, kRealCross, kRealMain, 0, 31, true),
        5000, false)), static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stickyVideo.TryAddImage(
        MakeMainAxisVideoFrame(
            longshot::Direction::Vertical, kRealCross, kRealMain, kRealStep, 32, true),
        5000, false)), static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stickyVideo.LastDispFull(), kRealStep);
}

static void test_feature_cross_axis_clamp() {
    // Clamp only post-transpose feature columns to 512. With a 1024px
    // cross axis, scaling the main axis as well would quantize this 37px move.
    constexpr int kCross = 1024;
    constexpr int kFrameMain = 151;
    constexpr int kStep = 37;

    longshot::LongShotStitcher vertical;
    vertical.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(vertical.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(vertical.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, kStep), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(vertical.LastDispFull(), kStep);
    ASSERT_EQ(vertical.Length(), kFrameMain + kStep);

    longshot::LongShotStitcher horizontal;
    horizontal.SetDirection(longshot::Direction::Horizontal);
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, kFrameMain, kStep), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(horizontal.LastDispFull(), kStep);
    ASSERT_EQ(horizontal.Length(), kFrameMain + kStep);
}

static void test_match_fail_and_max_length() {
    longshot::LongShotStitcher stationary;
    stationary.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stationary.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, 80, 100, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stationary.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, 80, 100, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(stationary.LastDispFull(), 0);
    ASSERT_EQ(stationary.Length(), 100);

    longshot::LongShotStitcher noise;
    noise.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(noise.TryAddImage(MakeSolidBitmap(100, 100, 10, 10, 10), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(noise.TryAddImage(MakeSolidBitmap(100, 100, 200, 50, 50), 1000, false)),
        static_cast<int>(longshot::StitchCode::MatchFail));
    ASSERT_EQ(noise.LastDispFull(), 0);

    longshot::LongShotStitcher unrelated;
    unrelated.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(unrelated.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, 240, 192, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(unrelated.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, 240, 192, 10000), 1000, false)),
        static_cast<int>(longshot::StitchCode::MatchFail));
    ASSERT_EQ(unrelated.LastDispFull(), 0);

    longshot::LongShotStitcher capped;
    capped.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(capped.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, 80, 100, 0), 50, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(capped.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, 80, 100, 30), 50, false)),
        static_cast<int>(longshot::StitchCode::MaxLength));
}

static void test_trim_anchor_protocol() {
    constexpr int kCross = 96;
    constexpr int kFrameMain = 160;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);

    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(stitcher.TrimAnchor(), 0);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.TrimAnchor(), 40);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.TrimAnchor(), 80);

    // Code 2 is a covered contact, not a first-frame reset. It advances the
    // same logical trim anchor by the signed displacement.
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(stitcher.TrimAnchor(), 40);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(stitcher.TrimAnchor(), 0);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, -40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedReverse));
    ASSERT_EQ(stitcher.TrimAnchor(), 0);
}

static void test_auto_crop_policy() {
    using longshot::LongShotAutoCropState;
    using longshot::PlanLongShotAutoCrop;
    using longshot::StitchCode;

    LongShotAutoCropState state;
    auto plan = PlanLongShotAutoCrop(
        state, StitchCode::AcceptedNoExpand, true, true, 0, 160, 160);
    ASSERT_TRUE(!plan.HasCrop());
    ASSERT_EQ(state.scrollTrend, 0);
    ASSERT_EQ(state.trimAnchorSnapshot, 0);

    // Forward trend then reverse result: retain the image head through
    // anchor+captureSpan.
    plan = PlanLongShotAutoCrop(state, StitchCode::ExtendedForward, false, true, 40, 200, 160);
    ASSERT_TRUE(!plan.HasCrop());
    ASSERT_EQ(state.scrollTrend, 1);
    ASSERT_EQ(state.trimAnchorSnapshot, 40);
    plan = PlanLongShotAutoCrop(state, StitchCode::ExtendedForward, false, true, 80, 240, 160);
    ASSERT_TRUE(!plan.HasCrop());
    ASSERT_EQ(state.trimAnchorSnapshot, 80);
    plan = PlanLongShotAutoCrop(state, StitchCode::ExtendedReverse, false, true, 0, 280, 160);
    ASSERT_TRUE(plan.HasCrop());
    ASSERT_EQ(plan.cropStart, 0);
    ASSERT_EQ(plan.cropEnd, 160);
    ASSERT_EQ(state.scrollTrend, 1); // Cropping does not flip the scroll trend.

    // Reverse trend then forward result: remove the stale head at the trim
    // anchor and preserve the tail.
    state = {};
    PlanLongShotAutoCrop(state, StitchCode::AcceptedNoExpand, true, true, 0, 160, 160);
    PlanLongShotAutoCrop(state, StitchCode::ExtendedReverse, false, true, 0, 200, 160);
    ASSERT_EQ(state.scrollTrend, -1);
    plan = PlanLongShotAutoCrop(state, StitchCode::ExtendedForward, false, true, 40, 280, 160);
    ASSERT_TRUE(plan.HasCrop());
    ASSERT_EQ(plan.cropStart, 40);
    ASSERT_EQ(plan.cropEnd, 280);
    ASSERT_EQ(state.scrollTrend, -1);

    // A covered code-2 contact with the same anchor is not a fresh frame and
    // does not crop or reinitialize the established trend.
    state = {};
    PlanLongShotAutoCrop(state, StitchCode::AcceptedNoExpand, true, true, 0, 160, 160);
    PlanLongShotAutoCrop(state, StitchCode::ExtendedForward, false, true, 40, 200, 160);
    plan = PlanLongShotAutoCrop(state, StitchCode::AcceptedNoExpand, false, true, 40, 200, 160);
    ASSERT_TRUE(!plan.HasCrop());
    ASSERT_EQ(state.scrollTrend, 1);
    ASSERT_EQ(state.trimAnchorSnapshot, 40);

    // When the image becomes almost one viewport long, clear the old
    // trend and lets this same result establish the new one.
    state = { 1, 20 };
    plan = PlanLongShotAutoCrop(state, StitchCode::ExtendedReverse, false, true, 0, 162, 160);
    ASSERT_TRUE(!plan.HasCrop());
    ASSERT_EQ(state.scrollTrend, -1);
    ASSERT_EQ(state.trimAnchorSnapshot, 0);
}

static void test_trim_anchor_rebase_after_crop() {
    constexpr int kCross = 96;
    constexpr int kFrameMain = 160;
    longshot::LongShotStitcher head;
    head.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(head.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(head.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(static_cast<int>(head.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(head.TrimAnchor(), 80);
    ASSERT_TRUE(head.Image().CropMainAxis(0, 200));
    head.RebaseTrimAnchorAfterCrop(0, kFrameMain);
    ASSERT_EQ(head.TrimAnchor(), 40); // max(0, end-captureSpan)

    longshot::LongShotStitcher tail;
    tail.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(tail.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(tail.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(static_cast<int>(tail.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_TRUE(tail.Image().CropMainAxis(80, 240));
    tail.RebaseTrimAnchorAfterCrop(80, kFrameMain);
    ASSERT_EQ(tail.TrimAnchor(), 160); // max(0, oldLength-start)
}

static void test_crop_main_axis() {
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, 40, 100, 0), 0, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_TRUE(stitcher.Image().CropMainAxis(20, 80));
    ASSERT_EQ(stitcher.Length(), 60);
    HBITMAP full = stitcher.Image().Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Vertical, 40, 60, 20);
        DeleteObject(full);
    }
    // Crop must preserve its raw logical origin/contact so subsequent frames do
    // not restart at zero and overwrite the retained document segment.
    ASSERT_EQ(stitcher.Image().MinMain(), 20);
    ASSERT_EQ(stitcher.Image().MaxMain(), 80);
    ASSERT_EQ(static_cast<int>(stitcher.Image().AddFrameAt(
        MakeDocumentFrame(longshot::Direction::Vertical, 40, 100, 80),
        80, 100, 40)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(stitcher.Image().Length(), 160);
    full = stitcher.Image().Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Vertical, 40, 160, 20);
        DeleteObject(full);
    }
    ASSERT_TRUE(!stitcher.Image().CropMainAxis(10, 10));
}

static void test_crop_main_axis_horizontal() {
    longshot::LongShotImage image;
    constexpr int kCross = 40;
    ASSERT_EQ(static_cast<int>(image.AddFirstFrame(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, 100, 0),
        longshot::Direction::Horizontal)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_TRUE(image.CropMainAxis(20, 80));
    ASSERT_EQ(image.Length(), 60);
    ASSERT_EQ(image.MinMain(), 20);
    ASSERT_EQ(image.MaxMain(), 80);

    HBITMAP full = image.Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Horizontal, kCross, 60, 20);
        DeleteObject(full);
    }

    ASSERT_EQ(static_cast<int>(image.AddFrameAt(
        MakeDocumentFrame(longshot::Direction::Horizontal, kCross, 100, 80),
        80, 100, kCross)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(image.Length(), 160);
    full = image.Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Horizontal, kCross, 160, 20);
        DeleteObject(full);
    }
}

static void test_bounded_cumulative_preview() {
    constexpr int kCross = 8;
    constexpr int kFrameMain = 100;
    constexpr DWORD kOld = 0xff1e140au;
    constexpr DWORD kNew = 0xff463c32u;

    longshot::LongShotImage vertical;
    ASSERT_EQ(static_cast<int>(vertical.AddFirstFrame(
        MakeSolidBitmap(kCross, kFrameMain, 0x0a, 0x14, 0x1e),
        longshot::Direction::Vertical)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(vertical.AddFrameAt(
        MakeSolidBitmap(kCross, kFrameMain, 0x32, 0x3c, 0x46),
        90, 50, kCross, 50, 40)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));

    int previewWidth = 0;
    int previewHeight = 0;
    HBITMAP preview = vertical.RenderPreview(80, 70, &previewWidth, &previewHeight);
    ASSERT_TRUE(preview != nullptr);
    ASSERT_EQ(previewWidth, 4);
    ASSERT_EQ(previewHeight, 70);
    int width = 0;
    int height = 0;
    std::vector<DWORD> pixels;
    ASSERT_TRUE(ReadPixels(preview, width, height, pixels));
    if (width == previewWidth && height == previewHeight && !pixels.empty()) {
        ASSERT_TRUE(pixels[0] == kOld);
        ASSERT_TRUE(pixels[static_cast<size_t>(height - 1) * width] == kNew);
    }
    if (preview) DeleteObject(preview);

    longshot::LongShotImage horizontal;
    ASSERT_EQ(static_cast<int>(horizontal.AddFirstFrame(
        MakeSolidBitmap(kFrameMain, kCross, 0x0a, 0x14, 0x1e),
        longshot::Direction::Horizontal)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(horizontal.AddFrameAt(
        MakeSolidBitmap(kFrameMain, kCross, 0x32, 0x3c, 0x46),
        90, 50, kCross, 50, 40)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    preview = horizontal.RenderPreview(70, 80, &previewWidth, &previewHeight);
    ASSERT_TRUE(preview != nullptr);
    ASSERT_EQ(previewWidth, 70);
    ASSERT_EQ(previewHeight, 4);
    ASSERT_TRUE(ReadPixels(preview, width, height, pixels));
    if (width == previewWidth && height == previewHeight && !pixels.empty()) {
        ASSERT_TRUE(pixels[0] == kOld);
        ASSERT_TRUE(pixels[static_cast<size_t>(width - 1)] == kNew);
    }
    if (preview) DeleteObject(preview);
}

static void test_incremental_tiles_coalesce() {
    constexpr int kCross = 16;
    constexpr int kFrameMain = 128;
    constexpr int kStep = 8;
    constexpr int kLastStart = 8192;
    longshot::LongShotImage image;
    ASSERT_EQ(static_cast<int>(image.AddFirstFrame(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0),
        longshot::Direction::Vertical)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    for (int start = kStep; start <= kLastStart; start += kStep) {
        ASSERT_EQ(static_cast<int>(image.AddFrameAt(
            MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, start),
            start, kFrameMain, kCross)),
            static_cast<int>(longshot::StitchCode::ExtendedForward));
    }
    const int expectedMain = kLastStart + kFrameMain;
    ASSERT_EQ(image.Length(), expectedMain);
    ASSERT_TRUE(image.StoredBytes() == static_cast<size_t>(kCross) * expectedMain * 4u);
    // A per-delta HBITMAP implementation would have 1025 tiles here. The
    // coalescer must retain a bounded number of appendable GDI chunks instead.
    ASSERT_EQ(static_cast<int>(image.Tiles().size()), 1);
    ASSERT_TRUE(image.Tiles()[0].capacityMain >= image.Tiles()[0].mainLen);
    HBITMAP full = image.Materialize();
    ASSERT_TRUE(full != nullptr);
    if (full) {
        AssertMaterializedDocument(full, longshot::Direction::Vertical, kCross, expectedMain, 0);
        DeleteObject(full);
    }
}

static void test_realistic_growth_preview() {
    // A roughly full-HD browser selection crosses the geometric tile growth
    // boundary near 3,000px.  Keep exercising both the contact overwrite and
    // bounded preview paths after that boundary; this is where cloning the
    // complete reserved backing DIB used to make live capture stall.
    constexpr int kCross = 1240;
    constexpr int kFrameMain = 1060;
    constexpr int kStep = 70;
    constexpr int kLastStart = 3500;
    constexpr int kHalf = kFrameMain / 2;

    longshot::LongShotImage image;
    ASSERT_EQ(static_cast<int>(image.AddFirstFrame(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0),
        longshot::Direction::Vertical)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));

    for (int start = kStep; start <= kLastStart; start += kStep) {
        ASSERT_EQ(static_cast<int>(image.AddFrameAt(
            MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, start),
            start + kHalf, kHalf, kCross, kHalf, start)),
            static_cast<int>(longshot::StitchCode::ExtendedForward));
        ASSERT_EQ(image.Length(), start + kFrameMain);
    }

    ASSERT_TRUE(!image.Tiles().empty());
    ASSERT_TRUE(image.Tiles().front().capacityMain >= 4000);
    int previewWidth = 0;
    int previewHeight = 0;
    HBITMAP preview = image.RenderPreview(208, 448, &previewWidth, &previewHeight);
    ASSERT_TRUE(preview != nullptr);
    ASSERT_TRUE(previewWidth > 0 && previewWidth <= 208);
    ASSERT_TRUE(previewHeight > 0 && previewHeight <= 448);
    if (preview) DeleteObject(preview);
}

static void test_streamed_png_and_cancel_transaction() {
    constexpr int kCross = 96;
    constexpr int kFrameMain = 160;
    longshot::LongShotStitcher stitcher;
    stitcher.SetDirection(longshot::Direction::Vertical);
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(static_cast<int>(stitcher.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Vertical, kCross, kFrameMain, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    AssertTileStorageDocument(stitcher.Image());

    const std::wstring encoded = ArtifactPath(L"longshot-stream-contract.png");
    DeleteFileW(encoded.c_str());
    std::atomic<bool> cancel{false};
    std::wstring error;
    ASSERT_TRUE(longshot::SaveLongShotImageToFile(
        stitcher.Image(), encoded, ScreenshotFormat::Png, 95, &cancel, {}, &error));
    AssertEncodedDocument(encoded, longshot::Direction::Vertical, kCross, 240, 0);
    DeleteFileW(encoded.c_str());

    const std::wstring preserved = ArtifactPath(L"longshot-cancel-contract.png");
    const BYTE sentinel[] = { 0x5a, 0x43, 0x50, 0x49, 0x4e };
    HANDLE file = CreateFileW(
        preserved.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ASSERT_TRUE(WriteFile(file, sentinel, sizeof(sentinel), &written, nullptr));
        ASSERT_EQ(written, sizeof(sentinel));
        CloseHandle(file);
    }
    cancel = false;
    error.clear();
    const bool cancelled = longshot::SaveLongShotImageToFile(
        stitcher.Image(), preserved, ScreenshotFormat::Png, 95, &cancel,
        [&cancel](int) { cancel = true; }, &error);
    ASSERT_TRUE(!cancelled);
    file = CreateFileW(preserved.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) {
        BYTE read[sizeof(sentinel)] = {};
        DWORD count = 0;
        ASSERT_TRUE(ReadFile(file, read, sizeof(read), &count, nullptr));
        ASSERT_EQ(count, sizeof(sentinel));
        for (size_t i = 0; i < sizeof(sentinel); ++i) ASSERT_EQ(read[i], sentinel[i]);
        CloseHandle(file);
    }
    DeleteFileW(preserved.c_str());

    longshot::LongShotStitcher horizontal;
    horizontal.SetDirection(longshot::Direction::Horizontal);
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, 128, 160, 0), 1000, false)),
        static_cast<int>(longshot::StitchCode::AcceptedNoExpand));
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, 128, 160, 40), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    ASSERT_EQ(static_cast<int>(horizontal.TryAddImage(
        MakeDocumentFrame(longshot::Direction::Horizontal, 128, 160, 80), 1000, false)),
        static_cast<int>(longshot::StitchCode::ExtendedForward));
    AssertTileStorageDocument(horizontal.Image());
    const std::wstring horizontalPath = ArtifactPath(L"longshot-stream-horizontal-contract.png");
    DeleteFileW(horizontalPath.c_str());
    cancel = false;
    error.clear();
    ASSERT_TRUE(longshot::SaveLongShotImageToFile(
        horizontal.Image(), horizontalPath, ScreenshotFormat::Png, 95, &cancel, {}, &error));
    AssertEncodedDocument(horizontalPath, longshot::Direction::Horizontal, 128, 240, 0);
    DeleteFileW(horizontalPath.c_str());
}

int main() {
    std::printf("test_longshot_stitch_contract\n");
    test_gates();
    test_top_down_dib_read_contract();
    test_legacy_half_contact_overwrite();
    test_vertical_forward_pixels();
    test_vertical_reverse_pixels();
    test_horizontal_forward_pixels();
    test_large_valid_displacement();
    test_diff_masked_displacement();
    test_mixed_fixed_dynamic_regions();
    test_main_axis_video_region();
    test_feature_cross_axis_clamp();
    test_match_fail_and_max_length();
    test_trim_anchor_protocol();
    test_auto_crop_policy();
    test_trim_anchor_rebase_after_crop();
    test_crop_main_axis();
    test_crop_main_axis_horizontal();
    test_bounded_cumulative_preview();
    test_incremental_tiles_coalesce();
    test_realistic_growth_preview();
    test_streamed_png_and_cancel_transaction();
    if (g_failures == 0) {
        std::printf("  ALL PASS\n");
        return 0;
    }
    std::printf("  %d failure(s)\n", g_failures);
    return 1;
}
