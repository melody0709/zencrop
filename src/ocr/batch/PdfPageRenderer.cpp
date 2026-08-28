#include "PdfPageRenderer.h"

#include "image/BitmapCodec.h"
#include "PageRange.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include "core/WideStringUtils.h"

using namespace winrt;
using namespace Windows::Data::Pdf;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

namespace {

struct TargetRenderSize {
    uint32_t width = 0;
    uint32_t height = 0;
    bool scaledDown = false;
    bool skippedTooLarge = false;
    std::wstring error;
};

// RAII guard: keep winrt::init_apartment() paired with uninit_apartment().
// Construct it inside the caller's try block so init failures are reported normally.
struct ApartmentGuard {
    explicit ApartmentGuard(winrt::apartment_type type) {
        winrt::init_apartment(type);
        initialized = true;
    }
    ~ApartmentGuard() {
        if (initialized) {
            winrt::uninit_apartment();
        }
    }
    ApartmentGuard(const ApartmentGuard&) = delete;
    ApartmentGuard& operator=(const ApartmentGuard&) = delete;

private:
    bool initialized = false;
};

std::wstring LastErrorMessage(const wchar_t* prefix) {
    DWORD err = GetLastError();
    wchar_t* buffer = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring result = prefix ? prefix : L"Operation failed";
    if (err != 0) {
        // OWN-126: pure paren int (WideStringUtils).
        result += WideFormatParenInt(static_cast<int>(err));
    }
    if (buffer) {
        result += L": ";
        result += buffer;
        LocalFree(buffer);
    }
    return result;
}

bool EnsureDirectory(const std::wstring& dir, std::wstring& error) {
    if (dir.empty()) {
        error = L"Output page image directory is empty.";
        return false;
    }
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS) return true;
    error = L"Failed to create output page image directory: " + dir;
    return false;
}

// OWN-113: thin-wrap pure path join.
std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    return WideJoinPath(left, right);
}

// OWN-113: pure page index name + product format extension.
std::wstring PageImageFileName(int pageIndex, PdfRenderImageFormat format) {
    return WideFormatPageIndexName(pageIndex) + PdfRenderImageFormatExtension(format);
}

void DeletePageImageVariants(const std::wstring& outputPageImagesDir, int pageIndex) {
    DeleteFileW(JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, PdfRenderImageFormat::Png)).c_str());
    DeleteFileW(JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, PdfRenderImageFormat::Jpeg)).c_str());
    DeleteFileW(JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, PdfRenderImageFormat::WebP)).c_str());
}

uint64_t FileSizeBytes(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER size = {};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

std::wstring TempSiblingPath(const std::wstring& finalPath, const wchar_t* suffix) {
    static std::atomic<unsigned> counter{0};
    // OWN-126: pure pid.tick.counter.suffix (WideStringUtils).
    return finalPath + WideFormatPidTickCounterSuffix(
        GetCurrentProcessId(),
        GetTickCount64(),
        ++counter,
        suffix);
}

std::wstring HResultMessage(const winrt::hresult_error& ex) {
    std::wstringstream ss;
    ss << L"WinRT error 0x" << std::hex << static_cast<uint32_t>(ex.code());
    std::wstring message = ex.message().c_str();
    if (!message.empty()) {
        ss << L": " << message;
    }
    return ss.str();
}

std::wstring ToLower(std::wstring text) {
    text = WideToLower(std::move(text)); // OWN-79
    return text;
}

bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

bool IsLikelyPasswordError(const winrt::hresult_error& ex, const std::wstring& message) {
    HRESULT hr = static_cast<HRESULT>(ex.code());
    if (hr == HRESULT_FROM_WIN32(ERROR_WRONG_PASSWORD)) return true;
    if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD)) return true;
    // Windows.Data.Pdf may report encrypted PDFs with this undocumented HRESULT
    // and no password text in the localized message.
    if (hr == static_cast<HRESULT>(0x80048040)) return true;
    return ContainsInsensitive(message, L"password") || ContainsInsensitive(message, L"密码");
}

