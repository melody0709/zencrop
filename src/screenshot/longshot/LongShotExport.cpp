#include "LongShotExport.h"

#include "screenshot/ScreenshotUtils.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

namespace longshot {
namespace {

constexpr UINT kEncodeBandMaxRows = 64;
constexpr std::size_t kEncodeBandByteBudget = 16u * 1024u * 1024u;

class ScopedComInitialization {
public:
    ScopedComInitialization()
        : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
          m_uninitialize(SUCCEEDED(m_result)) {}
    ~ScopedComInitialization() {
        if (m_uninitialize) CoUninitialize();
    }

    bool IsUsable() const {
        return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT m_result = E_FAIL;
    bool m_uninitialize = false;
};

bool IsCancelled(const std::atomic<bool>* cancel) {
    return cancel && cancel->load();
}

void SetError(std::wstring* error, const wchar_t* value) noexcept {
    if (!error) return;
    try {
        *error = value ? value : L"Long screenshot export failed.";
    } catch (...) {
        error->clear();
    }
}

void SetError(std::wstring* error, std::wstring value) noexcept {
    if (!error) return;
    try {
        *error = std::move(value);
    } catch (...) {
        error->clear();
    }
}

UINT EncodeBandRowsFor(UINT width, bool needsBgr24) {
    if (width == 0) return 1;
    const std::size_t bytesPerPixel = needsBgr24 ? 7u : 4u;
    const std::size_t widthBytes = static_cast<std::size_t>(width);
    if (widthBytes > (std::numeric_limits<std::size_t>::max)() / bytesPerPixel) return 1;
    const std::size_t bytesPerRow = widthBytes * bytesPerPixel;
    const std::size_t rowsByBudget = bytesPerRow == 0 ? 1 : kEncodeBandByteBudget / bytesPerRow;
    return static_cast<UINT>((std::max)(std::size_t{1},
        (std::min)(static_cast<std::size_t>(kEncodeBandMaxRows), rowsByBudget)));
}

std::wstring TempSiblingPath(const std::wstring& destinationPath) {
    static std::atomic<unsigned long> counter{0};
    return destinationPath + L".zencrop-longshot-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64()) + L"-" +
        std::to_wstring(++counter) + L".tmp";
}

bool GetOutputSize(const LongShotImage& image, UINT& width, UINT& height) {
    const int main = image.Length();
    const int cross = image.CrossSize();
    if (main <= 0 || cross <= 0) return false;
    const int w = image.Dir() == Direction::Vertical ? cross : main;
    const int h = image.Dir() == Direction::Vertical ? main : cross;
    if (w <= 0 || h <= 0) return false;
    width = static_cast<UINT>(w);
    height = static_cast<UINT>(h);
    return true;
}

bool ReadBitmapRect(
    HBITMAP bitmap,
    int bitmapWidth,
    int bitmapHeight,
    int sourceX,
    int firstRow,
    int copyWidth,
    int rowCount,
    BYTE* destination,
    std::size_t destinationStride) {
    if (!bitmap || bitmapWidth <= 0 || bitmapHeight <= 0 || sourceX < 0 || firstRow < 0 ||
        copyWidth <= 0 || rowCount <= 0 || sourceX > bitmapWidth ||
        copyWidth > bitmapWidth - sourceX || firstRow > bitmapHeight ||
        rowCount > bitmapHeight - firstRow || !destination ||
        destinationStride < static_cast<std::size_t>(copyWidth) * 4u) {
        return false;
    }

    // All LongShot tiles are our own 32bpp top-down DIB sections. Reading the
    // bits directly avoids both GetDIBits' inverted scan-line convention and a
    // full-capacity temporary buffer when a horizontal tile has spare capacity.
    DIBSECTION section = {};
    if (GetObjectW(bitmap, sizeof(section), &section) == sizeof(section) &&
        section.dsBm.bmBits && section.dsBm.bmBitsPixel == 32 &&
        section.dsBm.bmWidth == bitmapWidth &&
        std::abs(section.dsBm.bmHeight) == bitmapHeight) {
        const int sourceStride = section.dsBm.bmWidthBytes;
        if (sourceStride < bitmapWidth * 4) return false;
        // CreateDib() establishes the storage convention for every tile in this
        // class: the pointer's first row is logical row zero.  GetObjectW may
        // normalize dsBmih.biHeight to a positive magnitude on this GDI path,
        // so that field cannot safely be used to re-infer the scan direction.
        constexpr bool topDown = true;
        const auto* bits = static_cast<const BYTE*>(section.dsBm.bmBits);
        const std::size_t copyBytes = static_cast<std::size_t>(copyWidth) * 4u;
        for (int row = 0; row < rowCount; ++row) {
            const int logicalRow = firstRow + row;
            const int storageRow = topDown ? logicalRow : bitmapHeight - logicalRow - 1;
            const BYTE* src = bits + static_cast<std::size_t>(storageRow) * sourceStride +
                static_cast<std::size_t>(sourceX) * 4u;
            BYTE* dst = destination + static_cast<std::size_t>(row) * destinationStride;
            std::memcpy(dst, src, copyBytes);
        }
        return true;
    }

    // This should not be reached for tile storage, but preserve a safe fallback
    // for exact full-width DIBs instead of silently emitting mis-addressed rows.
    if (sourceX != 0 || copyWidth != bitmapWidth) return false;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bitmapWidth;
    bmi.bmiHeader.biHeight = -bitmapHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    // GetDIBits' start scan is addressed from the DIB's storage origin even
    // when the requested output BMI is top-down. Convert our top-down row
    // range explicitly; otherwise each 64-row band begins at the tile bottom.
    const UINT storageStart = static_cast<UINT>(bitmapHeight - firstRow - rowCount);
    const int copied = GetDIBits(
        screen, bitmap, storageStart, static_cast<UINT>(rowCount),
        destination, &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    return copied == rowCount;
}

bool ComposeBand(
    const LongShotImage& image,
    int bandTop,
    int bandRows,
    int outputWidth,
    std::vector<BYTE>& pixels) {
    if (bandTop < 0 || bandRows <= 0 || outputWidth <= 0) return false;
    const std::size_t stride = static_cast<std::size_t>(outputWidth) * 4u;
    const std::size_t bytes = stride * static_cast<std::size_t>(bandRows);
    if (stride / 4u != static_cast<std::size_t>(outputWidth) ||
        bytes / stride != static_cast<std::size_t>(bandRows)) {
        return false;
    }
    pixels.assign(bytes, 0);

    const int minMain = image.MinMain();
    auto forceOpaque = [&]() {
        for (std::size_t i = 3; i < pixels.size(); i += 4u) pixels[i] = 255;
    };
    if (image.Dir() == Direction::Vertical) {
        for (const Tile& tile : image.Tiles()) {
            const int tileTop = tile.offset - minMain;
            const int tileBottom = tileTop + tile.mainLen;
            const int copyTop = (std::max)(bandTop, tileTop);
            const int copyBottom = (std::min)(bandTop + bandRows, tileBottom);
            if (copyBottom <= copyTop) continue;
            const int rows = copyBottom - copyTop;
            const int sourceRow = copyTop - tileTop;
            BYTE* dst = pixels.data() +
                static_cast<std::size_t>(copyTop - bandTop) * stride;
            const int capacityMain = tile.capacityMain > 0 ? tile.capacityMain : tile.mainLen;
            if (tile.dataStart < 0 || tile.mainLen > capacityMain - tile.dataStart ||
                !ReadBitmapRect(tile.bitmap, tile.crossLen, capacityMain, 0,
                    tile.dataStart + sourceRow, tile.crossLen, rows, dst, stride)) {
                return false;
            }
        }
        forceOpaque();
        return true;
    }

    // Horizontal LongShot tiles are vertical strips. Compose a bounded batch
    // of output rows from every strip rather than materializing the whole image.
    for (const Tile& tile : image.Tiles()) {
        const int x = tile.offset - minMain;
        if (x < 0 || tile.mainLen <= 0 || x > outputWidth || tile.mainLen > outputWidth - x ||
            tile.crossLen < bandTop + bandRows) {
            return false;
        }
        const int capacityMain = tile.capacityMain > 0 ? tile.capacityMain : tile.mainLen;
        if (tile.dataStart < 0 || tile.mainLen > capacityMain - tile.dataStart) return false;
        BYTE* dst = pixels.data() + static_cast<std::size_t>(x) * 4u;
        if (!ReadBitmapRect(tile.bitmap, capacityMain, tile.crossLen, tile.dataStart,
                bandTop, tile.mainLen, bandRows, dst, stride)) return false;
    }
    forceOpaque();
    return true;
}

bool CreateEncoderForFormat(
    ScreenshotFormat format,
    GUID& container,
    WICPixelFormatGUID& pixelFormat,
    bool& needsBgr24) {
    needsBgr24 = false;
    switch (format) {
    case ScreenshotFormat::Png:
        container = GUID_ContainerFormatPng;
        pixelFormat = GUID_WICPixelFormat32bppBGRA;
        return true;
    case ScreenshotFormat::Jpeg:
        container = GUID_ContainerFormatJpeg;
        pixelFormat = GUID_WICPixelFormat24bppBGR;
        needsBgr24 = true;
        return true;
    case ScreenshotFormat::Bmp:
        container = GUID_ContainerFormatBmp;
        pixelFormat = GUID_WICPixelFormat32bppBGRA;
        return true;
    default:
        return false;
    }
}

bool SaveWicStreamed(
    const LongShotImage& image,
    const std::wstring& tempPath,
    ScreenshotFormat format,
    int jpegQuality,
    const std::atomic<bool>* cancel,
    const std::function<void(int)>& progress,
    std::wstring* error) {
    UINT width = 0;
    UINT height = 0;
    if (!GetOutputSize(image, width, height)) {
        SetError(error, L"Long screenshot is empty.");
        return false;
    }

    GUID container = {};
    WICPixelFormatGUID pixelFormat = {};
    bool needsBgr24 = false;
    if (!CreateEncoderForFormat(format, container, pixelFormat, needsBgr24)) {
        SetError(error, L"This long screenshot format is not supported by the tiled encoder.");
        return false;
    }

    ScopedComInitialization com;
    if (!com.IsUsable()) {
        SetError(error, L"Failed to initialize the long screenshot encoder.");
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (SUCCEEDED(hr)) hr = factory->CreateStream(stream.GetAddressOf());
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(tempPath.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(container, nullptr, encoder.GetAddressOf());
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
    if (SUCCEEDED(hr) && format == ScreenshotFormat::Jpeg && properties) {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = static_cast<float>((std::max)(0, (std::min)(100, jpegQuality))) / 100.0f;
        hr = properties->Write(1, &option, &value);
        VariantClear(&value);
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(properties.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pixelFormat);
    const WICPixelFormatGUID expectedFormat = needsBgr24
        ? GUID_WICPixelFormat24bppBGR
        : GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr) && !IsEqualGUID(pixelFormat, expectedFormat)) hr = E_FAIL;

    std::vector<BYTE> band;
    std::vector<BYTE> encodedBand;
    if (SUCCEEDED(hr)) {
        const UINT encodeBandRows = EncodeBandRowsFor(width, needsBgr24);
        const UINT bands = height / encodeBandRows + (height % encodeBandRows != 0 ? 1u : 0u);
        for (UINT bandIndex = 0; bandIndex < bands && SUCCEEDED(hr); ++bandIndex) {
            if (IsCancelled(cancel)) {
                hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                break;
            }
            const UINT top = bandIndex * encodeBandRows;
            const UINT rows = (std::min)(encodeBandRows, height - top);
            if (!ComposeBand(image, static_cast<int>(top), static_cast<int>(rows),
                    static_cast<int>(width), band)) {
                hr = E_FAIL;
                break;
            }

            const UINT sourceStride = width * 4u;
            if (needsBgr24) {
                const UINT encodedStride = width * 3u;
                encodedBand.resize(static_cast<std::size_t>(encodedStride) * rows);
                for (UINT y = 0; y < rows; ++y) {
                    const BYTE* src = band.data() + static_cast<std::size_t>(y) * sourceStride;
                    BYTE* dst = encodedBand.data() + static_cast<std::size_t>(y) * encodedStride;
                    for (UINT x = 0; x < width; ++x) {
                        dst[x * 3u + 0u] = src[x * 4u + 0u];
                        dst[x * 3u + 1u] = src[x * 4u + 1u];
                        dst[x * 3u + 2u] = src[x * 4u + 2u];
                    }
                }
                hr = frame->WritePixels(rows, encodedStride,
                    static_cast<UINT>(encodedBand.size()), encodedBand.data());
            } else {
                hr = frame->WritePixels(rows, sourceStride,
                    static_cast<UINT>(band.size()), band.data());
            }
            if (progress) {
                progress(10 + static_cast<int>((85ull * (bandIndex + 1u)) / bands));
            }
        }
    }
    if (SUCCEEDED(hr) && !IsCancelled(cancel)) hr = frame->Commit();
    if (SUCCEEDED(hr) && !IsCancelled(cancel)) hr = encoder->Commit();

    if (FAILED(hr) || IsCancelled(cancel)) {
        DeleteFileW(tempPath.c_str());
        SetError(error, IsCancelled(cancel)
            ? L"Save cancelled."
            : L"Failed to encode the long screenshot.");
        return false;
    }
    return true;
}

bool SaveFallbackAtomically(
    const LongShotImage& image,
    const std::wstring& tempPath,
    ScreenshotFormat format,
    int jpegQuality,
    const std::atomic<bool>* cancel,
    std::wstring* error) {
    if (IsCancelled(cancel)) {
        SetError(error, L"Save cancelled.");
        return false;
    }
    HBITMAP full = image.Materialize();
    if (!full) {
        SetError(error, L"Failed to build the long screenshot for this encoder.");
        return false;
    }
    std::wstring encodeError;
    const bool ok = Screenshot::SaveBitmapToFile(
        full, tempPath, format, jpegQuality, &encodeError, false);
    DeleteObject(full);
    if (!ok || IsCancelled(cancel)) {
        DeleteFileW(tempPath.c_str());
        if (IsCancelled(cancel)) SetError(error, L"Save cancelled.");
        else SetError(error, std::move(encodeError));
        return false;
    }
    return true;
}

} // namespace

bool SaveLongShotImageToFile(
    const LongShotImage& image,
    const std::wstring& destinationPath,
    ScreenshotFormat format,
    int jpegQuality,
    const std::atomic<bool>* cancel,
    const std::function<void(int)>& progress,
    std::wstring* error) {
    if (error) error->clear();
    std::wstring tempPath;
    try {
        if (destinationPath.empty()) {
            SetError(error, L"No save path was selected.");
            return false;
        }
        if (IsCancelled(cancel)) {
            SetError(error, L"Save cancelled.");
            return false;
        }

        // Synchronous callers may have just committed tile BitBlts on this thread.
        // LongShotSession also flushes on its UI thread before handing tiles to its
        // asynchronous encoder, avoiding a flush per tile/band in the hot path.
        GdiFlush();

        tempPath = TempSiblingPath(destinationPath);
        DeleteFileW(tempPath.c_str());
        const bool streamed = format == ScreenshotFormat::Png ||
            format == ScreenshotFormat::Jpeg || format == ScreenshotFormat::Bmp;
        const bool encoded = streamed
            ? SaveWicStreamed(image, tempPath, format, jpegQuality, cancel, progress, error)
            : SaveFallbackAtomically(image, tempPath, format, jpegQuality, cancel, error);
        if (!encoded) return false;
        if (IsCancelled(cancel)) {
            DeleteFileW(tempPath.c_str());
            SetError(error, L"Save cancelled.");
            return false;
        }
        if (!MoveFileExW(
                tempPath.c_str(), destinationPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tempPath.c_str());
            SetError(error, L"Failed to replace the destination image.");
            return false;
        }
        if (progress) progress(100);
        return true;
    } catch (...) {
        if (!tempPath.empty()) DeleteFileW(tempPath.c_str());
        SetError(error, L"Long screenshot export failed unexpectedly.");
        return false;
    }
}

} // namespace longshot
