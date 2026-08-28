#include "BitmapCodec.h"
#include "core/WideStringUtils.h"

#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <webp/decode.h>
#include <webp/encode.h>

#include <atomic>
#include <climits>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>

namespace ImageCodec {
namespace {

struct BitmapSize {
    int width = 0;
    int height = 0;
};

int ClampQuality(int quality) {
    if (quality < 1) return 1;
    if (quality > 100) return 100;
    return quality;
}

int ClampSpeed(int speed) {
    if (speed < 0) return 0;
    if (speed > 10) return 10;
    return speed;
}

void SetError(std::wstring* error, const wchar_t* message) {
    if (error) *error = message;
}

BitmapSize GetHBitmapSize(HBITMAP bitmap) {
    BitmapSize size;
    BITMAP bm = {};
    if (bitmap && GetObjectW(bitmap, sizeof(bm), &bm)) {
        size.width = bm.bmWidth;
        size.height = bm.bmHeight;
    }
    return size;
}

bool ReadHBitmapPixelsTopDown(HBITMAP bitmap, BitmapSize size, std::vector<DWORD>& pixels) {
    if (!bitmap || size.width <= 0 || size.height <= 0) return false;
    size_t pixelCount = (size_t)size.width * (size_t)size.height;
    if (pixelCount > (std::numeric_limits<size_t>::max)() / sizeof(DWORD)) return false;

    pixels.assign(pixelCount, 0);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size.width;
    bmi.bmiHeader.biHeight = -size.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    if (!hdc) return false;
    int lines = GetDIBits(hdc, bitmap, 0, size.height, pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    return lines == size.height;
}

void NormalizeAlpha(std::vector<DWORD>& pixels, bool forceOpaque) {
    bool anyAlpha = false;
    bool anyTransparent = false;
    for (DWORD pixel : pixels) {
        BYTE alpha = (BYTE)((pixel >> 24) & 0xFF);
        if (alpha != 0) anyAlpha = true;
        if (alpha != 0xFF) anyTransparent = true;
    }

    if (forceOpaque || !anyAlpha || !anyTransparent) {
        for (DWORD& pixel : pixels) {
            pixel = 0xFF000000 | (pixel & 0x00FFFFFF);
        }
    }
}

void ConvertPremultipliedToStraightAlpha(
    std::vector<DWORD>& pixels,
    BitmapSize size)
{
    if (size.width <= 0 || size.height <= 0 ||
        pixels.size() < (size_t)size.width * size.height) {
        return;
    }

    const size_t pixelCount = (size_t)size.width * size.height;
    std::vector<size_t> colorQueue;
    colorQueue.reserve(pixelCount);
    std::vector<unsigned char> hasColor(pixelCount, 0);

    for (size_t i = 0; i < pixelCount; ++i) {
        DWORD pixel = pixels[i];
        const unsigned int alpha = (pixel >> 24) & 0xFF;
        if (alpha == 0) continue;

        unsigned int red = (pixel >> 16) & 0xFF;
        unsigned int green = (pixel >> 8) & 0xFF;
        unsigned int blue = pixel & 0xFF;
        if (alpha < 255) {
            red = (std::min)(255u, (red * 255u + alpha / 2u) / alpha);
            green = (std::min)(255u, (green * 255u + alpha / 2u) / alpha);
            blue = (std::min)(255u, (blue * 255u + alpha / 2u) / alpha);
            pixels[i] = (alpha << 24) | (red << 16) | (green << 8) | blue;
        }
        hasColor[i] = 1;
        colorQueue.push_back(i);
    }

    // Lossy AVIF encodes RGB and alpha separately.  Give fully transparent
    // pixels the nearest visible RGB so transparent black cannot bleed into
    // the soft shadow edge during YUV compression.
    for (size_t head = 0; head < colorQueue.size(); ++head) {
        const size_t index = colorQueue[head];
        const int x = (int)(index % (size_t)size.width);
        const int y = (int)(index / (size_t)size.width);
        const size_t neighbors[4] = {
            x > 0 ? index - 1 : index,
            x + 1 < size.width ? index + 1 : index,
            y > 0 ? index - (size_t)size.width : index,
            y + 1 < size.height ? index + (size_t)size.width : index,
        };
        for (size_t neighbor : neighbors) {
            if (neighbor == index || hasColor[neighbor]) continue;
            pixels[neighbor] =
                (pixels[neighbor] & 0xFF000000) |
                (pixels[index] & 0x00FFFFFF);
            hasColor[neighbor] = 1;
            colorQueue.push_back(neighbor);
        }
    }
}

bool GetEncoderClsid(const wchar_t* mimeType, CLSID& clsid) {
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) return false;

    std::vector<Gdiplus::ImageCodecInfo> encoders(size / sizeof(Gdiplus::ImageCodecInfo));
    if (Gdiplus::GetImageEncoders(count, size, encoders.data()) != Gdiplus::Ok) {
        return false;
    }

    for (UINT i = 0; i < count; i++) {
        if (WideEquals(encoders[i].MimeType, mimeType)) {
            clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

bool SavePixelsWithGdiplus(
    const std::vector<DWORD>& pixelsTopDown,
    BitmapSize size,
    const std::wstring& path,
    ImageFileFormat format,
    int quality,
    std::wstring* error)
{
    const wchar_t* mime = L"image/png";
    if (format == ImageFileFormat::Jpeg) mime = L"image/jpeg";
    else if (format == ImageFileFormat::Bmp) mime = L"image/bmp";

    CLSID clsid = {};
    if (!GetEncoderClsid(mime, clsid)) {
        SetError(error, L"Image encoder is not available.");
        return false;
    }

    Gdiplus::Bitmap bitmap(
        size.width,
        size.height,
        size.width * (INT)sizeof(DWORD),
        PixelFormat32bppARGB,
        reinterpret_cast<BYTE*>(const_cast<DWORD*>(pixelsTopDown.data())));

    Gdiplus::Status status = Gdiplus::GenericError;
    if (format == ImageFileFormat::Jpeg) {
        ULONG gdiplusQuality = (ULONG)ClampQuality(quality);
        Gdiplus::EncoderParameters params = {};
        params.Count = 1;
        params.Parameter[0].Guid = Gdiplus::EncoderQuality;
        params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        params.Parameter[0].NumberOfValues = 1;
        params.Parameter[0].Value = &gdiplusQuality;
        status = bitmap.Save(path.c_str(), &clsid, &params);
    } else {
        status = bitmap.Save(path.c_str(), &clsid, nullptr);
    }

    if (status != Gdiplus::Ok) {
        SetError(error, L"Failed to save image.");
        return false;
    }
    return true;
}

bool WriteBytesToFile(const std::wstring& path, const void* data, size_t size) {
    if (!data || size == 0 || size > (std::numeric_limits<DWORD>::max)()) return false;
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    const BYTE* cursor = static_cast<const BYTE*>(data);
    size_t remaining = size;
    bool ok = true;
    while (remaining > 0) {
        DWORD chunk = (DWORD)(std::min<size_t>)(remaining, 1u << 20);
        DWORD written = 0;
        if (!WriteFile(file, cursor, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            break;
        }
        cursor += written;
        remaining -= written;
    }
    if (!FlushFileBuffers(file)) ok = false;
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamoff length = in.tellg();
    if (length <= 0 || length > (std::numeric_limits<int>::max)()) return false;
    in.seekg(0, std::ios::beg);
    out.resize((size_t)length);
    in.read(reinterpret_cast<char*>(out.data()), length);
    return in.good();
}

bool SaveWebP(
    const std::vector<DWORD>& pixelsTopDown,
    BitmapSize size,
    const std::wstring& path,
    int quality,
    std::wstring* error)
{
    WebPConfig config = {};
    if (!WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, (float)ClampQuality(quality))) {
        SetError(error, L"Failed to initialize WebP encoder.");
        return false;
    }
    config.quality = (float)ClampQuality(quality);
    config.method = 3;
    config.exact = 1;
    if (!WebPValidateConfig(&config)) {
        SetError(error, L"Invalid WebP encoder configuration.");
        return false;
    }

    WebPPicture picture = {};
    if (!WebPPictureInit(&picture)) {
        SetError(error, L"Failed to initialize WebP picture.");
        return false;
    }
    picture.width = size.width;
    picture.height = size.height;

    WebPMemoryWriter writer = {};
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    bool ok = false;
    if (WebPPictureImportBGRA(
            &picture,
            reinterpret_cast<const uint8_t*>(pixelsTopDown.data()),
            size.width * (int)sizeof(DWORD)) &&
        WebPEncode(&config, &picture)) {
        ok = WriteBytesToFile(path, writer.mem, writer.size);
    }

    WebPPictureFree(&picture);
    WebPMemoryWriterClear(&writer);

    if (!ok) {
        DeleteFileW(path.c_str());
        SetError(error, L"Failed to save WebP image.");
    }
    return ok;
}

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";
    // OWN-96: pure parent dir (WideStringUtils).
    return WideExeDirFromModulePath(path);
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& child) {
    // OWN-96: pure path join (WideStringUtils).
    return WideJoinPath(dir, child);
}

std::wstring FindTool(const wchar_t* exeName) {
    std::wstring moduleDir = GetModuleDir();
    if (!moduleDir.empty()) {
        std::wstring bundled = JoinPath(JoinPath(moduleDir, L"imagecodecs"), exeName);
        if (FileExists(bundled)) return bundled;
    }

    std::wstring sourceTree = JoinPath(JoinPath(JoinPath(moduleDir, L".."), L"third_party\\imagecodecs\\bin"), exeName);
    if (FileExists(sourceTree)) return sourceTree;

    std::wstring cwdBundled = JoinPath(L"third_party\\imagecodecs\\bin", exeName);
    if (FileExists(cwdBundled)) return cwdBundled;

    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, exeName, nullptr, MAX_PATH, found, nullptr) > 0 && FileExists(found)) {
        return found;
    }
    return L"";
}

std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            backslashes++;
        } else if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(ch);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

bool RunTool(const std::vector<std::wstring>& args, DWORD* exitCode) {
    if (args.empty() || args[0].empty()) return false;
    std::wstring command;
    for (const auto& arg : args) {
        if (!command.empty()) command.push_back(L' ');
        command += QuoteArg(arg);
    }

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    // OWN-96: pure parent dir of tool exe (WideStringUtils).
    std::wstring workingDir;
    if (!args.empty()) {
        workingDir = WideParentDirFromPath(args[0]);
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDir.empty() ? nullptr : workingDir.c_str(),
            &si,
            &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCode) *exitCode = code;
    return code == 0;
}

std::wstring TempDir() {
    wchar_t tempPath[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) return L"";
    std::wstring dir = tempPath;
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L"\\";
    dir += L"ZenCrop";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

std::wstring UniqueTempPath(const wchar_t* suffix) {
    static std::atomic<unsigned> counter{0};
    std::wstring dir = TempDir();
    if (dir.empty()) return L"";

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    DWORD pid = GetCurrentProcessId();
    for (int attempt = 0; attempt < 100; ++attempt) {
        // OWN-113: pure codec temp name formatter (PID/counter/attempt still product args).
        std::wstring name = WideFormatCodecTempName(
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            (unsigned long)pid, ++counter, attempt, suffix);
        std::wstring path = JoinPath(dir, name);
        if (!FileExists(path)) return path;
    }
    return L"";
}

std::wstring AbsolutePath(const std::wstring& path) {
    wchar_t fullPath[MAX_PATH] = {};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr);
    if (len == 0 || len >= MAX_PATH) return path;
    return fullPath;
}

bool SaveAvifViaTool(
    const std::vector<DWORD>& pixelsTopDown,
    BitmapSize size,
    const std::wstring& path,
    const EncodeOptions& options,
    std::wstring* error)
{
    std::wstring avifenc = FindTool(L"avifenc.exe");
    if (avifenc.empty()) {
        SetError(error, L"AVIF encoder is not available.");
        return false;
    }

    std::wstring inputPng = UniqueTempPath(L".png");
    std::wstring outputAvif = UniqueTempPath(L".avif");
    if (inputPng.empty() || outputAvif.empty()) {
        SetError(error, L"Failed to create temporary AVIF files.");
        return false;
    }

    bool ok = SavePixelsWithGdiplus(pixelsTopDown, size, inputPng, ImageFileFormat::Png, 100, error);
    if (ok) {
        DWORD exitCode = 1;
        // OWN-127: pure int labels (WideStringUtils).
        std::wstring quality = WideFormatIntLabel(ClampQuality(options.quality));
        std::wstring alphaQuality = WideFormatIntLabel(
            options.inputAlphaPremultiplied ? 100 : ClampQuality(options.quality));
        std::wstring speed = WideFormatIntLabel(ClampSpeed(options.avifSpeed));
        std::vector<std::wstring> args = {
            avifenc,
            L"--yuv", L"444",
            L"--speed", speed,
            L"--qcolor", quality,
            L"--qalpha", alphaQuality,
        };
        if (options.inputAlphaPremultiplied) {
            // ConvertScreenshot input was normalized to straight alpha before
            // this bridge. Re-associate it here and signal the relationship in
            // the AVIF. Standards-aware decoders can recover straight color,
            // while alpha-blind Windows HEIF decoding retains the established
            // dark-canvas/soft-shadow fallback instead of exposing solid RGB.
            args.push_back(L"--premultiply");
        }
        args.push_back(inputPng);
        args.push_back(outputAvif);
        ok = RunTool(args, &exitCode);
        if (!ok) {
            SetError(error, L"Failed to encode AVIF image.");
        }
    }

    DeleteFileW(inputPng.c_str());
    if (ok) {
        DeleteFileW(path.c_str());
        ok = MoveFileExW(outputAvif.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != FALSE;
        if (!ok) SetError(error, L"Failed to write AVIF image.");
    }
    DeleteFileW(outputAvif.c_str());
    return ok;
}

Gdiplus::Bitmap* CloneGdiplusBitmap(Gdiplus::Bitmap* source) {
    if (!source || source->GetLastStatus() != Gdiplus::Ok ||
        source->GetWidth() == 0 || source->GetHeight() == 0) {
        return nullptr;
    }
    Gdiplus::Rect rc(0, 0, (INT)source->GetWidth(), (INT)source->GetHeight());
    Gdiplus::Bitmap* clone = source->Clone(rc, PixelFormat32bppARGB);
    if (!clone || clone->GetLastStatus() != Gdiplus::Ok) {
        delete clone;
        return nullptr;
    }
    return clone;
}

Gdiplus::Bitmap* LoadGdiplusBitmap(const std::wstring& path) {
    std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromFile(path.c_str()));
    return CloneGdiplusBitmap(source.get());
}

struct ScopedComInit {
    HRESULT hr = E_FAIL;
    bool initialized = false;
    ScopedComInit() {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hr == S_OK || hr == S_FALSE) initialized = true;
        if (hr == RPC_E_CHANGED_MODE) hr = S_OK;
    }
    ~ScopedComInit() {
        if (initialized) CoUninitialize();
    }
    bool ok() const { return SUCCEEDED(hr); }
};

