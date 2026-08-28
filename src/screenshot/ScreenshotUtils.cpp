#include "ScreenshotUtils.h"
#include "core/ClipboardUtils.h"
#include "core/WideStringUtils.h"
#include "image/BitmapCodec.h"
#include "AppMessages.h"
#include <gdiplus.h>
#include <knownfolders.h>
#include <limits>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vector>

namespace Screenshot {


static void DrawCursorIfNeeded(HDC hdc, const RECT& screenRect) {
    CURSORINFO cursorInfo = { sizeof(cursorInfo) };
    if (!GetCursorInfo(&cursorInfo) || !(cursorInfo.flags & CURSOR_SHOWING) || !cursorInfo.hCursor) {
        return;
    }

    ICONINFO iconInfo = {};
    if (!GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
        return;
    }

    int x = cursorInfo.ptScreenPos.x - (int)iconInfo.xHotspot - screenRect.left;
    int y = cursorInfo.ptScreenPos.y - (int)iconInfo.yHotspot - screenRect.top;
    DrawIconEx(hdc, x, y, cursorInfo.hCursor, 0, 0, 0, nullptr, DI_NORMAL);

    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
}

BitmapSize GetBitmapSize(HBITMAP hBitmap) {
    BitmapSize size;
    if (!hBitmap) return size;

    BITMAP bm = {};
    if (GetObjectW(hBitmap, sizeof(bm), &bm)) {
        size.width = bm.bmWidth;
        size.height = bm.bmHeight;
    }
    return size;
}

bool BitmapHasTransparentPixels(HBITMAP hBitmap) {
    BitmapSize size = GetBitmapSize(hBitmap);
    if (size.width <= 0 || size.height <= 0) return false;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size.width;
    bmi.bmiHeader.biHeight = -size.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<DWORD> pixels((size_t)size.width * size.height);
    HDC hdc = GetDC(nullptr);
    int lines = GetDIBits(hdc, hBitmap, 0, size.height, pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    if (lines == 0) return false;

    for (DWORD pixel : pixels) {
        if (((pixel >> 24) & 0xFF) < 255) return true;
    }
    return false;
}

HBITMAP DuplicateBitmap(HBITMAP hBitmap) {
    BitmapSize size = GetBitmapSize(hBitmap);
    if (size.width <= 0 || size.height <= 0) return nullptr;

    HDC hScreen = GetDC(nullptr);
    if (!hScreen) return nullptr;

    HDC hSrc = CreateCompatibleDC(hScreen);
    HDC hDst = CreateCompatibleDC(hScreen);
    if (!hSrc || !hDst) {
        if (hSrc) DeleteDC(hSrc);
        if (hDst) DeleteDC(hDst);
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size.width;
    bmi.bmiHeader.biHeight = -size.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP copy = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!copy || !bits) {
        if (copy) DeleteObject(copy);
        DeleteDC(hSrc);
        DeleteDC(hDst);
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    HBITMAP oldSrc = (HBITMAP)SelectObject(hSrc, hBitmap);
    HBITMAP oldDst = (HBITMAP)SelectObject(hDst, copy);
    BOOL copied = BitBlt(hDst, 0, 0, size.width, size.height, hSrc, 0, 0, SRCCOPY);
    if (oldSrc) SelectObject(hSrc, oldSrc);
    if (oldDst) SelectObject(hDst, oldDst);

    DeleteDC(hSrc);
    DeleteDC(hDst);
    ReleaseDC(nullptr, hScreen);

    if (!copied) {
        DeleteObject(copy);
        return nullptr;
    }
    return copy;
}

HBITMAP CaptureScreenRect(const RECT& rect, bool includeCursor) {
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return nullptr;

    HDC hScreen = GetDC(nullptr);
    if (!hScreen) return nullptr;

    HDC hMem = CreateCompatibleDC(hScreen);
    if (!hMem) {
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBitmap || !bits) {
        if (hBitmap) DeleteObject(hBitmap);
        DeleteDC(hMem);
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    HBITMAP old = (HBITMAP)SelectObject(hMem, hBitmap);
    BOOL copied = BitBlt(hMem, 0, 0, width, height, hScreen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
    if (copied && includeCursor) {
        DrawCursorIfNeeded(hMem, rect);
    }
    SelectObject(hMem, old);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);

    if (!copied) {
        DeleteObject(hBitmap);
        return nullptr;
    }

    return hBitmap;
}


bool CopyBitmapToClipboard(HWND owner, HBITMAP hBitmap, bool alphaPremultiplied) {
    // Keep DIB/PNG clipboard compatibility in core, but make the CF_HDROP file
    // handed to Explorer match the screenshot Save/Quick Save format setting.
    ScreenshotSettings settings = LoadScreenshotSettings();
    std::wstring path = BuildClipboardTempFilePath(FormatExtension(settings.format));
    if (path.empty()) return false;

    std::wstring error;
    if (!SaveBitmapToFile(
            hBitmap,
            path,
            settings.format,
            settings.jpegQuality,
            &error,
            alphaPremultiplied)) {
        DeleteFileW(path.c_str());
        return false;
    }

    if (::CopyBitmapToClipboard(owner, hBitmap, path)) return true;
    DeleteFileW(path.c_str());
    return false;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    // Stage3 3-A-3: sole text clipboard in core; screenshot wrapper only.
    return ::CopyTextToClipboard(owner, text);
}

const wchar_t* FormatExtension(ScreenshotFormat format) {
    switch (format) {
    case ScreenshotFormat::Jpeg: return L".jpg";
    case ScreenshotFormat::Bmp: return L".bmp";
    case ScreenshotFormat::WebP: return L".webp";
    case ScreenshotFormat::Avif: return L".avif";
    case ScreenshotFormat::Png:
    default:
        return L".png";
    }
}

ScreenshotFormat FormatFromPathOrDefault(const std::wstring& path, ScreenshotFormat fallback) {
    // OWN-95: pure extension extract (WideStringUtils).
    const std::wstring ext = WideExtensionFromPath(path);
    if (ext.empty()) return fallback;
    if (WideEqualsNoCase(ext, L".jpg") || WideEqualsNoCase(ext, L".jpeg")) return ScreenshotFormat::Jpeg;
    if (WideEqualsNoCase(ext, L".bmp")) return ScreenshotFormat::Bmp;
    if (WideEqualsNoCase(ext, L".webp")) return ScreenshotFormat::WebP;
    if (WideEqualsNoCase(ext, L".avif")) return ScreenshotFormat::Avif;
    if (WideEqualsNoCase(ext, L".png")) return ScreenshotFormat::Png;
    return fallback;
}

std::wstring EnsureExtensionForFormat(const std::wstring& path, ScreenshotFormat format) {
    // OWN-95: pure extension presence check.
    if (!WideExtensionFromPath(path).empty()) return path;
    return path + FormatExtension(format);
}

std::wstring DefaultQuickSaveDir() {
    PWSTR pictures = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT, nullptr, &pictures)) && pictures) {
        dir = pictures;
        CoTaskMemFree(pictures);
    }
    if (dir.empty()) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        dir = WideExeDirFromModulePath(path);
    }
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') {
        dir += L"\\";
    }
    dir += L"ZenCrop";
    return dir;
}

