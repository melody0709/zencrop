#include "core/ClipboardUtils.h"

#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>

#include <limits>
#include <string>
#include <vector>

// Stage3 3-A-3: sole clipboard implementation (HDROP+DIBV5+DIB+PNG; temp PNG file).
// No screenshot Settings dependency. Dual ScreenshotUtils body deleted.

namespace {

struct BitmapSize {
    int width = 0;
    int height = 0;
};

BitmapSize GetBitmapSizeLocal(HBITMAP hBitmap)
{
    BITMAP bm = {};
    if (!hBitmap || !GetObjectW(hBitmap, sizeof(bm), &bm)) {
        return {};
    }
    return { bm.bmWidth, bm.bmHeight };
}

bool GetBitmapPixelsTopDown(HBITMAP hBitmap, BitmapSize size, std::vector<DWORD>& pixels)
{
    if (!hBitmap || size.width <= 0 || size.height <= 0) return false;
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
    int lines = GetDIBits(hdc, hBitmap, 0, size.height, pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    return lines == size.height;
}

void NormalizeClipboardAlpha(std::vector<DWORD>& pixels)
{
    bool anyAlpha = false;
    bool anyTransparent = false;
    for (DWORD pixel : pixels) {
        BYTE alpha = (BYTE)((pixel >> 24) & 0xFF);
        if (alpha != 0) anyAlpha = true;
        if (alpha != 0xFF) anyTransparent = true;
    }

    if (!anyAlpha || !anyTransparent) {
        for (DWORD& pixel : pixels) {
            pixel = 0xFF000000 | (pixel & 0x00FFFFFF);
        }
    }
}

HGLOBAL CreateClipboardDib(const std::vector<DWORD>& pixelsTopDown, BitmapSize size)
{
    if (size.width <= 0 || size.height <= 0) return nullptr;
    size_t stride = (size_t)size.width * sizeof(DWORD);
    size_t imageSize = stride * (size_t)size.height;
    if (imageSize > (std::numeric_limits<DWORD>::max)()) return nullptr;
    if (imageSize > (std::numeric_limits<SIZE_T>::max)() - sizeof(BITMAPINFOHEADER)) return nullptr;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
    if (!hMem) return nullptr;

    BYTE* locked = static_cast<BYTE*>(GlobalLock(hMem));
    if (!locked) {
        GlobalFree(hMem);
        return nullptr;
    }

    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = size.width;
    header.biHeight = size.height;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = (DWORD)imageSize;
    memcpy(locked, &header, sizeof(header));

    DWORD* dst = reinterpret_cast<DWORD*>(locked + sizeof(BITMAPINFOHEADER));
    for (int y = size.height - 1; y >= 0; --y) {
        const DWORD* src = pixelsTopDown.data() + (size_t)y * size.width;
        for (int x = 0; x < size.width; ++x) {
            dst[x] = 0xFF000000 | (src[x] & 0x00FFFFFF);
        }
        dst += size.width;
    }

    GlobalUnlock(hMem);
    return hMem;
}

HGLOBAL CreateClipboardDibV5(const std::vector<DWORD>& pixelsTopDown, BitmapSize size)
{
    if (size.width <= 0 || size.height <= 0) return nullptr;
    size_t stride = (size_t)size.width * sizeof(DWORD);
    size_t imageSize = stride * (size_t)size.height;
    if (imageSize > (std::numeric_limits<DWORD>::max)()) return nullptr;
    if (imageSize > (std::numeric_limits<SIZE_T>::max)() - sizeof(BITMAPV5HEADER)) return nullptr;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + imageSize);
    if (!hMem) return nullptr;

    BYTE* locked = static_cast<BYTE*>(GlobalLock(hMem));
    if (!locked) {
        GlobalFree(hMem);
        return nullptr;
    }

    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = size.width;
    header.bV5Height = size.height;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = (DWORD)imageSize;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    header.bV5CSType = LCS_sRGB;
    header.bV5Intent = LCS_GM_IMAGES;
    memcpy(locked, &header, sizeof(header));

    DWORD* dst = reinterpret_cast<DWORD*>(locked + sizeof(BITMAPV5HEADER));
    for (int y = size.height - 1; y >= 0; --y) {
        const DWORD* src = pixelsTopDown.data() + (size_t)y * size.width;
        for (int x = 0; x < size.width; ++x) {
            DWORD pixel = src[x];
            BYTE alpha = (BYTE)((pixel >> 24) & 0xFF);
            dst[x] = alpha == 0 ? 0x00FFFFFF : pixel;
        }
        dst += size.width;
    }

    GlobalUnlock(hMem);
    return hMem;
}

bool GetEncoderClsid(const wchar_t* mimeType, CLSID& clsid)
{
    UINT count = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) return false;

    std::vector<Gdiplus::ImageCodecInfo> encoders(size / sizeof(Gdiplus::ImageCodecInfo));
    if (Gdiplus::GetImageEncoders(count, size, encoders.data()) != Gdiplus::Ok) {
        return false;
    }