bool ContainsNonAscii(const std::wstring& value) {
    return std::any_of(value.begin(), value.end(), [](wchar_t ch) {
        return ch > 0x7f;
    });
}

struct ScopedPdfInputPath {
    std::wstring path;
    std::wstring error;
    bool temporary = false;

    ~ScopedPdfInputPath() {
        if (temporary && !path.empty()) {
            DeleteFileW(path.c_str());
        }
    }

    bool Prepare(const std::wstring& sourcePath) {
        path = sourcePath;
        error.clear();
        temporary = false;

        if (sourcePath.empty() || !ContainsNonAscii(sourcePath)) {
            return true;
        }

        wchar_t tempPathBuffer[MAX_PATH] = {};
        DWORD tempLen = GetTempPathW(MAX_PATH, tempPathBuffer);
        if (tempLen == 0 || tempLen >= MAX_PATH) {
            error = LastErrorMessage(L"Failed to locate temp directory for PDF render");
            return false;
        }

        std::wstring tempDir = JoinPath(tempPathBuffer, L"ZenCropPdfRender");
        int rc = SHCreateDirectoryExW(nullptr, tempDir.c_str(), nullptr);
        if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS) {
            error = L"Failed to create temp PDF render directory.";
            return false;
        }

        static std::atomic<unsigned> counter{0};
        // OWN-114: pure pdf temp name (WideStringUtils).
        // Preserve pre-increment then mod behavior of historical ++counter % 1000.
        const unsigned seq = ++counter % 1000;
        const std::wstring fileName = WideFormatPdfTempName(
            GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64()),
            seq);
        std::wstring tempPdfPath = JoinPath(tempDir, fileName);

        if (!CopyFileW(sourcePath.c_str(), tempPdfPath.c_str(), FALSE)) {
            error = LastErrorMessage(L"Failed to copy PDF to temp render path");
            return false;
        }

        path = std::move(tempPdfPath);
        temporary = true;
        return true;
    }
};

TargetRenderSize ComputeTargetSize(const Size& pageSize, const PdfRenderSettings& settings) {
    TargetRenderSize target;

    int dpi = settings.dpi > 0 ? settings.dpi : kDefaultPdfRenderDpi;
    double rawWidth = std::ceil(static_cast<double>(pageSize.Width) * dpi / 96.0);
    double rawHeight = std::ceil(static_cast<double>(pageSize.Height) * dpi / 96.0);

    if (!std::isfinite(rawWidth) || !std::isfinite(rawHeight) || rawWidth <= 0.0 || rawHeight <= 0.0) {
        target.skippedTooLarge = true;
        target.error = L"PDF page has an invalid size.";
        return target;
    }

    double scale = 1.0;
    if (settings.maxPixelEdge > 0) {
        scale = (std::min)(scale, static_cast<double>(settings.maxPixelEdge) / rawWidth);
        scale = (std::min)(scale, static_cast<double>(settings.maxPixelEdge) / rawHeight);
    }

    if (settings.maxMegapixels > 0) {
        double maxPixels = static_cast<double>(settings.maxMegapixels) * 1000000.0;
        double rawPixels = rawWidth * rawHeight;
        if (rawPixels > maxPixels && rawPixels > 0.0) {
            scale = (std::min)(scale, std::sqrt(maxPixels / rawPixels));
        }
    }

    scale = (std::max)(scale, 0.0001);
    target.scaledDown = scale < 0.999;

    double scaledWidth = std::floor(rawWidth * scale);
    double scaledHeight = std::floor(rawHeight * scale);
    scaledWidth = (std::max)(1.0, scaledWidth);
    scaledHeight = (std::max)(1.0, scaledHeight);

    if (scaledWidth > static_cast<double>((std::numeric_limits<uint32_t>::max)()) ||
        scaledHeight > static_cast<double>((std::numeric_limits<uint32_t>::max)())) {
        target.skippedTooLarge = true;
        target.error = L"PDF page render target is too large.";
        return target;
    }

    target.width = static_cast<uint32_t>(scaledWidth);
    target.height = static_cast<uint32_t>(scaledHeight);
    return target;
}

