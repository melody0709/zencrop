#pragma once

// D-I-3: free Host helpers for multi-TU conversion (was Window.cpp / StateAndHelpers statics).

#include "ocr/OcrUtils.h"
#include "ocr/ui/dashboard/DashboardDialogLayout.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "core/WideStringUtils.h"
#include "image/BitmapCodec.h"

#include <atomic>
#include <gdiplus.h>
#include <memory>
#include <objbase.h>
#include <objidl.h>
#include <shellscalingapi.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <windows.h>

// Design DPI alias (same 144 as dialogs).
inline constexpr UINT kDashboardHostDesignDpi = kDashboardDialogDesignDpi;

// COM apartment helper for Host dialogs / OLE (was StateAndHelpers static).
inline bool DashboardEnsureComForDashboard(bool& shouldUninitialize) {
    shouldUninitialize = false;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        shouldUninitialize = true;
        return true;
    }
    return hr == RPC_E_CHANGED_MODE;
}

inline UINT DashboardGetMonitorEffectiveDpi(HMONITOR hMon) {
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (hMon && SUCCEEDED(GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0) {
        return dpiX;
    }
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        dpiX = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
    }
    return dpiX > 0 ? dpiX : 96;
}

// Alias: same design DPI 144 as dialog font helper.
inline HFONT DashboardCreateHostFont(int designPixelHeight, UINT dpi, int weight = FW_NORMAL) {
    return DashboardCreateDialogFont(designPixelHeight, dpi, weight);
}

inline std::wstring DashboardDisplayFileName(const std::wstring& path) {
    std::wstring name = DashboardFileNameFromPath(path);
    return name.empty() ? path : name;
}

inline bool DashboardCanonicalizePath(const std::wstring& path, std::wstring& out) {
    wchar_t full[MAX_PATH] = {};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    if (len == 0 || len >= MAX_PATH) return false;
    wchar_t canonical[MAX_PATH] = {};
    if (!PathCanonicalizeW(canonical, full)) return false;
    out = WideToLower(canonical);
    return true;
}

inline bool DashboardIsPathInOcrImageCache(const std::wstring& path) {
    std::wstring fullPath;
    std::wstring fullDir;
    if (!DashboardCanonicalizePath(path, fullPath) ||
        !DashboardCanonicalizePath(GetOcrImageDir(), fullDir)) {
        return false;
    }
    return WideIsPathStrictlyUnderDirectory(fullPath, fullDir);
}

inline bool DashboardDirectoryExistsWide(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Win32 full-path expand + pure trailing-sep trim + lower (compare key).
inline std::wstring DashboardNormalizePathForCompare(const std::wstring& path) {
    if (path.empty()) return L"";
    wchar_t full[MAX_PATH] = {};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    std::wstring result = (len > 0 && len < MAX_PATH) ? std::wstring(full) : path;
    result = DashboardTrimTrailingSeparators(std::move(result));
    return DashboardToLowerWide(std::move(result));
}

// Process-wide generation counter (was file-static g_dashboardGeneration).
uint64_t DashboardNextHostGeneration();

// Implementations with process-wide counters live in DashboardHostUtils.cpp.
std::wstring DashboardMakeOcrImageCachePath(const wchar_t* prefix);
std::wstring DashboardMakeOcrImportCacheFilePath(const std::wstring& originalName);
bool DashboardGetPngEncoderClsid(CLSID& clsid);
bool DashboardSaveBitmapAsPng(Gdiplus::Bitmap* bitmap, const std::wstring& destPath);
bool DashboardCacheImageForHistory(
    const std::wstring& sourcePath,
    std::wstring& cachedPath,
    bool* created = nullptr);

// OLE drop medium → cache file (Import TU).
bool DashboardWriteBytesToFile(const std::wstring& path, const void* data, DWORD size);
bool DashboardWriteStreamToFile(IStream* stream, const std::wstring& path);
bool DashboardWriteStorageMediumToFile(STGMEDIUM& medium, const std::wstring& path);

// Block label colors (shared by Blocks paint + Messages ImageArea paint).
inline COLORREF DashboardColorRefForBlockLabel(const std::wstring& label) {
    std::wstring cls = WideToLower(label);
    if (cls == L"table" || cls == L"table_caption" || cls == L"table_title") return RGB(0, 186, 173);
    if (cls == L"image" || cls == L"chart" || cls == L"seal" || cls == L"figure") return RGB(189, 76, 255);
    if (cls == L"display_formula" || cls == L"inline_formula" || cls == L"formula_number") return RGB(250, 219, 20);
    if (cls == L"doc_title" || cls == L"paragraph_title" || cls == L"figure_title" || cls == L"header") return RGB(182, 178, 241);
    if (cls == L"footer" || cls == L"footnote" || cls == L"vision_footnote") return RGB(128, 140, 158);
    if (cls == L"algorithm") return RGB(255, 156, 40);
    return RGB(70, 88, 255);
}

inline Gdiplus::Color DashboardGdiColorForBlockLabel(const std::wstring& label, BYTE alpha) {
    COLORREF c = DashboardColorRefForBlockLabel(label);
    return Gdiplus::Color(
        alpha,
        WideUnpackR(static_cast<unsigned int>(c)),
        WideUnpackG(static_cast<unsigned int>(c)),
        WideUnpackB(static_cast<unsigned int>(c)));
}