    for (UINT i = 0; i < count; i++) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, mimeType) == 0) {
            clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

HGLOBAL CreateClipboardPng(const std::vector<DWORD>& pixelsTopDown, BitmapSize size)
{
    if (size.width <= 0 || size.height <= 0) return nullptr;

    CLSID pngClsid = {};
    if (!GetEncoderClsid(L"image/png", pngClsid)) return nullptr;

    Gdiplus::Bitmap bitmap(
        size.width,
        size.height,
        size.width * (INT)sizeof(DWORD),
        PixelFormat32bppARGB,
        reinterpret_cast<BYTE*>(const_cast<DWORD*>(pixelsTopDown.data())));

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || !stream) {
        return nullptr;
    }

    if (bitmap.Save(stream, &pngClsid, nullptr) != Gdiplus::Ok) {
        stream->Release();
        return nullptr;
    }

    HGLOBAL hStreamMem = nullptr;
    if (FAILED(GetHGlobalFromStream(stream, &hStreamMem)) || !hStreamMem) {
        stream->Release();
        return nullptr;
    }

    SIZE_T sizeBytes = GlobalSize(hStreamMem);
    if (sizeBytes == 0) {
        stream->Release();
        return nullptr;
    }

    void* src = GlobalLock(hStreamMem);
    if (!src) {
        stream->Release();
        return nullptr;
    }

    HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, sizeBytes);
    if (!hPng) {
        GlobalUnlock(hStreamMem);
        stream->Release();
        return nullptr;
    }

    void* dst = GlobalLock(hPng);
    if (!dst) {
        GlobalFree(hPng);
        GlobalUnlock(hStreamMem);
        stream->Release();
        return nullptr;
    }

    memcpy(dst, src, sizeBytes);
    GlobalUnlock(hPng);
    GlobalUnlock(hStreamMem);
    stream->Release();
    return hPng;
}

HBITMAP CreateBitmapFromTopDownPixels(const std::vector<DWORD>& pixelsTopDown, BitmapSize size)
{
    if (size.width <= 0 || size.height <= 0) return nullptr;
    if (pixelsTopDown.size() != (size_t)size.width * (size_t)size.height) return nullptr;

    HDC hdc = GetDC(nullptr);
    if (!hdc) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size.width;
    bmi.bmiHeader.biHeight = -size.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hBitmap || !bits) {
        if (hBitmap) DeleteObject(hBitmap);
        return nullptr;
    }

    memcpy(bits, pixelsTopDown.data(), pixelsTopDown.size() * sizeof(DWORD));
    return hBitmap;
}

std::wstring ClipboardTempDir()
{
    wchar_t tempPath[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) return L"";

    std::wstring dir = tempPath;
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') {
        dir += L"\\";
    }
    dir += L"ZenCrop";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

void CleanupOldClipboardTempFiles(const std::wstring& dir)
{
    if (dir.empty()) return;

    FILETIME nowFt = {};
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now = {};
    now.LowPart = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;
    constexpr unsigned long long kOneDay100Ns = 24ull * 60ull * 60ull * 10000000ull;

    std::wstring pattern = dir + L"\\ZenCrop_clip_*";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;

        ULARGE_INTEGER write = {};
        write.LowPart = data.ftLastWriteTime.dwLowDateTime;
        write.HighPart = data.ftLastWriteTime.dwHighDateTime;
        if (now.QuadPart > write.QuadPart && now.QuadPart - write.QuadPart > kOneDay100Ns) {
            std::wstring path = dir + L"\\" + data.cFileName;
            DeleteFileW(path.c_str());
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

bool SaveClipboardTempPngFile(HBITMAP hBitmap, std::wstring& outPath)
{
    if (!hBitmap) return false;
    std::wstring path = BuildClipboardTempFilePath(L".png");
    if (path.empty()) return false;

    CLSID pngClsid = {};
    if (!GetEncoderClsid(L"image/png", pngClsid)) return false;

    Gdiplus::Bitmap bitmap(hBitmap, nullptr);
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;
    if (bitmap.Save(path.c_str(), &pngClsid, nullptr) != Gdiplus::Ok) return false;

    outPath = path;
    return true;
}

HGLOBAL CreateClipboardHDrop(const std::wstring& filePath)
{
    if (filePath.empty()) return nullptr;

    SIZE_T chars = filePath.length() + 2;
    SIZE_T bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) return nullptr;

    BYTE* locked = static_cast<BYTE*>(GlobalLock(hMem));
    if (!locked) {
        GlobalFree(hMem);
        return nullptr;
    }

    ZeroMemory(locked, bytes);
    DROPFILES* drop = reinterpret_cast<DROPFILES*>(locked);
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    wchar_t* paths = reinterpret_cast<wchar_t*>(locked + sizeof(DROPFILES));
    memcpy(paths, filePath.c_str(), filePath.length() * sizeof(wchar_t));

    GlobalUnlock(hMem);
    return hMem;
}

HGLOBAL CreateClipboardDropEffect()
{
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!hMem) return nullptr;

    DWORD* effect = static_cast<DWORD*>(GlobalLock(hMem));
    if (!effect) {
        GlobalFree(hMem);
        return nullptr;
    }

    *effect = DROPEFFECT_COPY;
    GlobalUnlock(hMem);
    return hMem;
}

} // namespace