bool WriteBytesAtomic(const std::wstring& path, const std::vector<uint8_t>& bytes, std::wstring& error) {
    std::wstring tmpPath = path + L".tmp";
    HANDLE file = CreateFileW(
        tmpPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = LastErrorMessage(L"Failed to create temp page image");
        return false;
    }

    bool ok = true;
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD chunk = static_cast<DWORD>((std::min)(bytes.size() - offset, static_cast<size_t>(1u << 20)));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            error = LastErrorMessage(L"Failed to write temp page image");
            break;
        }
        offset += written;
    }

    if (ok && !FlushFileBuffers(file)) {
        ok = false;
        error = LastErrorMessage(L"Failed to flush temp page image");
    }

    CloseHandle(file);

    if (!ok) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = LastErrorMessage(L"Failed to replace page image");
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

bool ReadStreamBytes(IRandomAccessStream const& stream, std::vector<uint8_t>& bytes, std::wstring& error) {
    uint64_t size = stream.Size();
    if (size > (std::numeric_limits<uint32_t>::max)()) {
        error = L"Rendered page image is too large to read.";
        return false;
    }

    stream.Seek(0);
    DataReader reader(stream.GetInputStreamAt(0));
    reader.LoadAsync(static_cast<uint32_t>(size)).get();

    bytes.assign(static_cast<size_t>(size), 0);
    if (!bytes.empty()) {
        reader.ReadBytes(winrt::array_view<uint8_t>(bytes));
    }
    return true;
}

