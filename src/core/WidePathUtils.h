#pragma once

// Wide string path manipulation and file extension helpers.
// Pure, no Win32 / HWND dependencies.

#include "core/WideCaseOps.h"
#include "core/WideCompareOps.h"

#include <format>
#include <initializer_list>
#include <string>

// Strip trailing '/' characters (URL-ish). Empty stays empty.
inline std::wstring WideTrimTrailingSlashes(std::wstring value) {
    while (!value.empty() && value.back() == L'/') value.pop_back();
    return value;
}

// File name from path (both separators). Empty -> empty.
inline std::wstring WideFileNameFromPath(const std::wstring& path) {
    if (path.empty()) return L"";
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return path;
    return path.substr(slash + 1);
}

// Parent directory (both separators). Drive root "C:\a" -> "C:\"; alone -> "".
inline std::wstring WideParentDirFromPath(const std::wstring& path) {
    if (path.empty()) return L"";
    size_t end = path.size();
    while (end > 1 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) {
        if (end == 3 && path[1] == L':') break;
        --end;
    }
    std::wstring trimmed = path.substr(0, end);
    size_t slash = trimmed.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    if (slash == 0) return trimmed.substr(0, 1);
    if (slash == 2 && trimmed.size() >= 2 && trimmed[1] == L':') {
        return trimmed.substr(0, 3);
    }
    return trimmed.substr(0, slash);
}

// Module-path -> exe directory.
inline std::wstring WideExeDirFromModulePath(const std::wstring& modulePath) {
    return WideParentDirFromPath(modulePath);
}

inline std::wstring WideExeDirFromModulePath(const wchar_t* modulePath) {
    return WideParentDirFromPath(modulePath ? modulePath : L"");
}

// Join with '\\'. Empty side returns the other.
inline std::wstring WideJoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    if (left.back() == L'\\' || left.back() == L'/') return left + right;
    return left + L"\\" + right;
}

// Join with '/'. Backslash trailing on left is normalized to '/'.
inline std::wstring WideJoinPathForwardSlash(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    if (left.back() == L'/' || left.back() == L'\\') {
        std::wstring out = left;
        if (out.back() == L'\\') out.back() = L'/';
        return out + right;
    }
    return left + L"/" + right;
}

// True when path looks drive-absolute ("C:\..." / "c:/...").
inline bool WideIsDriveAbsolutePath(const std::wstring& path) {
    return path.size() >= 2
        && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))
        && path[1] == L':';
}

// Relative path check (no drive root, no leading slash).
inline bool WideIsRelativePath(const std::wstring& path) {
    if (path.empty()) return true;
    if (WideIsDriveAbsolutePath(path)) return false;
    if (path[0] == L'\\' || path[0] == L'/') return false;
    return true;
}

// Strip final extension from a leaf name. ".hidden" kept; "a.b.c" -> "a.b".
inline std::wstring WideStripFinalExtension(std::wstring name) {
    if (name.empty()) return name;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

// Replace every '\\' with '/'.
inline std::wstring WideToForwardSlashes(std::wstring path) {
    for (wchar_t& ch : path) {
        if (ch == L'\\') ch = L'/';
    }
    return path;
}

// Replace every '/' with '\\'.
inline std::wstring WideToBackSlashes(std::wstring path) {
    for (wchar_t& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
    return path;
}

// Insert suffix before final extension of a full path (or append).
inline std::wstring WidePathWithSuffix(const std::wstring& path, const std::wstring& suffix) {
    std::wstring result = path;
    size_t slash = result.find_last_of(L"\\/");
    size_t dot = result.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        result.erase(dot);
    }
    result += suffix;
    return result;
}

// Resolve relative path under root; absolute returned unchanged.
inline std::wstring WideResolvePathUnderRoot(
    const std::wstring& root,
    const std::wstring& relativeOrAbsolute)
{
    if (relativeOrAbsolute.empty()) return L"";
    if (!WideIsRelativePath(relativeOrAbsolute)) return relativeOrAbsolute;
    return WideJoinPath(root, relativeOrAbsolute);
}

// File extension including leading '.' (both separators). Empty if none.
inline std::wstring WideExtensionFromPath(const std::wstring& path) {
    std::wstring name = WideFileNameFromPath(path);
    if (name.empty()) return L"";
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    return name.substr(dot);
}

// True when path extension is a supported image type (case-insensitive).
inline bool WideIsAllowedImageExtension(const std::wstring& path) {
    const std::wstring ext = WideToLower(WideExtensionFromPath(path));
    return ext == L".jpg" || ext == L".jpeg" || ext == L".png" ||
        ext == L".gif" || ext == L".bmp" || ext == L".webp" ||
        ext == L".avif" || ext == L".tif" || ext == L".tiff";
}

// Normalize asset extension for embedded OCR assets. Empty/unknown -> ".png".
inline std::wstring WideNormalizeAssetExtension(const std::wstring& sourcePath) {
    std::wstring ext = WideToLower(WideExtensionFromPath(sourcePath));
    if (ext.empty()) return L".png";
    return WideIsAllowedImageExtension(sourcePath) ? ext : L".png";
}

// Ensure dir ends with a single '\\' (empty stays empty).
inline std::wstring WideEnsureTrailingBackslash(std::wstring dir) {
    if (dir.empty()) return dir;
    if (dir.back() != L'\\') dir += L'\\';
    return dir;
}

// True when fullPath is strictly under fullDir (prefix match).
inline bool WideIsPathStrictlyUnderDirectory(
    const std::wstring& fullPath,
    const std::wstring& fullDir)
{
    if (fullPath.empty() || fullDir.empty()) return false;
    std::wstring dir = WideEnsureTrailingBackslash(fullDir);
    return fullPath.size() > dir.size() && fullPath.rfind(dir, 0) == 0;
}

// Case-insensitive path-under check (lowercases both sides; no filesystem).
inline bool WideIsPathStrictlyUnderDirectoryNoCase(
    const std::wstring& path,
    const std::wstring& dir)
{
    return WideIsPathStrictlyUnderDirectory(WideToLower(path), WideToLower(dir));
}

// Ensure path uses only backslashes and is lowercased (compare key).
inline std::wstring WidePathCompareKey(std::wstring path) {
    return WideToLower(WideToBackSlashes(std::move(path)));
}

// True when path ends with one of the given extensions (case-insensitive).
inline bool WidePathHasExtensionNoCase(
    const std::wstring& path,
    std::initializer_list<const wchar_t*> exts)
{
    const std::wstring ext = WideToLower(WideExtensionFromPath(path));
    if (ext.empty()) return false;
    for (const wchar_t* allowed : exts) {
        if (allowed && WideEqualsNoCase(ext, allowed)) return true;
    }
    return false;
}

// Stable PDF page pause key: jobKey + "#page:" + pageIndex (empty if invalid).
inline std::wstring WidePdfPagePauseKey(const std::wstring& jobKey, int pageIndex) {
    if (jobKey.empty() || pageIndex <= 0) return L"";
    return std::format(L"{}#page:{}", jobKey, pageIndex);
}
