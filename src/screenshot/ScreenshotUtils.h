#pragma once

#include "Settings.h"
#include <windows.h>
#include <string>

namespace Screenshot {

struct BitmapSize {
    int width = 0;
    int height = 0;
};

BitmapSize GetBitmapSize(HBITMAP hBitmap);
bool BitmapHasTransparentPixels(HBITMAP hBitmap);
HBITMAP DuplicateBitmap(HBITMAP hBitmap);
HBITMAP CaptureScreenRect(const RECT& rect, bool includeCursor);

// Stage3 3-A-3: wrappers over core/ClipboardUtils (ocr_ui must include core, not this).
bool CopyBitmapToClipboard(HWND owner, HBITMAP hBitmap, bool alphaPremultiplied = false);
bool CopyTextToClipboard(HWND owner, const std::wstring& text);

const wchar_t* FormatExtension(ScreenshotFormat format);
ScreenshotFormat FormatFromPathOrDefault(const std::wstring& path, ScreenshotFormat fallback);
std::wstring EnsureExtensionForFormat(const std::wstring& path, ScreenshotFormat format);
std::wstring DefaultQuickSaveDir();
std::wstring BuildQuickSavePath(const ScreenshotSettings& settings);
bool SaveBitmapToFile(
    HBITMAP hBitmap,
    const std::wstring& path,
    ScreenshotFormat format,
    int jpegQuality,
    std::wstring* error = nullptr,
    bool alphaPremultiplied = false);

}