bool SaveEncodedHBitmapAtomic(
    HBITMAP bitmap,
    const std::wstring& finalPath,
    PdfRenderImageFormat format,
    int quality,
    std::wstring& error)
{
    if (!bitmap) {
        error = L"Rendered page bitmap is empty.";
        return false;
    }

    std::wstring tmpPath = TempSiblingPath(finalPath, PdfRenderImageFormatExtension(format));
    ImageCodec::ImageFileFormat codecFormat = ImageCodec::ImageFileFormat::Png;
    switch (format) {
    case PdfRenderImageFormat::Jpeg:
        codecFormat = ImageCodec::ImageFileFormat::Jpeg;
        break;
    case PdfRenderImageFormat::WebP:
        codecFormat = ImageCodec::ImageFileFormat::WebP;
        break;
    case PdfRenderImageFormat::Png:
    case PdfRenderImageFormat::Auto:
    default:
        codecFormat = ImageCodec::ImageFileFormat::Png;
        break;
    }

    ImageCodec::EncodeOptions options;
    options.quality = ClampPdfRenderImageQuality(quality);
    if (!ImageCodec::SaveHBitmapToFile(bitmap, tmpPath, codecFormat, options, &error)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    if (!MoveFileExW(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
        error = LastErrorMessage(L"Failed to replace encoded page image");
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

bool SavePdfRenderedPageImage(
    const std::wstring& outputPageImagesDir,
    int pageIndex,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& pngBytes,
    const PdfRenderSettings& settings,
    std::wstring& imagePath,
    PdfRenderImageFormat& imageFormat,
    uint64_t& imageByteSize,
    std::wstring& error)
{
    imagePath.clear();
    imageFormat = PdfRenderImageFormat::Png;
    imageByteSize = 0;

    PdfRenderImageFormat requested = settings.imageFormat;
    int quality = ClampPdfRenderImageQuality(settings.imageQuality);
    double megapixels = (static_cast<double>(width) * static_cast<double>(height)) / 1000000.0;
    const bool keepPngForAuto = requested == PdfRenderImageFormat::Auto &&
        pngBytes.size() <= 4ull * 1024ull * 1024ull &&
        megapixels <= 8.0;

    DeletePageImageVariants(outputPageImagesDir, pageIndex);

    if (requested == PdfRenderImageFormat::Png || keepPngForAuto) {
        imageFormat = PdfRenderImageFormat::Png;
        imagePath = JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, imageFormat));
        if (!WriteBytesAtomic(imagePath, pngBytes, error)) {
            return false;
        }
        imageByteSize = FileSizeBytes(imagePath);
        return true;
    }

    std::wstring tempPng = TempSiblingPath(JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, PdfRenderImageFormat::Png)), L".png");
    if (!WriteBytesAtomic(tempPng, pngBytes, error)) {
        return false;
    }

    std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)> bitmap(
        ImageCodec::LoadHBitmapFromFile(tempPng, &error),
        DeleteObject);
    DeleteFileW(tempPng.c_str());
    if (!bitmap) {
        return false;
    }

    auto tryEncode = [&](PdfRenderImageFormat format, std::wstring& outPath, uint64_t& outSize, std::wstring& outError) {
        outPath = JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, format));
        outError.clear();
        if (!SaveEncodedHBitmapAtomic(bitmap.get(), outPath, format, quality, outError)) {
            DeleteFileW(outPath.c_str());
            return false;
        }
        outSize = FileSizeBytes(outPath);
        if (outSize == 0) {
            outError = L"Encoded page image is empty.";
            DeleteFileW(outPath.c_str());
            return false;
        }
        std::unique_ptr<Gdiplus::Bitmap> verify(ImageCodec::LoadBitmapFromFile(outPath));
        if (!verify) {
            outError = L"Encoded page image could not be decoded for verification.";
            DeleteFileW(outPath.c_str());
            return false;
        }
        return true;
    };

    if (requested == PdfRenderImageFormat::Jpeg || requested == PdfRenderImageFormat::WebP) {
        std::wstring encodedPath;
        uint64_t encodedSize = 0;
        std::wstring encodeError;
        if (tryEncode(requested, encodedPath, encodedSize, encodeError)) {
            imageFormat = requested;
            imagePath = encodedPath;
            imageByteSize = encodedSize;
            return true;
        }
        if (!encodeError.empty()) {
            OutputDebugStringW((L"[PDF Renderer] Falling back to PNG after " +
                std::wstring(PdfRenderImageFormatToString(requested)) +
                L" encode failed: " + encodeError + L"\n").c_str());
        }

        imageFormat = PdfRenderImageFormat::Png;
        imagePath = JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, imageFormat));
        if (!WriteBytesAtomic(imagePath, pngBytes, error)) {
            return false;
        }
        imageByteSize = FileSizeBytes(imagePath);
        return true;
    }

    std::wstring webpPath;
    std::wstring jpegPath;
    uint64_t webpSize = 0;
    uint64_t jpegSize = 0;
    std::wstring webpError;
    std::wstring jpegError;
    bool webpOk = tryEncode(PdfRenderImageFormat::WebP, webpPath, webpSize, webpError);
    bool jpegOk = tryEncode(PdfRenderImageFormat::Jpeg, jpegPath, jpegSize, jpegError);
    if (!webpOk && !webpError.empty()) {
        OutputDebugStringW((L"[PDF Renderer] Auto WebP candidate failed: " + webpError + L"\n").c_str());
    }
    if (!jpegOk && !jpegError.empty()) {
        OutputDebugStringW((L"[PDF Renderer] Auto JPEG candidate failed: " + jpegError + L"\n").c_str());
    }

    uint64_t pngSize = static_cast<uint64_t>(pngBytes.size());
    bool usePng = true;
    PdfRenderImageFormat bestEncodedFormat = PdfRenderImageFormat::Png;
    std::wstring bestEncodedPath;
    uint64_t bestEncodedSize = pngSize;
    if (webpOk && webpSize < bestEncodedSize) {
        usePng = false;
        bestEncodedFormat = PdfRenderImageFormat::WebP;
        bestEncodedPath = webpPath;
        bestEncodedSize = webpSize;
    }
    if (jpegOk && jpegSize < bestEncodedSize) {
        usePng = false;
        bestEncodedFormat = PdfRenderImageFormat::Jpeg;
        bestEncodedPath = jpegPath;
        bestEncodedSize = jpegSize;
    }

    if (!usePng) {
        if (webpOk && bestEncodedFormat != PdfRenderImageFormat::WebP) DeleteFileW(webpPath.c_str());
        if (jpegOk && bestEncodedFormat != PdfRenderImageFormat::Jpeg) DeleteFileW(jpegPath.c_str());
        imageFormat = bestEncodedFormat;
        imagePath = bestEncodedPath;
        imageByteSize = bestEncodedSize;
        return true;
    }

    if (webpOk) DeleteFileW(webpPath.c_str());
    if (jpegOk) DeleteFileW(jpegPath.c_str());
    imageFormat = PdfRenderImageFormat::Png;
    imagePath = JoinPath(outputPageImagesDir, PageImageFileName(pageIndex, imageFormat));
    if (!WriteBytesAtomic(imagePath, pngBytes, error)) {
        return false;
    }
    imageByteSize = FileSizeBytes(imagePath);
    return true;
}

} // namespace

