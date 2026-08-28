#define WIN32_LEAN_AND_MEAN
#include "ocr/ui/dashboard/DashboardHistoryCache.h"

#include <windows.h>
#include <shlwapi.h>

// D-C-S2: sole OS canonicalize for history cache ownership (lowered full path).
bool DashboardHistoryCacheCanonicalizePath(std::wstring path, std::wstring& out)
{
    wchar_t full[MAX_PATH] = {};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    if (len == 0 || len >= MAX_PATH) return false;
    wchar_t canonical[MAX_PATH] = {};
    if (!PathCanonicalizeW(canonical, full)) return false;
    out = WideToLower(canonical);
    return true;
}