template <typename T>
void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

Gdiplus::Bitmap* LoadWithWic(const std::wstring& path) {
    ScopedComInit com;
    if (!com.ok()) return nullptr;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    Gdiplus::Bitmap* bitmap = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(hr)) {
        UINT width = 0;
        UINT height = 0;
        converter->GetSize(&width, &height);
        if (width > 0 && height > 0 && width <= INT_MAX && height <= INT_MAX) {
            bitmap = new Gdiplus::Bitmap((INT)width, (INT)height, PixelFormat32bppARGB);
            Gdiplus::BitmapData data = {};
            Gdiplus::Rect rc(0, 0, (INT)width, (INT)height);
            if (bitmap->LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
                hr = converter->CopyPixels(nullptr, data.Stride, data.Stride * height, static_cast<BYTE*>(data.Scan0));
                bitmap->UnlockBits(&data);
            }
            if (FAILED(hr) || bitmap->GetLastStatus() != Gdiplus::Ok) {
                delete bitmap;
                bitmap = nullptr;
            }
        }
    }

    SafeRelease(converter);
    SafeRelease(frame);
    SafeRelease(decoder);
    SafeRelease(factory);
    return bitmap;
}

Gdiplus::Bitmap* LoadWebP(const std::wstring& path) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes)) return nullptr;

    int width = 0;
    int height = 0;
    uint8_t* bgra = WebPDecodeBGRA(bytes.data(), bytes.size(), &width, &height);
    if (!bgra || width <= 0 || height <= 0) {
        if (bgra) WebPFree(bgra);
        return nullptr;
    }

    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::BitmapData data = {};
    Gdiplus::Rect rc(0, 0, width, height);
    bool ok = false;
    if (bitmap->LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
        const int srcStride = width * 4;
        for (int y = 0; y < height; ++y) {
            memcpy(
                static_cast<BYTE*>(data.Scan0) + (size_t)y * data.Stride,
                bgra + (size_t)y * srcStride,
                srcStride);
        }
        bitmap->UnlockBits(&data);
        ok = true;
    }
    WebPFree(bgra);

    if (!ok || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return nullptr;
    }
    return bitmap;
}

