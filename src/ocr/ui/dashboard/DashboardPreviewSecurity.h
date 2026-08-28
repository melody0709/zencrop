#pragma once

#include "core/WideStringUtils.h"

#include <string>

// Stage 1 D-H seed: pure preview asset path allow-list (no filesystem / HWND).

// Accept only relative assets\* image paths without .. traversal or absolute roots.
// On success, normalized uses backslashes and retains the assets\ prefix.
inline bool DashboardPreviewIsSafeRelativeAssetPath(
    std::wstring relPath,
    std::wstring& normalized)
{
    normalized.clear();
    if (relPath.empty() ||
        relPath.find(L":") != std::wstring::npos ||
        relPath.rfind(L"//", 0) == 0 ||
        relPath.front() == L'/' ||
        relPath.front() == L'\\') {
        return false;
    }
    // OWN-79: pure slash normalize (WideStringUtils).
    relPath = WideToBackSlashes(std::move(relPath));

    // OWN-79: pure case-insensitive prefix (WideStringUtils).
    if (WideStartsWithNoCase(relPath, L".\\assets\\")) {
        relPath.erase(0, 2);
    } else if (WideStartsWithNoCase(relPath, L"..\\assets\\")) {
        relPath.erase(0, 3);
    }
    if (!WideStartsWithNoCase(relPath, L"assets\\")) return false;

    size_t pos = 0;
    while (pos <= relPath.size()) {
        size_t slash = relPath.find(L'\\', pos);
        std::wstring part = slash == std::wstring::npos
            ? relPath.substr(pos)
            : relPath.substr(pos, slash - pos);
        if (part.empty() || part == L"." || part == L"..") return false;
        if (slash == std::wstring::npos) break;
        pos = slash + 1;
    }

    // OWN-79: pure extension allow-list (WideStringUtils).
    if (!WidePathHasExtensionNoCase(relPath, {
            L".png", L".jpg", L".jpeg", L".webp", L".gif", L".bmp", L".avif"
        })) {
        return false;
    }

    normalized = relPath;
    return true;
}

// Stale-target guard used by preview edit/save protocol.
inline bool DashboardPreviewRenderTokenMatches(
    const std::wstring& pendingToken,
    const std::wstring& messageToken)
{
    return !pendingToken.empty()
        && !messageToken.empty()
        && pendingToken == messageToken;
}