PdfPreflightResult PdfPageRenderer::Inspect(
    const std::wstring& pdfPath,
    const std::wstring& password)
{
    PdfPreflightResult result;

    try {
        ScopedPdfInputPath input;
        if (!input.Prepare(pdfPath)) {
            result.error = input.error.empty() ? L"Failed to prepare PDF input path." : input.error;
            return result;
        }

        ApartmentGuard apartment(winrt::apartment_type::single_threaded);
        StorageFile file = StorageFile::GetFileFromPathAsync(input.path).get();
        PdfDocument document = password.empty()
            ? PdfDocument::LoadFromFileAsync(file).get()
            : PdfDocument::LoadFromFileAsync(file, winrt::hstring(password)).get();

        result.requiresPassword = document.IsPasswordProtected();
        uint32_t pageCount = document.PageCount();
        result.pageCount = static_cast<int>((std::min<uint32_t>)(
            pageCount,
            static_cast<uint32_t>((std::numeric_limits<int>::max)())));
        result.pages.reserve(result.pageCount > 0 ? static_cast<size_t>(result.pageCount) : 0);
        for (int i = 0; i < result.pageCount; i++) {
            PdfPage page = document.GetPage(static_cast<uint32_t>(i));
            Size size = page.Size();
            PdfPreflightPageInfo pageInfo;
            pageInfo.pageIndex = i + 1;
            pageInfo.widthDip = size.Width;
            pageInfo.heightDip = size.Height;
            result.pages.push_back(pageInfo);
            page.Close();
        }
        result.success = result.pageCount > 0;
        if (!result.success) {
            result.error = L"PDF has no pages.";
        }
        return result;
    } catch (const winrt::hresult_error& ex) {
        result.error = HResultMessage(ex);
        result.requiresPassword = IsLikelyPasswordError(ex, result.error);
        return result;
    } catch (const std::exception& ex) {
        int len = MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, nullptr, 0);
        if (len > 1) {
            result.error.assign(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, result.error.data(), len);
            if (!result.error.empty() && result.error.back() == L'\0') result.error.pop_back();
        } else {
            result.error = L"Failed to inspect PDF.";
        }
        return result;
    } catch (...) {
        result.error = L"Failed to inspect PDF.";
        return result;
    }
}