Gdiplus::Bitmap* LoadAvifViaTool(const std::wstring& path) {
    std::wstring avifdec = FindTool(L"avifdec.exe");
    if (avifdec.empty()) return nullptr;

    std::wstring outputPng = UniqueTempPath(L".png");
    if (outputPng.empty()) return nullptr;

    DWORD exitCode = 1;
    bool ok = RunTool({ avifdec, AbsolutePath(path), outputPng }, &exitCode);
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    if (ok) {
        bitmap.reset(LoadGdiplusBitmap(outputPng));
    }
    DeleteFileW(outputPng.c_str());
    return bitmap.release();
}

} // namespace

ImageFileFormat FormatFromPath(const std::wstring& path) {
    // OWN-94: pure case-insensitive ext compare (WideStringUtils).
    const std::wstring ext = WideExtensionFromPath(path);
    if (ext.empty()) return ImageFileFormat::Unknown;
    if (WideEqualsNoCase(ext, L".png")) return ImageFileFormat::Png;
    if (WideEqualsNoCase(ext, L".jpg") || WideEqualsNoCase(ext, L".jpeg")) return ImageFileFormat::Jpeg;
    if (WideEqualsNoCase(ext, L".bmp")) return ImageFileFormat::Bmp;
    if (WideEqualsNoCase(ext, L".webp")) return ImageFileFormat::WebP;
    if (WideEqualsNoCase(ext, L".avif")) return ImageFileFormat::Avif;
    return ImageFileFormat::Unknown;
}