std::wstring BuildClipboardTempFilePath(const std::wstring& extension)
{
    if (extension.size() < 2 || extension.front() != L'.' ||
        extension.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
        return L"";
    }

    std::wstring dir = ClipboardTempDir();
    if (dir.empty()) return L"";
    CleanupOldClipboardTempFiles(dir);

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    DWORD pid = GetCurrentProcessId();

    for (int attempt = 0; attempt < 100; ++attempt) {
        wchar_t stem[128] = {};
        swprintf_s(stem, L"ZenCrop_clip_%04u%02u%02u_%02u%02u%02u_%03u_%lu_%d",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            pid, attempt);
        std::wstring path = dir + L"\\" + stem + extension;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }
    return L"";
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(wchar_t));
    if (!hMem) return false;

    wchar_t* dst = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!dst) {
        GlobalFree(hMem);
        return false;
    }
    wcscpy_s(dst, text.length() + 1, text.c_str());
    GlobalUnlock(hMem);

    if (!OpenClipboard(owner)) {
        GlobalFree(hMem);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        CloseClipboard();
        GlobalFree(hMem);
        return false;
    }
    CloseClipboard();
    return true;
}

bool CopyBitmapToClipboard(HWND owner, HBITMAP hBitmap, const std::wstring& fileDropPath)
{
    // Sole clipboard implementation: CF_HDROP + PREFERRED_DROPEFFECT + CF_DIBV5 + CF_DIB + PNG.
    // Screenshot may provide its Settings-encoded CF_HDROP file; generic callers
    // retain the PNG temp-file fallback without creating a Settings dependency here.
    BitmapSize size = GetBitmapSizeLocal(hBitmap);
    if (size.width <= 0 || size.height <= 0) return false;
    if (!fileDropPath.empty() && GetFileAttributesW(fileDropPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    std::vector<DWORD> pixelsTopDown;
    if (!GetBitmapPixelsTopDown(hBitmap, size, pixelsTopDown)) return false;
    NormalizeClipboardAlpha(pixelsTopDown);

    HGLOBAL hDibV5 = CreateClipboardDibV5(pixelsTopDown, size);
    HGLOBAL hDib = CreateClipboardDib(pixelsTopDown, size);
    HGLOBAL hPng = CreateClipboardPng(pixelsTopDown, size);

    std::wstring tempFile = fileDropPath;
    if (tempFile.empty()) {
        HBITMAP normalizedBitmap = CreateBitmapFromTopDownPixels(pixelsTopDown, size);
        if (normalizedBitmap) {
            SaveClipboardTempPngFile(normalizedBitmap, tempFile);
            DeleteObject(normalizedBitmap);
        }
    }

    HGLOBAL hDrop = CreateClipboardHDrop(tempFile);
    HGLOBAL hDropEffect = hDrop ? CreateClipboardDropEffect() : nullptr;
    if (!hDibV5 && !hDib && !hPng && !hDrop) return false;

    if (!OpenClipboard(owner)) {
        if (hDibV5) GlobalFree(hDibV5);
        if (hDib) GlobalFree(hDib);
        if (hPng) GlobalFree(hPng);
        if (hDrop) GlobalFree(hDrop);
        if (hDropEffect) GlobalFree(hDropEffect);
        return false;
    }

    EmptyClipboard();
    bool copied = false;
    bool fileDropCopied = false;

    if (hDrop && SetClipboardData(CF_HDROP, hDrop)) {
        hDrop = nullptr;
        copied = true;
        fileDropCopied = true;
    }
    UINT dropEffectFormat = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (dropEffectFormat != 0 && hDropEffect && SetClipboardData(dropEffectFormat, hDropEffect)) {
        hDropEffect = nullptr;
    }
    if (hDibV5 && SetClipboardData(CF_DIBV5, hDibV5)) {
        hDibV5 = nullptr;
        copied = true;
    }
    if (hDib && SetClipboardData(CF_DIB, hDib)) {
        hDib = nullptr;
        copied = true;
    }
    UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (pngFormat != 0 && hPng && SetClipboardData(pngFormat, hPng)) {
        hPng = nullptr;
        copied = true;
    }

    CloseClipboard();

    if (hDibV5) GlobalFree(hDibV5);
    if (hDib) GlobalFree(hDib);
    if (hPng) GlobalFree(hPng);
    if (hDrop) GlobalFree(hDrop);
    if (hDropEffect) GlobalFree(hDropEffect);
    return copied && (fileDropPath.empty() || fileDropCopied);
}