PdfRenderResult PdfPageRenderer::RenderToPageImages(
    const std::wstring& pdfPath,
    const std::wstring& outputPageImagesDir,
    const PdfRenderSettings& settings)
{
    PdfRenderResult result;

    std::wstring error;
    if (!EnsureDirectory(outputPageImagesDir, error)) {
        result.error = error;
        return result;
    }

    try {
        ScopedPdfInputPath input;
        if (!input.Prepare(pdfPath)) {
            result.error = input.error.empty() ? L"Failed to prepare PDF input path." : input.error;
            return result;
        }

        ApartmentGuard apartment(winrt::apartment_type::multi_threaded);
        StorageFile file = StorageFile::GetFileFromPathAsync(input.path).get();
        PdfDocument document = settings.password.empty()
            ? PdfDocument::LoadFromFileAsync(file).get()
            : PdfDocument::LoadFromFileAsync(file, winrt::hstring(settings.password)).get();

        result.requiresPassword = document.IsPasswordProtected();
        uint32_t pageCount = document.PageCount();
        result.pageCount = static_cast<int>((std::min<uint32_t>)(pageCount, static_cast<uint32_t>((std::numeric_limits<int>::max)())));
        std::vector<int> selectedPages;
        if (!PageRange::Parse(settings.pageRange, result.pageCount, selectedPages, error)) {
            result.error = error;
            return result;
        }
        result.pages.reserve(selectedPages.size());

        bool anyPageError = false;
        for (int selectedPage : selectedPages) {
            PdfRenderedPage rendered;
            rendered.pageIndex = selectedPage;
            PdfRenderImageFormat plannedFormat = settings.imageFormat == PdfRenderImageFormat::Auto
                ? PdfRenderImageFormat::Png
                : settings.imageFormat;
            rendered.imagePath = JoinPath(outputPageImagesDir, PageImageFileName(rendered.pageIndex, plannedFormat));
            rendered.imageFormat = plannedFormat;

            try {
                PdfPage page = document.GetPage(static_cast<uint32_t>(selectedPage - 1));
                TargetRenderSize target = ComputeTargetSize(page.Size(), settings);
                rendered.width = target.width;
                rendered.height = target.height;
                rendered.scaledDown = target.scaledDown;
                rendered.skippedTooLarge = target.skippedTooLarge;

                if (target.skippedTooLarge) {
                    rendered.error = target.error;
                    anyPageError = true;
                    result.pages.push_back(std::move(rendered));
                    page.Close();
                    continue;
                }

                PdfPageRenderOptions options;
                options.DestinationWidth(target.width);
                options.DestinationHeight(target.height);

                InMemoryRandomAccessStream stream;
                page.RenderToStreamAsync(stream, options).get();
                stream.Seek(0);

                try {
                    BitmapDecoder decoder = BitmapDecoder::CreateAsync(stream).get();
                    rendered.width = decoder.PixelWidth();
                    rendered.height = decoder.PixelHeight();
                } catch (...) {
                    rendered.width = target.width;
                    rendered.height = target.height;
                }

                if (settings.savePageImages) {
                    std::vector<uint8_t> bytes;
                    if (!ReadStreamBytes(stream, bytes, error) ||
                        !SavePdfRenderedPageImage(
                            outputPageImagesDir,
                            rendered.pageIndex,
                            rendered.width,
                            rendered.height,
                            bytes,
                            settings,
                            rendered.imagePath,
                            rendered.imageFormat,
                            rendered.imageByteSize,
                            error)) {
                        rendered.error = error;
                        anyPageError = true;
                    }
                } else {
                    rendered.imagePath.clear();
                }

                page.Close();
            } catch (const winrt::hresult_error& ex) {
                rendered.error = HResultMessage(ex);
                anyPageError = true;
            } catch (const std::exception& ex) {
                int len = MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, nullptr, 0);
                if (len > 1) {
                    std::wstring message(len, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, message.data(), len);
                    if (!message.empty() && message.back() == L'\0') message.pop_back();
                    rendered.error = message;
                } else {
                    rendered.error = L"PDF page render failed.";
                }
                anyPageError = true;
            } catch (...) {
                rendered.error = L"PDF page render failed.";
                anyPageError = true;
            }

            result.pages.push_back(std::move(rendered));
        }

        result.success = !anyPageError;
        if (anyPageError) {
            result.error = L"One or more PDF pages failed to render.";
        }
        return result;
    } catch (const winrt::hresult_error& ex) {
        result.error = HResultMessage(ex);
        result.requiresPassword = IsLikelyPasswordError(ex, result.error);
        return result;
    } catch (const std::exception& ex) {
        int len = MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, nullptr, 0);
        if (len > 1) {
            result.error.assign(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, result.error.data(), len);
            if (!result.error.empty() && result.error.back() == L'\0') result.error.pop_back();
        } else {
            result.error = L"Failed to render PDF.";
        }
        return result;
    } catch (...) {
        result.error = L"Failed to render PDF.";
        return result;
    }
}

