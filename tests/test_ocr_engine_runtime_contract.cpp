#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "ocr/engine/OcrEngine_Local.h"
#include "Settings.h"

OcrSettings LoadOcrSettings() {
    OcrSettings settings;
    settings.mode = L"local";
    settings.language = L"auto";
    return settings;
}

void SaveOcrSettings(const OcrSettings&) {}

void GetBitmapBits32(HBITMAP hBitmap, int& width, int& height, std::vector<uint8_t>& pixels) {
    BITMAP bmp = {};
    GetObject(hBitmap, sizeof(BITMAP), &bmp);
    width = bmp.bmWidth;
    height = bmp.bmHeight;
    pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(nullptr);
    GetDIBits(dc, hBitmap, 0, height, pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
}

static HBITMAP CreateOcrSmokeBitmap() {
    const int width = 520;
    const int height = 160;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!bitmap) return nullptr;

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    RECT rc = {0, 0, width, height};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rc, white);
    DeleteObject(white);

    HFONT font = CreateFontW(
        72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    RECT textRc = {24, 30, width - 24, height - 24};
    DrawTextW(dc, L"ZEN CROP 123", -1, &textRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBitmap);
    DeleteDC(dc);
    return bitmap;
}

static std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len), '\0');
    int written = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), -1, out.data(), len, nullptr, nullptr);
    if (written <= 1) return {};
    out.resize(static_cast<size_t>(written - 1));
    return out;
}

int wmain() {
    OcrEngineLocal engine;
    if (!engine.IsAvailable()) {
        std::cout << "Windows local OCR engine is unavailable; runtime recognize smoke skipped.\n";
        return 0;
    }

    HBITMAP bitmap = CreateOcrSmokeBitmap();
    if (!bitmap) {
        std::wcerr << L"Failed to create OCR smoke bitmap.\n";
        return 1;
    }

    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done) {
        DeleteObject(bitmap);
        std::wcerr << L"Failed to create completion event.\n";
        return 1;
    }

    OcrOutput output;
    engine.Recognize(bitmap, [&](OcrOutput out) {
        output = std::move(out);
        SetEvent(done);
    });

    DWORD wait = WaitForSingleObject(done, 60000);
    CloseHandle(done);
    if (wait != WAIT_OBJECT_0) {
        std::wcerr << L"Timed out waiting for local OCR result.\n";
        return 1;
    }

    if (!output.success || !output.error.empty() || output.text.empty()) {
        std::wcerr << L"Local OCR smoke failed. success=" << output.success
                   << L" error=" << output.error
                   << L" text=" << output.text << L"\n";
        return 1;
    }

    std::wstring lower = output.text;
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    if (lower.find(L"zen") == std::wstring::npos &&
        lower.find(L"crop") == std::wstring::npos &&
        lower.find(L"123") == std::wstring::npos) {
        std::wcerr << L"Local OCR smoke returned unexpected text: " << output.text << L"\n";
        return 1;
    }

    std::cout << "Local OCR runtime contract passed: " << ToUtf8(output.text) << "\n";
    return 0;
}