bool SaveHBitmapToFile(
    HBITMAP bitmap,
    const std::wstring& path,
    ImageFileFormat format,
    const EncodeOptions& options,
    std::wstring* error)
{
    if (!bitmap) {
        SetError(error, L"No image to save.");
        return false;
    }

    BitmapSize size = GetHBitmapSize(bitmap);
    if (size.width <= 0 || size.height <= 0) {
        SetError(error, L"No image to save.");
        return false;
    }

    std::vector<DWORD> pixelsTopDown;
    if (!ReadHBitmapPixelsTopDown(bitmap, size, pixelsTopDown)) {
        SetError(error, L"Failed to read image pixels.");
        return false;
    }

    const bool lossyOpaqueFormat = format == ImageFileFormat::Jpeg || format == ImageFileFormat::Bmp;
    NormalizeAlpha(pixelsTopDown, lossyOpaqueFormat);

    switch (format) {
    case ImageFileFormat::Png:
    case ImageFileFormat::Jpeg:
    case ImageFileFormat::Bmp:
        return SavePixelsWithGdiplus(pixelsTopDown, size, path, format, options.quality, error);
    case ImageFileFormat::WebP:
        return SaveWebP(pixelsTopDown, size, path, options.quality, error);
    case ImageFileFormat::Avif: {
        if (!options.inputAlphaPremultiplied) {
            return SaveAvifViaTool(pixelsTopDown, size, path, options, error);
        }
        std::vector<DWORD> straightPixels = pixelsTopDown;
        ConvertPremultipliedToStraightAlpha(straightPixels, size);
        return SaveAvifViaTool(straightPixels, size, path, options, error);
    }
    default:
        SetError(error, L"Unsupported image format.");
        return false;
    }
}