PdfCoverRenderResult PdfPageRenderer::RenderFirstPageCover(
    const std::wstring& pdfPath,
    const std::wstring& candidatePath,
    const std::wstring& password,
    uint32_t targetWidth,
    uint32_t maxPixelEdge,
    PdfRenderImageFormat format,
    int quality)
{
    PdfCoverRenderResult result;
    result.candidatePath = candidatePath;
    if (pdfPath.empty() || candidatePath.empty()) {
        result.error = L"PDF cover input or candidate path is empty.";
        return result;
    }
    if (targetWidth == 0) targetWidth = 512;
    if (maxPixelEdge == 0) maxPixelEdge = 768;

    try {
        ScopedPdfInputPath input;
        if (!input.Prepare(pdfPath)) {
            result.error = input.error.empty() ? L"Failed to prepare PDF cover input." : input.error;
            return result;
        }

        ApartmentGuard apartment(winrt::apartment_type::multi_threaded);
        StorageFile file = StorageFile::GetFileFromPathAsync(input.path).get();
        PdfDocument document = password.empty()
            ? PdfDocument::LoadFromFileAsync(file).get()
            : PdfDocument::LoadFromFileAsync(file, winrt::hstring(password)).get();
        result.requiresPassword = document.IsPasswordProtected();
        if (document.PageCount() == 0) {
            result.error = L"PDF has no pages.";
            return result;
        }

        PdfPage page = document.GetPage(0);
        Size pageSize = page.Size();
        double width = static_cast<double>(pageSize.Width);
        double height = static_cast<double>(pageSize.Height);
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
            page.Close();
            result.error = L"PDF first page has an invalid size.";
            return result;
        }
        double scale = (std::min)(1.0, static_cast<double>(targetWidth) / width);
        scale = (std::min)(scale, static_cast<double>(maxPixelEdge) / (std::max)(width, height));
        result.width = static_cast<uint32_t>((std::max)(1.0, std::floor(width * scale)));
        result.height = static_cast<uint32_t>((std::max)(1.0, std::floor(height * scale)));

        InMemoryRandomAccessStream stream;
        uint32_t requestedWidth = result.width;
        uint32_t requestedHeight = result.height;
        for (int attempt = 0; attempt < 3; ++attempt) {
            PdfPageRenderOptions options;
            options.DestinationWidth(requestedWidth);
            options.DestinationHeight(requestedHeight);
            InMemoryRandomAccessStream attemptStream;
            page.RenderToStreamAsync(attemptStream, options).get();
            attemptStream.Seek(0);
            BitmapDecoder decoder = BitmapDecoder::CreateAsync(attemptStream).get();
            result.width = decoder.PixelWidth();
            result.height = decoder.PixelHeight();
            stream = attemptStream;
            uint32_t actualMax = (std::max)(result.width, result.height);
            if (actualMax <= maxPixelEdge || actualMax == 0) break;
            double correction = static_cast<double>(maxPixelEdge) / actualMax;
            requestedWidth = (std::max)(1u, static_cast<uint32_t>(std::floor(requestedWidth * correction)));
            requestedHeight = (std::max)(1u, static_cast<uint32_t>(std::floor(requestedHeight * correction)));
        }
        if (result.width == 0 || result.height == 0 ||
            result.width > maxPixelEdge || result.height > maxPixelEdge) {
            page.Close();
            // OWN-120: pure size label (WideStringUtils).
            result.error = L"Rendered PDF cover has invalid dimensions: " +
                WideFormatSizeWxH(static_cast<int>(result.width), static_cast<int>(result.height)) + L".";
            return result;
        }

        std::vector<uint8_t> bytes;
        std::wstring error;
        if (!ReadStreamBytes(stream, bytes, error) || bytes.empty()) {
            page.Close();
            result.error = error.empty() ? L"Failed to read PDF cover candidate." : error;
            return result;
        }

        const PdfRenderImageFormat requested =
            format == PdfRenderImageFormat::Auto ? PdfRenderImageFormat::WebP : format;
        bool encoded = false;
        if (requested == PdfRenderImageFormat::Png) {
            encoded = WriteBytesAtomic(candidatePath, bytes, error);
        } else {
            const std::wstring tempPng = TempSiblingPath(candidatePath, L".png");
            if (WriteBytesAtomic(tempPng, bytes, error)) {
                std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)> bitmap(
                    ImageCodec::LoadHBitmapFromFile(tempPng, &error),
                    DeleteObject);
                DeleteFileW(tempPng.c_str());
                if (bitmap) {
                    encoded = SaveEncodedHBitmapAtomic(
                        bitmap.get(),
                        candidatePath,
                        requested,
                        ClampPdfRenderImageQuality(quality > 0 ? quality : 80),
                        error);
                }
            }
        }

        if (!encoded) {
            // The cover is derived UI state. A failed lossy encoder must not
            // make PDF import fail; emit a genuine PNG with a matching suffix.
            std::wstring fallbackPath = candidatePath;
            const size_t extension = fallbackPath.find_last_of(L'.');
            if (extension == std::wstring::npos) {
                page.Close();
                result.error = error.empty() ? L"Failed to create PNG cover fallback path." : error;
                return result;
            }
            fallbackPath.resize(extension);
            fallbackPath += L".png";
            if (!WriteBytesAtomic(fallbackPath, bytes, error)) {
                page.Close();
                result.error = error.empty() ? L"Failed to save PDF cover fallback." : error;
                return result;
            }
            result.candidatePath = std::move(fallbackPath);
        }

        std::unique_ptr<Gdiplus::Bitmap> verify(ImageCodec::LoadBitmapFromFile(result.candidatePath, &error));
        if (!verify) {
            DeleteFileW(result.candidatePath.c_str());
            page.Close();
            result.error = error.empty() ? L"Encoded PDF cover could not be decoded for verification." : error;
            return result;
        }
        page.Close();
        result.success = true;
        return result;
    } catch (const winrt::hresult_error& ex) {
        result.error = HResultMessage(ex);
        result.requiresPassword = IsLikelyPasswordError(ex, result.error);
    } catch (const std::exception& ex) {
        int len = MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, nullptr, 0);
        if (len > 1) {
            result.error.assign(static_cast<size_t>(len), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, result.error.data(), len);
            if (!result.error.empty() && result.error.back() == L'\0') result.error.pop_back();
        } else {
            result.error = L"Failed to render PDF cover.";
        }
    } catch (...) {
        result.error = L"Failed to render PDF cover.";
    }
    DeleteFileW(candidatePath.c_str());
    if (!WideEqualsNoCase(result.candidatePath, candidatePath)) {
        DeleteFileW(result.candidatePath.c_str());
    }
    return result;
}