static void ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.length(), to);
        pos += to.length();
    }
}

static std::wstring BuildFileNameFromTemplate(const ScreenshotSettings& settings) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    // OWN-114: pure compact date/time/ms parts (WideStringUtils).
    const std::wstring date = WideFormatYmdCompact(st.wYear, st.wMonth, st.wDay);
    const std::wstring time = WideFormatHmsCompact(st.wHour, st.wMinute, st.wSecond);
    const std::wstring ms = WideFormatMs3(st.wMilliseconds);

    std::wstring name = settings.fileNameTemplate.empty()
        ? L"ZenCrop_{yyyyMMdd}_{HHmmss}_{fff}"
        : settings.fileNameTemplate;
    ReplaceAll(name, L"{yyyyMMdd}", date);
    ReplaceAll(name, L"{HHmmss}", time);
    ReplaceAll(name, L"{fff}", ms);

    for (wchar_t& ch : name) {
        if (ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' ||
            ch == L'/' || ch == L'\\' || ch == L'|' || ch == L'?' || ch == L'*') {
            ch = L'_';
        }
    }
    if (name.empty()) name = L"ZenCrop";
    return EnsureExtensionForFormat(name, settings.format);
}

std::wstring BuildQuickSavePath(const ScreenshotSettings& settings) {
    std::wstring dir = settings.quickSaveDir.empty() ? DefaultQuickSaveDir() : settings.quickSaveDir;
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);

    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') {
        dir += L"\\";
    }
    return dir + BuildFileNameFromTemplate(settings);
}

bool SaveBitmapToFile(
    HBITMAP hBitmap,
    const std::wstring& path,
    ScreenshotFormat format,
    int jpegQuality,
    std::wstring* error,
    bool alphaPremultiplied) {
    ImageCodec::ImageFileFormat codecFormat = ImageCodec::ImageFileFormat::Png;
    switch (format) {
    case ScreenshotFormat::Jpeg: codecFormat = ImageCodec::ImageFileFormat::Jpeg; break;
    case ScreenshotFormat::Bmp: codecFormat = ImageCodec::ImageFileFormat::Bmp; break;
    case ScreenshotFormat::WebP: codecFormat = ImageCodec::ImageFileFormat::WebP; break;
    case ScreenshotFormat::Avif: codecFormat = ImageCodec::ImageFileFormat::Avif; break;
    case ScreenshotFormat::Png:
    default: codecFormat = ImageCodec::ImageFileFormat::Png; break;
    }

    ImageCodec::EncodeOptions options;
    options.quality = jpegQuality;
    options.avifSpeed = 6;
    options.inputAlphaPremultiplied =
        alphaPremultiplied && format == ScreenshotFormat::Avif;
    return ImageCodec::SaveHBitmapToFile(hBitmap, path, codecFormat, options, error);
}

}