Gdiplus::Bitmap* LoadBitmapFromFile(const std::wstring& path, std::wstring* error) {
    ImageFileFormat format = FormatFromPath(path);
    Gdiplus::Bitmap* bitmap = nullptr;

    if (format == ImageFileFormat::WebP) {
        bitmap = LoadWebP(path);
        if (!bitmap) bitmap = LoadWithWic(path);
    } else if (format == ImageFileFormat::Avif) {
        bitmap = LoadAvifViaTool(path);
        if (!bitmap) bitmap = LoadWithWic(path);
    } else {
        bitmap = LoadGdiplusBitmap(path);
        if (!bitmap) bitmap = LoadWithWic(path);
    }

    if (!bitmap) {
        SetError(error, L"Failed to load image file.");
    }
    return bitmap;
}

HBITMAP LoadHBitmapFromFile(const std::wstring& path, std::wstring* error) {
    std::unique_ptr<Gdiplus::Bitmap> bitmap(LoadBitmapFromFile(path, error));
    if (!bitmap) return nullptr;

    HBITMAP hBitmap = nullptr;
    if (bitmap->GetHBITMAP(Gdiplus::Color(255, 255, 255, 255), &hBitmap) != Gdiplus::Ok) {
        SetError(error, L"Failed to create bitmap.");
        return nullptr;
    }
    return hBitmap;
}

} // namespace ImageCodec
