#pragma once

// OWN-74: pure wide-string helpers (no HWND / Win32 API).
// Shared foundation for JsonUtils, DashboardFileTypes, and product thin wrappers.
// Header-only so hermetic tests can include without linking extra TUs.

#include <cwctype>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <vector>

// Trim leading/trailing whitespace (iswspace) and leading BOM (U+FEFF).
// OWN-78: BOM strip matches historical document Normalizer Trim.
inline std::wstring WideTrim(std::wstring value) {
    size_t first = 0;
    while (first < value.size() &&
        (value[first] == 0xFEFF || iswspace(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && iswspace(value[last - 1])) --last;
    if (first == 0 && last == value.size()) return value;
    return value.substr(first, last - first);
}

// In-place / by-value lowercase via towlower.
inline std::wstring WideToLower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value;
}

// OWN-78: normalize label tokens — '-' and whitespace → '_', then lower.
// Matches historical DocumentOcrAlignment::NormalizeLabel.
inline std::wstring WideNormalizeLabelToken(std::wstring label) {
    for (wchar_t& ch : label) {
        if (ch == L'-' || iswspace(ch)) {
            ch = L'_';
        } else {
            ch = static_cast<wchar_t>(towlower(ch));
        }
    }
    return label;
}

// Case-insensitive equality.
inline bool WideEqualsNoCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (towlower(a[i]) != towlower(b[i])) return false;
    }
    return true;
}

// Case-sensitive equality (pure; replaces product wcscmp == 0).
inline bool WideEquals(const std::wstring& a, const std::wstring& b) {
    return a == b;
}

// Case-sensitive equality for C-string vs literal (pure).
inline bool WideEquals(const wchar_t* a, const wchar_t* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return wcscmp(a, b) == 0;
}

// OWN-102: C-string vs wstring case-sensitive.
inline bool WideEquals(const wchar_t* a, const std::wstring& b) {
    if (!a) return b.empty();
    return b == a;
}
inline bool WideEquals(const std::wstring& a, const wchar_t* b) {
    if (!b) return a.empty();
    return a == b;
}

// Case-insensitive prefix match.
inline bool WideStartsWithNoCase(const std::wstring& value, const std::wstring& prefix) {
    if (value.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (towlower(value[i]) != towlower(prefix[i])) return false;
    }
    return true;
}

// Case-insensitive substring search. Empty needle → true.
inline bool WideContainsNoCase(const std::wstring& value, const std::wstring& needle) {
    if (needle.empty()) return true;
    if (value.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= value.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (towlower(value[i + j]) != towlower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// True when value looks like http:// or https:// URL (case-insensitive scheme).
inline bool WideIsHttpUrl(const std::wstring& value) {
    return WideStartsWithNoCase(value, L"https://")
        || WideStartsWithNoCase(value, L"http://");
}

// Escape JSON string content (no surrounding quotes).
// Matches historical JsonUtils::EscapeJsonString (\\ \" \r \n \t only).
inline std::wstring WideEscapeJsonString(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\': result += L"\\\\"; break;
        case L'"':  result += L"\\\""; break;
        case L'\r': result += L"\\r"; break;
        case L'\n': result += L"\\n"; break;
        case L'\t': result += L"\\t"; break;
        default:    result += ch; break;
        }
    }
    return result;
}

// Unescape JSON string content (handles \/, \b, \f, \uXXXX).
// Matches historical JsonUtils::UnescapeJsonString invalid-\u fallback
// (emit the backslash only; leave following chars for the next iteration).
inline std::wstring WideUnescapeJsonString(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.size());

    auto hexValue = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == L'\\' && i + 1 < input.size()) {
            switch (input[i + 1]) {
            case L'"':  result += L'"';  ++i; break;
            case L'\\': result += L'\\'; ++i; break;
            case L'/':  result += L'/';  ++i; break;
            case L'n':  result += L'\n'; ++i; break;
            case L't':  result += L'\t'; ++i; break;
            case L'r':  result += L'\r'; ++i; break;
            case L'b':  result += L'\b'; ++i; break;
            case L'f':  result += L'\f'; ++i; break;
            case L'u':
                if (i + 5 < input.size()) {
                    int code = 0;
                    bool ok = true;
                    for (int j = 0; j < 4; ++j) {
                        int v = hexValue(input[i + 2 + j]);
                        if (v < 0) { ok = false; break; }
                        code = (code << 4) | v;
                    }
                    if (ok) {
                        result += static_cast<wchar_t>(code);
                        i += 5;
                        break;
                    }
                }
                result += input[i];
                break;
            default:
                result += input[i];
                break;
            }
        } else {
            result += input[i];
        }
    }
    return result;
}

// Strip trailing '/' characters (URL-ish). Empty stays empty.
inline std::wstring WideTrimTrailingSlashes(std::wstring value) {
    while (!value.empty() && value.back() == L'/') value.pop_back();
    return value;
}

// Normalize newlines to CRLF.
inline std::wstring WideNormalizeNewlines(std::wstring text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                out += L"\r\n";
                ++i;
            } else {
                out += L"\r\n";
            }
        } else if (text[i] == L'\n') {
            out += L"\r\n";
        } else {
            out += text[i];
        }
    }
    return out;
}

// File name from path (both separators). Empty → empty.
inline std::wstring WideFileNameFromPath(const std::wstring& path) {
    if (path.empty()) return L"";
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return path;
    return path.substr(slash + 1);
}

// Parent directory (both separators). Drive root "C:\a" → "C:\"; alone → "".
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

// Join with '\\'. Empty side returns the other.
// OWN-108: pure module-path → exe directory (caller still owns GetModuleFileNameW).
inline std::wstring WideExeDirFromModulePath(const std::wstring& modulePath)
{
    return WideParentDirFromPath(modulePath);
}

inline std::wstring WideExeDirFromModulePath(const wchar_t* modulePath)
{
    return WideParentDirFromPath(modulePath ? modulePath : L"");
}

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

// Strip final extension from a leaf name. ".hidden" kept; "a.b.c" → "a.b".
inline std::wstring WideStripFinalExtension(std::wstring name) {
    if (name.empty()) return name;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

// Find next markdown image marker "![...](" with alt length ≤ maxAltChars.
// True when text contains unresolved local OCR asset references.
inline bool WideContainsUnresolvedOcrAssetReference(const std::wstring& text) {
    const std::wstring lower = WideToLower(text);
    return lower.find(L"zencrop-asset://") != std::wstring::npos ||
        lower.find(L"http://127.0.0.1") != std::wstring::npos ||
        lower.find(L"http://localhost") != std::wstring::npos;
}

// Skip JSON whitespace from pos; returns new pos.
inline size_t WideSkipJsonWhitespace(const std::wstring& s, size_t pos) {
    while (pos < s.size() &&
           (s[pos] == L' ' || s[pos] == L'\t' || s[pos] == L'\r' || s[pos] == L'\n')) {
        ++pos;
    }
    return pos;
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

// Clamp integer into [lo, hi] (assumes lo <= hi).
inline int WideClampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

// Strip auth scheme prefix "Bearer " / "Token " (case-insensitive); return remainder trimmed.
// Empty input → empty. No scheme → trimmed original.
inline std::wstring WideStripAuthSchemePrefix(std::wstring token) {
    token = WideTrim(std::move(token));
    if (WideStartsWithNoCase(token, L"Bearer ")) {
        return WideTrim(token.substr(7));
    }
    if (WideStartsWithNoCase(token, L"Token ")) {
        return WideTrim(token.substr(6));
    }
    return token;
}

// Build "Authorization: bearer <token>" header value (historical SettingsDialog shape).
// Preserves existing "Bearer ..." form; rewrites "Token ..." → "bearer ..."; else prefixes.
inline std::wstring WideBuildBearerAuthorizationHeader(const std::wstring& token) {
    std::wstring trimmed = WideTrim(token);
    if (trimmed.empty()) return L"Authorization: bearer ";
    if (WideStartsWithNoCase(trimmed, L"bearer ")) {
        return L"Authorization: " + trimmed;
    }
    if (WideStartsWithNoCase(trimmed, L"token ")) {
        return L"Authorization: bearer " + WideTrim(trimmed.substr(6));
    }
    return L"Authorization: bearer " + trimmed;
}

// True when haystack starts with needle at pos (case-insensitive). pos past end → false unless empty needle.
inline bool WideStartsWithNoCaseAt(const std::wstring& text, size_t pos, const wchar_t* prefix) {
    if (!prefix) return false;
    if (*prefix == L'\0') return true;
    if (pos >= text.size()) return false;
    return WideStartsWithNoCase(text.c_str() + pos, prefix);
}

// Insert suffix before final extension of a full path (or append). "a/b/c.json"+".x" → "a/b/c.x".
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

// OWN-75: pure text/path helpers for History/ImageLinks/OcrUtils product thin-wrap.

// URL terminator chars used by asset/link scanners (union of ImageLinks + PreviewHost).
inline bool WideIsUrlTerminator(wchar_t ch) {
    return iswspace(ch) || ch == L')' || ch == L']' || ch == L'}' ||
        ch == L'"' || ch == L'\'' || ch == L'<' || ch == L'>' ||
        ch == L'`' || ch == L';' || ch == L',';
}

// File extension including leading '.' (both separators). Empty if none.
// "a/b/c.PNG" → ".PNG"; "noext" → ""; ".hidden" → ".hidden".
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

// Normalize asset extension for embedded OCR assets. Empty/unknown → ".png".
inline std::wstring WideNormalizeAssetExtension(const std::wstring& sourcePath) {
    std::wstring ext = WideToLower(WideExtensionFromPath(sourcePath));
    if (ext.empty()) return L".png";
    return WideIsAllowedImageExtension(sourcePath) ? ext : L".png";
}

// OWN-75: pure path-prefix helpers (operate on already-canonicalized paths).

// Ensure dir ends with a single '\\' (empty stays empty).
inline std::wstring WideEnsureTrailingBackslash(std::wstring dir) {
    if (dir.empty()) return dir;
    if (dir.back() != L'\\') dir += L'\\';
    return dir;
}

// True when fullPath is strictly under fullDir (prefix match).
// Callers must pass already-canonicalized (and usually lowercased) paths.
// fullDir may omit trailing '\\'; empty dir → false.
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
// Exts should include leading '.' (e.g. ".png").
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
    // OWN-127: pure page pause key (no std::to_wstring).
    wchar_t buf[48] = {};
    swprintf_s(buf, L"#page:%d", pageIndex);
    return jobKey + buf;
}

// True when status token is an actively-running batch status (case-insensitive).
inline bool WideIsRunningBatchStatusToken(const std::wstring& status) {
    const std::wstring lower = WideToLower(WideTrim(status));
    return lower == L"recognizing" || lower == L"writing";
}

// True when status token is terminal (completed/failed/canceled|cancelled).
inline bool WideIsTerminalBatchStatusToken(const std::wstring& status) {
    const std::wstring lower = WideToLower(WideTrim(status));
    return lower == L"completed" || lower == L"failed"
        || lower == L"canceled" || lower == L"cancelled";
}

// OWN-76: pure color / JSON / cycle helpers (no HWND / GDI).

// Hex digit value 0..15, or -1 if not hex.
inline int WideHexDigitValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return -1;
}

// Pack RGB as 0x00BBGGRR (matches Win32 COLORREF layout without windows.h).
inline unsigned int WidePackRgb(unsigned int r, unsigned int g, unsigned int b) {
    return (r & 0xFFu) | ((g & 0xFFu) << 8) | ((b & 0xFFu) << 16);
}

inline unsigned int WideUnpackR(unsigned int packed) { return packed & 0xFFu; }
inline unsigned int WideUnpackG(unsigned int packed) { return (packed >> 8) & 0xFFu; }
inline unsigned int WideUnpackB(unsigned int packed) { return (packed >> 16) & 0xFFu; }

// OWN-80: strict try-parse of "#RRGGBB" / "RRGGBB" (optional leading '#',
// leading/trailing whitespace). Case-insensitive. Writes packed 0x00BBGGRR.
inline bool WideTryParseColorHex(const std::wstring& raw, unsigned int& outPacked) {
    std::wstring hex = WideTrim(raw);
    if (!hex.empty() && hex[0] == L'#') hex.erase(hex.begin());
    if (hex.size() < 6) return false;
    // Only first 6 hex digits matter (matches historical ColorPicker parse).
    int h1 = WideHexDigitValue(hex[0]);
    int h2 = WideHexDigitValue(hex[1]);
    int h3 = WideHexDigitValue(hex[2]);
    int h4 = WideHexDigitValue(hex[3]);
    int h5 = WideHexDigitValue(hex[4]);
    int h6 = WideHexDigitValue(hex[5]);
    if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0 || h5 < 0 || h6 < 0) return false;
    const unsigned int r = static_cast<unsigned int>(h1 * 16 + h2);
    const unsigned int g = static_cast<unsigned int>(h3 * 16 + h4);
    const unsigned int b = static_cast<unsigned int>(h5 * 16 + h6);
    outPacked = WidePackRgb(r, g, b);
    return true;
}

// Parse "#RRGGBB" (case-insensitive; optional '#'; trim). Invalid → fallbackPacked.
inline unsigned int WideParseColorHex(
    const std::wstring& hex,
    unsigned int fallbackPacked = WidePackRgb(255, 0, 0))
{
    unsigned int packed = 0;
    if (!WideTryParseColorHex(hex, packed)) return fallbackPacked;
    return packed;
}

// Format packed COLORREF-layout RGB as "#RRGGBB" (uppercase).
inline std::wstring WideColorToHex(unsigned int packed) {
    wchar_t buf[8] = {};
    swprintf_s(buf, L"#%02X%02X%02X",
        static_cast<unsigned>(WideUnpackR(packed)),
        static_cast<unsigned>(WideUnpackG(packed)),
        static_cast<unsigned>(WideUnpackB(packed)));
    return buf;
}

// OWN-109/111/112: pure date/time/UI/pad formatters (int parts only; no SYSTEMTIME).
inline std::wstring WideFormatPad2(int value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%02d", value);
    return buf;
}

inline std::wstring WideFormatPad4(int value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%04d", value);
    return buf;
}

inline std::wstring WideFormatPointLabel(long x, long y)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"(%ld, %ld)", x, y);
    return buf;
}

inline std::wstring WideColorToHexLower(unsigned int packed)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"#%02x%02x%02x",
        WideUnpackR(packed), WideUnpackG(packed), WideUnpackB(packed));
    return buf;
}

inline std::wstring WideFormatDateParts(int year, int month, int day)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%04d-%02d-%02d", year, month, day);
    return buf;
}

inline std::wstring WideFormatPercentLabel(int value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%d%%", value);
    return buf;
}

inline std::wstring WideFormatPxLabel(int value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%d px", value);
    return buf;
}

inline std::wstring WideFormatIntLabel(int value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%d", value);
    return buf;
}

inline std::wstring WideFormatCompactStamp(
    int year, int month, int day,
    int hour, int minute, int second)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L".bad.%04d%02d%02d-%02d%02d%02d",
        year, month, day, hour, minute, second);
    return buf;
}

inline std::wstring WideFormatDateTimeParts(
    int year, int month, int day,
    int hour, int minute, int second)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
        year, month, day, hour, minute, second);
    return buf;
}

inline std::wstring WideFormatOcrCropFileName(
    int hour, int minute, int second, int milliseconds)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"ocr_crop_%02d%02d%02d_%03d.png",
        hour, minute, second, milliseconds);
    return buf;
}

// OWN-113: pure UI/page/hex/elapsed/temp-name formatters (int/string parts only).
inline std::wstring WideFormatPad3(int value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%03d", value);
    return buf;
}

inline std::wstring WideFormatMmSs(unsigned minutes, unsigned seconds)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%02u:%02u", minutes, seconds);
    return buf;
}

inline std::wstring WideFormatOcrElapsedLabel(unsigned minutes, unsigned seconds)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"OCR %02u:%02u", minutes, seconds);
    return buf;
}

inline std::wstring WideFormatElapsedMs(unsigned long elapsedMs)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%lums", elapsedMs);
    return buf;
}

inline std::wstring WideFormatSeconds1(double seconds)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.1fs", seconds);
    return buf;
}

inline std::wstring WideFormatPageIndexName(int pageIndex)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"page_%04d", pageIndex);
    return buf;
}

inline std::wstring WideFormatImageIndexName(int index)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"image_%03d", index);
    return buf;
}

inline std::wstring WideFormatPageAssetStem(int pageIndex, int assetIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"page_%04d_img_%03d", pageIndex, assetIndex);
    return buf;
}

inline std::wstring WideFormatPageAssetPrefix(int pageIndex)
{
    wchar_t buf[40] = {};
    swprintf_s(buf, L"page_%04d_img_", pageIndex);
    return buf;
}

inline std::wstring WideFormatDupSuffix02(int suffix)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"_%02d", suffix);
    return buf;
}

inline std::wstring WideFormatHexByte02(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"0x%02X", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatHexU32(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"0x%08X", value);
    return buf;
}

inline std::wstring WideFormatUrlPercentByte(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%%%02X", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatUPlusCodepoint(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"U+%04X", value);
    return buf;
}

inline std::wstring WideFormatHash016(unsigned long long value)
{
    wchar_t buf[24] = {};
    swprintf_s(buf, L"%016llx", value);
    return buf;
}

inline std::wstring WideFormatMagnifierScale(double scale)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%.1fx", scale);
    return buf;
}

inline std::wstring WideFormatYmdCompact(int year, int month, int day)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%04d%02d%02d", year, month, day);
    return buf;
}

inline std::wstring WideFormatHmsCompact(int hour, int minute, int second)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%02d%02d%02d", hour, minute, second);
    return buf;
}

inline std::wstring WideFormatMs3(int milliseconds)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%03d", milliseconds);
    return buf;
}

// ZenCrop_clip_YYYYMMDD_HHMMSS_mmm_pid_attempt.ext (parts pure; PID/attempt product args).
inline std::wstring WideFormatClipTempName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds,
    unsigned long pid, int attempt, const wchar_t* ext)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"ZenCrop_clip_%04d%02d%02d_%02d%02d%02d_%03d_%lu_%02d%s",
        year, month, day, hour, minute, second, milliseconds,
        pid, attempt, ext ? ext : L"");
    return buf;
}

// ZenCrop_codec_YYYYMMDD_HHMMSS_mmm_pid_counter_attempt.suffix
inline std::wstring WideFormatCodecTempName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds,
    unsigned long pid, unsigned counter, int attempt, const wchar_t* suffix)
{
    wchar_t buf[180] = {};
    swprintf_s(buf, L"ZenCrop_codec_%04d%02d%02d_%02d%02d%02d_%03d_%lu_%u_%02d%s",
        year, month, day, hour, minute, second, milliseconds,
        pid, counter, attempt, suffix ? suffix : L"");
    return buf;
}

// ocr_drop_YYYYMMDD_HHMMSS_mmm.png
inline std::wstring WideFormatOcrDropFileName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"ocr_drop_%04d%02d%02d_%02d%02d%02d_%03d.png",
        year, month, day, hour, minute, second, milliseconds);
    return buf;
}

inline std::wstring WideFormatMegabytes1(double megabytes)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.1f MB", megabytes);
    return buf;
}

inline std::wstring WideFormatMegabytes0(double megabytes)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.0f MB", megabytes);
    return buf;
}

// OWN-114: pure hex/float/CSV/zoom/temp-name formatters (int/float/string parts only).
inline std::wstring WideFormatHexLower02(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%02x", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatHexUpper02(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%02X", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatHexLower(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%x", value);
    return buf;
}

inline std::wstring WideFormatHexUpper(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%X", value);
    return buf;
}

inline std::wstring WideFormatFloat1(double value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.1f", value);
    return buf;
}

inline std::wstring WideFormatFloat2(double value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.2f", value);
    return buf;
}

inline std::wstring WideFormatFloat6(double value)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"%.6f", value);
    return buf;
}

inline std::wstring WideFormatZoomPercent0(double zoomFractionTimes100)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.0f%%", zoomFractionTimes100);
    return buf;
}

inline std::wstring WideFormatElapsedParenSeconds1(double seconds)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"  (%.1fs)", seconds);
    return buf;
}

inline std::wstring WideFormatCsvInt5(int a, int b, int c, int d, int e)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%d,%d,%d,%d,%d", a, b, c, d, e);
    return buf;
}

inline std::wstring WideFormatUnsigned(unsigned value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%u", value);
    return buf;
}

inline std::wstring WideFormatHotkeyJson(
    const wchar_t* winLit, const wchar_t* ctrlLit,
    const wchar_t* shiftLit, const wchar_t* altLit, int key)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"{\"win\": %s, \"ctrl\": %s, \"shift\": %s, \"alt\": %s, \"key\": %d}",
        winLit ? winLit : L"false",
        ctrlLit ? ctrlLit : L"false",
        shiftLit ? shiftLit : L"false",
        altLit ? altLit : L"false",
        key);
    return buf;
}

// YYYY-MM-DD HH:MM (SourceRail history stamp; no seconds).
inline std::wstring WideFormatDateTimeMinuteParts(
    unsigned year, unsigned month, unsigned day,
    unsigned hour, unsigned minute)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u",
        year, month, day, hour, minute);
    return buf;
}

// tiff_pNNNN page stem.
inline std::wstring WideFormatTiffPagePrefix(unsigned pageOneBased)
{
    wchar_t buf[24] = {};
    swprintf_s(buf, L"tiff_p%04u", pageOneBased);
    return buf;
}

// zencrop_pdf_preview_pid_tick
inline std::wstring WideFormatPdfPreviewDirName(unsigned long pid, unsigned long long tick)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"zencrop_pdf_preview_%lu_%llu", pid, tick);
    return buf;
}

// %.1f MP total, %.1f MP max/page
inline std::wstring WideFormatMpEstimate(double totalMp, double maxPerPageMp)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"%.1f MP total, %.1f MP max/page", totalMp, maxPerPageMp);
    return buf;
}

// ocr_virtual_HHMMSS_mmm_seq (no date; product may prefix date)
inline std::wstring WideFormatOcrVirtualStem(
    int hour, int minute, int second, int milliseconds, unsigned seq)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"ocr_virtual_%02d%02d%02d_%03d_%03u",
        hour, minute, second, milliseconds, seq);
    return buf;
}

// name_HHMMSS_mmm_seq.png (prefix product-owned)
inline std::wstring WideFormatTimedSeqPng(
    const wchar_t* prefix,
    int hour, int minute, int second, int milliseconds, unsigned seq)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"%s_%02d%02d%02d_%03d_%03u.png",
        prefix ? prefix : L"item",
        hour, minute, second, milliseconds, seq);
    return buf;
}

// pdf render temp: pdf_pid_tick_counter.pdf
inline std::wstring WideFormatPdfTempName(
    unsigned long pid, unsigned long long tick, unsigned counter)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"pdf_%lu_%llu_%03u.pdf", pid, tick, counter);
    return buf;
}

// True when haystack contains needle (case-sensitive; null-safe).
inline bool WideContains(const wchar_t* haystack, const wchar_t* needle)
{
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    return wcsstr(haystack, needle) != nullptr;
}

inline bool WideContains(const std::wstring& haystack, const wchar_t* needle)
{
    return WideContains(haystack.c_str(), needle);
}

// ISO-8601 UTC timestamp YYYY-MM-DDTHH:MM:SS.mmmZ (int parts only; no SYSTEMTIME).
inline std::wstring WideFormatIsoUtcTimestamp(
    unsigned year, unsigned month, unsigned day,
    unsigned hour, unsigned minute, unsigned second, unsigned milliseconds)
{
    wchar_t buf[40] = {};
    swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        year, month, day, hour, minute, second, milliseconds);
    return buf;
}

// SettingsDialog-style HTTP status / port labels (compose pure ints + fixed copy).
inline std::wstring WideFormatHttpStatusRejected(int statusCode)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"Endpoint is reachable, but the token was rejected. (HTTP %d)", statusCode);
    return buf;
}

inline std::wstring WideFormatServerRunningOnPort(int port)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"Server is already running on port %d.", port);
    return buf;
}

inline std::wstring WideFormatServerStartedOnPort(int port)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"Server started successfully on port %d.\nIt is now ready for OCR.", port);
    return buf;
}

inline std::wstring WideFormatServerStartFailedOnPort(int port)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"Failed to start llama-server on port %d.\n\n", port);
    return buf;
}

// Debug PATH_TABLE load count (product debug logs).
inline std::wstring WideFormatPathTableLoadedCount(int count)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"[ToolbarIconRenderer] Loaded PATH_TABLE.tsv: %d entries\n", count);
    return buf;
}

inline std::wstring WideFormatMissingCodepoint(unsigned codepoint)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"[ToolbarIconRenderer] Missing codepoint 0x%04X\n", codepoint);
    return buf;
}

// Settings general JSON section (language + showTitlebar bool literal already pure).
inline std::wstring WideFormatGeneralSettingsJson(
    const wchar_t* langStr, const wchar_t* showTitlebarLit)
{
    wchar_t buf[256] = {};
    swprintf_s(buf, L"  \"general\": {\n    \"language\": \"%s\",\n    \"showTitlebar\": %s\n  }",
        langStr ? langStr : L"",
        showTitlebarLit ? showTitlebarLit : L"false");
    return buf;
}

// Overlay settings JSON section.
inline std::wstring WideFormatOverlaySettingsJson(
    const wchar_t* colorHex, int thickness, const wchar_t* cropOnTopLit)
{
    wchar_t buf[256] = {};
    swprintf_s(buf, L"  \"overlay\": {\n    \"color\": \"%s\",\n    \"thickness\": %d,\n    \"cropOnTop\": %s\n  }",
        colorHex ? colorHex : L"#000000",
        thickness,
        cropOnTopLit ? cropOnTopLit : L"false");
    return buf;
}

// Always-on-top settings JSON section.
inline std::wstring WideFormatAotSettingsJson(
    const wchar_t* showBorderLit, const wchar_t* customColorLit,
    const wchar_t* colorHex, int opacity, int thickness,
    const wchar_t* roundedCornersLit, int inset)
{
    wchar_t buf[640] = {};
    swprintf_s(buf,
        L"  \"alwaysOnTop\": {\n    \"showBorder\": %s,\n    \"customColor\": %s,\n"
        L"    \"color\": \"%s\",\n    \"opacity\": %d,\n    \"thickness\": %d,\n"
        L"    \"roundedCorners\": %s,\n    \"inset\": %d\n  }",
        showBorderLit ? showBorderLit : L"false",
        customColorLit ? customColorLit : L"false",
        colorHex ? colorHex : L"#000000",
        opacity,
        thickness,
        roundedCornersLit ? roundedCornersLit : L"false",
        inset);
    return buf;
}

// Official async jobs endpoint reachable message.
inline std::wstring WideFormatHttpJobsEndpointReachable(int statusCode)
{
    wchar_t buf[320] = {};
    swprintf_s(buf,
        L"Official async jobs endpoint is reachable. (HTTP %d)\n\n"
        L"This test does not submit an OCR job; actual OCR will upload an image and poll by jobId.",
        statusCode);
    return buf;
}

// Crop label with product i18n format (expects four %d: left, top, width, height).
inline std::wstring WideFormatCropLabel(
    const wchar_t* format, int left, int top, int width, int height)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, 128, format ? format : L"%d, %d  %d x %d", left, top, width, height);
    return buf;
}

// OWN-115: pure JSON field line builders (indent 4 spaces; no trailing comma).
inline std::wstring WideJsonFieldString(const wchar_t* key, const wchar_t* escapedValue)
{
    // key and value already escaped by caller where needed.
    std::wstring out = L"    \"";
    out += key ? key : L"";
    out += L"\": \"";
    out += escapedValue ? escapedValue : L"";
    out += L"\"";
    return out;
}

inline std::wstring WideJsonFieldString(const wchar_t* key, const std::wstring& escapedValue)
{
    return WideJsonFieldString(key, escapedValue.c_str());
}

inline std::wstring WideJsonFieldInt(const wchar_t* key, int value)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"    \"%s\": %d", key ? key : L"", value);
    return buf;
}

inline std::wstring WideJsonFieldUnsigned(const wchar_t* key, unsigned value)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"    \"%s\": %u", key ? key : L"", value);
    return buf;
}

inline std::wstring WideJsonFieldBool(const wchar_t* key, bool value)
{
    // Inline true/false — WideJsonBoolLiteral is defined later in this header.
    std::wstring out = L"    \"";
    out += key ? key : L"";
    out += L"\": ";
    out += value ? L"true" : L"false";
    return out;
}

inline std::wstring WideJsonFieldBoolLit(const wchar_t* key, const wchar_t* boolLit)
{
    std::wstring out = L"    \"";
    out += key ? key : L"";
    out += L"\": ";
    out += boolLit ? boolLit : L"false";
    return out;
}

// Join JSON field lines with ",\n" and wrap in section header.
inline std::wstring WideJsonObjectSection(
    const wchar_t* sectionName,
    const std::wstring* fields,
    size_t fieldCount)
{
    std::wstring out = L"  \"";
    out += sectionName ? sectionName : L"";
    out += L"\": {\n";
    for (size_t i = 0; i < fieldCount; ++i) {
        out += fields[i];
        if (i + 1 < fieldCount) out += L",\n";
        else out += L"\n";
    }
    out += L"  }";
    return out;
}

// LayoutEngine debug line: family + threshold profile + two floats.
inline std::wstring WideFormatLayoutEngineDebug(
    const wchar_t* family, const wchar_t* thresholdProfile,
    double textThresh, double tableThresh)
{
    wchar_t buf[320] = {};
    swprintf_s(buf,
        L"[LayoutEngine] family=%s thresholdProfile=%s text=%.2f table=%.2f\n",
        family ? family : L"",
        thresholdProfile ? thresholdProfile : L"",
        textThresh, tableThresh);
    return buf;
}

// Fixed string JSON field with hardcoded provider value (ppocrv6Provider).
inline std::wstring WideJsonFieldStringLiteral(const wchar_t* key, const wchar_t* literal)
{
    return WideJsonFieldString(key, literal);
}

// OWN-122: pure count-prefix / queue / page-meta / runtime-key wide formatters.
// "prefixN" e.g. "OCR x5", "Cloud 3", "Q12", "PQ5", "P3"
inline std::wstring WideFormatCountPrefix(const wchar_t* prefix, int count)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"%s%d", prefix ? prefix : L"", count);
    return buf;
}

// "a/b" with unsigned sizes (page progress, etc.)
inline std::wstring WideFormatSlashCountU(unsigned a, unsigned b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%u/%u", a, b);
    return buf;
}

// "Nx" magnification integer label (e.g. 2x, 3x)
inline std::wstring WideFormatTimesInt(int times)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%dx", times);
    return buf;
}

// "image:runtime:<index>:" prefix (caller appends path)
inline std::wstring WideFormatImageRuntimeKeyPrefix(int index)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"image:runtime:%d:", index);
    return buf;
}

// "pdf:runtime:<index>"
inline std::wstring WideFormatPdfRuntimeKey(int index)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"pdf:runtime:%d", index);
    return buf;
}

// ":duplicate:<ordinal>"
inline std::wstring WideFormatDuplicateSuffix(int ordinal)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L":duplicate:%d", ordinal);
    return buf;
}

// "P<N> · <elapsed>" meta suffix
inline std::wstring WideFormatPageMetaSuffix(int pageIndex, const std::wstring& elapsed)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"P%d · %s", pageIndex, elapsed.c_str());
    return buf;
}

// OWN-123: pure paren-slash / thumbnail-gen / hash-page / unsigned-label wide formatters.
// "(a/b)" progress fragment
inline std::wstring WideFormatParenSlashCount(int a, int b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"(%d/%d)", a, b);
    return buf;
}

// "thumbnail.g<generation>."
inline std::wstring WideFormatThumbnailGenPrefix(unsigned long long generation)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"thumbnail.g%llu.", generation);
    return buf;
}

// "<path>#p<pageIndex>"
inline std::wstring WideFormatPathHashPage(const std::wstring& path, int pageIndex)
{
    wchar_t pageBuf[32] = {};
    swprintf_s(pageBuf, L"#p%d", pageIndex);
    return path + pageBuf;
}

// unsigned long long as decimal string
inline std::wstring WideFormatUll(unsigned long long value)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"%llu", value);
    return buf;
}

// " / Page N" suffix
inline std::wstring WideFormatPageSlashLabel(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L" / Page %d", pageIndex);
    return buf;
}

// OWN-124: pure wide format helpers (no HWND; dual-write only).

// "page_<1-based>:block_<order>"
inline std::wstring WideFormatPageBlockId(int pageOneBased, int order)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"page_%d:block_%d", pageOneBased, order);
    return buf;
}

// "page_1:bbox_<n>"
inline std::wstring WideFormatPageBboxId(int n)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"page_1:bbox_%d", n);
    return buf;
}

// "page_1:layout_<n>:asset"
inline std::wstring WideFormatPageLayoutAssetId(int regionOneBased)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"page_1:layout_%d:asset", regionOneBased);
    return buf;
}

// "left,top - right,bottom"
inline std::wstring WideFormatBboxLtrb(long left, long top, long right, long bottom)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"%ld,%ld - %ld,%ld", left, top, right, bottom);
    return buf;
}

// ".tmp.<pid>.<tick>"
inline std::wstring WideFormatTmpPidTick(unsigned long pid, unsigned long long tick)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L".tmp.%lu.%llu", pid, tick);
    return buf;
}

// "#N" history/index label (1-based display)
inline std::wstring WideFormatHashIndex(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"#%d", indexOneBased);
    return buf;
}

// "  \"key\": N,\r\n" (2-space indent JSON int field line)
inline std::wstring WideJsonFieldInt2(const wchar_t* key, int value)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"  \"%s\": %d,\r\n", key ? key : L"", value);
    return buf;
}

// "  \"key\": N,\r\n" for size_t-ish unsigned long long
inline std::wstring WideJsonFieldUll2(const wchar_t* key, unsigned long long value)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"  \"%s\": %llu,\r\n", key ? key : L"", value);
    return buf;
}

// "\"key\":N" compact JSON int field (no spaces/indent)
inline std::wstring WideJsonFieldIntCompact(const wchar_t* key, int value)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"\"%s\":%d", key ? key : L"", value);
    return buf;
}

// "\"key\":N" compact for unsigned long long
inline std::wstring WideJsonFieldUllCompact(const wchar_t* key, unsigned long long value)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"\"%s\":%llu", key ? key : L"", value);
    return buf;
}

// OWN-125: pure source-rail / page-key / size-key / middot-slash wide formatters.

// ":page:<n>" stable source key suffix
inline std::wstring WideFormatColonPageKey(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L":page:%d", pageIndex);
    return buf;
}

// "\nWxH" thumbnail cache key suffix (path + "\n" + WxH)
inline std::wstring WideFormatThumbSizeSuffix(int width, int height)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"\n%dx%d", width, height);
    return buf;
}

// " · a/b" middot slash progress fragment
inline std::wstring WideFormatMiddotSlashCount(int a, int b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L" \x00b7 %d/%d", a, b);
    return buf;
}

// "P<n> · <elapsed>" live page meta (elapsed may contain middle-dot text)
inline std::wstring WideFormatPageMetaLive(int pageOneBased, const std::wstring& elapsed)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"P%d \x00b7 ", pageOneBased);
    return std::wstring(buf) + elapsed;
}

// "Image N" / "PDF N" / "Capture N" titled index labels
inline std::wstring WideFormatImageTitle(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"Image %d", indexOneBased);
    return buf;
}

inline std::wstring WideFormatPdfTitle(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"PDF %d", indexOneBased);
    return buf;
}

inline std::wstring WideFormatCaptureTitle(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"Capture %d", indexOneBased);
    return buf;
}

// "a/b | " status prefix used by source-rail paint
inline std::wstring WideFormatSlashCountBar(int a, int b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%d/%d | ", a, b);
    return buf;
}

// "/" + count fragment (filtered header "visible/total")
inline std::wstring WideFormatSlashTotal(int total)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"/%d", total);
    return buf;
}

// "p.N" page-dot label (preview strip)
inline std::wstring WideFormatPageDotLabel(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"p.%d", pageIndex);
    return buf;
}

// "PDF p.N" selection page label
inline std::wstring WideFormatPdfPageDotLabel(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"PDF p.%d", pageIndex);
    return buf;
}

// "prefixA/B" e.g. "D3/10" drop progress fingerprint
inline std::wstring WideFormatPrefixSlashCount(const wchar_t* prefix, int a, int b)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%s%d/%d", prefix ? prefix : L"", a, b);
    return buf;
}

// "EX:<id>:" external progress fingerprint fragment
inline std::wstring WideFormatExProgressPrefix(int progressId)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"EX:%d:", progressId);
    return buf;
}

// "OCR:<key>:<page>|" current OCR fingerprint fragment (key may be empty)
inline std::wstring WideFormatOcrFpSuffix(const std::wstring& stableKey, int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L":%d|", pageIndex);
    return L"OCR:" + stableKey + buf;
}

// " (N)" parenthesized int (Win32 error suffix, counts)
inline std::wstring WideFormatParenInt(int value)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L" (%d)", value);
    return buf;
}

// ".<kind>.<pid>.<tick>" temp/candidate/backup path fragment
inline std::wstring WideFormatDotKindPidTick(
    const wchar_t* kind, unsigned long pid, unsigned long long tick)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L".%s.%lu.%llu", kind ? kind : L"", pid, tick);
    return buf;
}

// ".candidate.<pid>.<tick>"
inline std::wstring WideFormatCandidatePidTick(unsigned long pid, unsigned long long tick)
{
    return WideFormatDotKindPidTick(L"candidate", pid, tick);
}

// ".backup.<pid>.<tick>"
inline std::wstring WideFormatBackupPidTick(unsigned long pid, unsigned long long tick)
{
    return WideFormatDotKindPidTick(L"backup", pid, tick);
}

// OWN-126: pure provider-asset / page-warn / offset-error wide formatters.

// "page_<n>:provider_asset_<order>"
inline std::wstring WideFormatPageProviderAssetId(int pageNumber, int localOrder)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"page_%d:provider_asset_%d", pageNumber, localOrder);
    return buf;
}

// "page_<n>:<kind>_<order>" generic page-kind-order id
inline std::wstring WideFormatPageKindOrderId(
    int pageNumber, const wchar_t* kind, int order)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"page_%d:%s_%d", pageNumber, kind ? kind : L"", order);
    return buf;
}

// "zencrop-asset://provider/page_<n>/asset_<order>"
inline std::wstring WideFormatProviderAssetUri(int pageNumber, int localOrder)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"zencrop-asset://provider/page_%d/asset_%d", pageNumber, localOrder);
    return buf;
}

// "Page <n>: " warning prefix
inline std::wstring WideFormatPageWarnPrefix(int pageNumber)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"Page %d: ", pageNumber);
    return buf;
}

// " at UTF-16 offset <n>." JSONL error suffix
inline std::wstring WideFormatUtf16OffsetSuffix(unsigned long long offset)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L" at UTF-16 offset %llu.", offset);
    return buf;
}

// "<prefix><n>." int-suffixed sentence end (error tails)
inline std::wstring WideFormatIntDotSuffix(const wchar_t* prefix, int value)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"%s%d.", prefix ? prefix : L"", value);
    return buf;
}

// "<prefix><n>: <detail>" int-midfix with trailing detail
inline std::wstring WideFormatIntColonDetail(
    const wchar_t* prefix, int value, const std::wstring& detail)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%s%d: ", prefix ? prefix : L"", value);
    return std::wstring(buf) + detail;
}

// "page_<n>" bare page id fragment
inline std::wstring WideFormatPageId(int pageNumber)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"page_%d", pageNumber);
    return buf;
}

// "asset_<n>" bare asset id fragment
inline std::wstring WideFormatAssetId(int order)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"asset_%d", order);
    return buf;
}

// "provider_asset_<n>" provider local asset fragment
inline std::wstring WideFormatProviderAssetLocal(int order)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"provider_asset_%d", order);
    return buf;
}

// ".<pid>.<tick>.<counter><suffix>" temp sibling path tail
inline std::wstring WideFormatPidTickCounterSuffix(
    unsigned long pid, unsigned long long tick, unsigned counter, const wchar_t* suffix)
{
    wchar_t buf[160] = {};
    swprintf_s(
        buf,
        L".%lu.%llu.%u%s",
        pid,
        tick,
        counter,
        suffix ? suffix : L".tmp");
    return buf;
}

// OWN-127: pure block-id / page-prefix / ann / function-key / http / json-index formatters.

// "block_<n>" bare block id fragment
inline std::wstring WideFormatBlockId(int order)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"block_%d", order);
    return buf;
}

// "page_<n>:" page prefix for id composition
inline std::wstring WideFormatPagePrefix(int pageNumber)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"page_%d:", pageNumber);
    return buf;
}

// "ann_<n>" annotation id
inline std::wstring WideFormatAnnIdPlain(unsigned long long id)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"ann_%llu", id);
    return buf;
}

// "legacy_<n>" legacy annotation id
inline std::wstring WideFormatLegacyId(int index)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"legacy_%d", index);
    return buf;
}

// "F<n>" function key label (F1..)
inline std::wstring WideFormatFunctionKey(int n)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"F%d", n);
    return buf;
}

// "HTTP <n>" http status fragment
inline std::wstring WideFormatHttpStatus(int statusCode)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"HTTP %d", statusCode);
    return buf;
}

// "{\"index\":N" compact json index field open
inline std::wstring WideFormatJsonIndexOpen(int index)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"{\"index\":%d", index);
    return buf;
}

// "x,y" point pair for migration serialization
inline std::wstring WideFormatPointXy(int x, int y)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%d,%d", x, y);
    return buf;
}

// "Failed to create job directory (N)."
inline std::wstring WideFormatJobDirError(unsigned long err)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"Failed to create job directory (%lu).", err);
    return buf;
}

// "Endpoint returned HTTP N" dialog message prefix
inline std::wstring WideFormatEndpointHttp(int statusCode)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"Endpoint returned HTTP %d", statusCode);
    return buf;
}

// "group_<n>" region group id
inline std::wstring WideFormatGroupId(int order)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"group_%d", order);
    return buf;
}

// "N group(s) failed." failed groups suffix
inline std::wstring WideFormatGroupsFailedSuffix(int failedGroups)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%d group(s) failed.", failedGroups);
    return buf;
}

// "📌 #<n>  |  " history paint header prefix
inline std::wstring WideFormatHistoryPinHeader(int oneBasedIndex)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"\U0001F4CC #%d  |  ", oneBasedIndex);
    return buf;
}

inline int WideCycleIntInclusive(int value, int minValue, int maxValue, int delta = 1) {
    if (minValue > maxValue) return value;
    if (value < minValue) value = minValue;
    if (value > maxValue) value = maxValue;
    const int span = maxValue - minValue + 1;
    int next = value + delta;
    // Normalize into range with positive modulo.
    int offset = (next - minValue) % span;
    if (offset < 0) offset += span;
    return minValue + offset;
}

// Cycle line style 1..5 (historical screenshot line styles).
inline int WideCycleLineStyle(int current) {
    return WideCycleIntInclusive(current, 1, 5, 1);
}

// Cycle marker blend / mosaic mode 0..1.
inline int WideCycleBinaryMode(int current) {
    return WideCycleIntInclusive(current, 0, 1, 1);
}

// Cycle serial type 0..4.
inline int WideCycleSerialType(int current) {
    return WideCycleIntInclusive(current, 0, 4, 1);
}

// Adjust serial counter with floor 1.
inline int WideAdjustSerialCounter(int current, int delta) {
    const int next = current + delta;
    return next < 1 ? 1 : next;
}

// True when JSON text has a top-level-ish key token "\"key\"" followed by ':'.
// Pure structural scan (no unescape of key content).
inline bool WideHasJsonKey(const std::wstring& json, const std::wstring& key) {
    const std::wstring search = L"\"" + key + L"\"";
    size_t pos = json.find(search);
    if (pos == std::wstring::npos) return false;
    pos = json.find(L':', pos + search.size());
    return pos != std::wstring::npos;
}

// Skip a JSON string starting at pos (must point at opening '"').
// Returns position after closing quote, or pos if not a string.
inline size_t WideSkipJsonString(const std::wstring& json, size_t pos) {
    if (pos >= json.size() || json[pos] != L'"') return pos;
    pos++;
    while (pos < json.size()) {
        if (json[pos] == L'\\' && pos + 1 < json.size()) {
            pos += 2;
            continue;
        }
        if (json[pos] == L'"') return pos + 1;
        pos++;
    }
    return pos;
}

// Skip JSON whitespace including BOM (historical SkipJsonWhitespace).
inline size_t WideSkipJsonWhitespaceBom(const std::wstring& s, size_t pos) {
    while (pos < s.size() &&
           (s[pos] == 0xFEFF || s[pos] == L' ' || s[pos] == L'\t' ||
            s[pos] == L'\r' || s[pos] == L'\n')) {
        ++pos;
    }
    return pos;
}

// Extract a simple JSON field value after "key": (string without quotes, or
// raw token/array/object slice). Matches historical ExtractJsonField shape for
// common cases used by product. Empty if missing.
inline std::wstring WideExtractJsonField(const std::wstring& objStr, const std::wstring& key) {
    const std::wstring search = L"\"" + key + L"\"";
    size_t p = objStr.find(search);
    if (p == std::wstring::npos) return L"";
    p = objStr.find(L':', p + search.size());
    if (p == std::wstring::npos) return L"";
    p = WideSkipJsonWhitespace(objStr, p + 1);
    if (p >= objStr.size()) return L"";

    if (objStr[p] == L'"') {
        size_t end = p + 1;
        while (end < objStr.size()) {
            if (objStr[end] == L'\\' && end + 1 < objStr.size()) { end += 2; continue; }
            if (objStr[end] == L'"') break;
            end++;
        }
        if (end >= objStr.size()) return L"";
        return objStr.substr(p + 1, end - p - 1);
    }

    if (objStr[p] == L'[') {
        int depth = 1;
        bool inStr = false;
        size_t end = p + 1;
        while (end < objStr.size() && depth > 0) {
            if (inStr) {
                if (objStr[end] == L'\\' && end + 1 < objStr.size()) end++;
                else if (objStr[end] == L'"') inStr = false;
            } else {
                if (objStr[end] == L'"') inStr = true;
                else if (objStr[end] == L'[') depth++;
                else if (objStr[end] == L']') depth--;
            }
            end++;
        }
        return objStr.substr(p, end - p);
    }

    if (objStr[p] == L'{') {
        int depth = 1;
        bool inStr = false;
        size_t end = p + 1;
        while (end < objStr.size() && depth > 0) {
            if (inStr) {
                if (objStr[end] == L'\\' && end + 1 < objStr.size()) end++;
                else if (objStr[end] == L'"') inStr = false;
            } else {
                if (objStr[end] == L'"') inStr = true;
                else if (objStr[end] == L'{') depth++;
                else if (objStr[end] == L'}') depth--;
            }
            end++;
        }
        return objStr.substr(p, end - p);
    }

    // Bare token (number / true / false / null).
    size_t end = p;
    while (end < objStr.size() &&
           objStr[end] != L',' && objStr[end] != L'}' &&
           objStr[end] != L'\n' && objStr[end] != L'\r') {
        end++;
    }
    return objStr.substr(p, end - p);
}

// Parse JSON bool token. "true"/"1" → true; "false"/"0" → false; else fallback.
inline bool WideParseJsonBoolToken(const std::wstring& token, bool fallback = false) {
    const std::wstring lower = WideToLower(WideTrim(token));
    if (lower == L"true" || lower == L"1") return true;
    if (lower == L"false" || lower == L"0") return false;
    return fallback;
}

// OWN-81: JSON bool literal for save paths (no heap).
inline const wchar_t* WideJsonBoolLiteral(bool value) {
    return value ? L"true" : L"false";
}

// Parse JSON int token. Empty/invalid → fallback.
inline int WideParseJsonIntToken(const std::wstring& token, int fallback = 0) {
    const std::wstring text = WideTrim(token);
    if (text.empty()) return fallback;
    // Manual parse to stay pure (no _wtoi dependency).
    size_t i = 0;
    bool neg = false;
    if (text[i] == L'-') { neg = true; ++i; }
    else if (text[i] == L'+') { ++i; }
    if (i >= text.size() || text[i] < L'0' || text[i] > L'9') return fallback;
    long long value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < L'0' || text[i] > L'9') break;
        value = value * 10 + (text[i] - L'0');
        if (value > 2147483647LL) return fallback;
    }
    if (neg) value = -value;
    return static_cast<int>(value);
}

// True when URL contains the official Paddle jobs path fragment (case-insensitive).
inline bool WideIsPaddleOcrJobsUrlPath(const std::wstring& url) {
    return WideContainsNoCase(url, L"/api/v2/ocr/jobs");
}

// OWN-78: strict int parse — rejects empty/null/trailing junk/overflow.
// Returns true and writes out when the whole trimmed token is a signed decimal int.
inline bool WideTryParseJsonIntToken(const std::wstring& raw, int& out) {
    const std::wstring text = WideTrim(raw);
    if (text.empty() || WideEqualsNoCase(text, L"null")) return false;
    size_t i = 0;
    bool neg = false;
    if (text[i] == L'-') { neg = true; ++i; }
    else if (text[i] == L'+') { ++i; }
    if (i >= text.size() || text[i] < L'0' || text[i] > L'9') return false;
    long long value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false; // trailing junk
        value = value * 10 + (text[i] - L'0');
        if (value > 2147483647LL) return false;
    }
    if (neg) value = -value;
    // INT_MIN is -2147483648; after negation of 2147483648 we need care.
    // Digits path above caps at 2147483647 before negate, so INT_MIN not representable
    // via positive digits — accept -2147483648 only if we allow one more digit step.
    // Keep symmetric with WideParseJsonIntToken range (INT_MAX bound).
    if (value < -2147483647LL) return false;
    out = static_cast<int>(value);
    return true;
}

// OWN-78: strict int64 parse — rejects empty/null/trailing junk/overflow.
inline bool WideTryParseJsonInt64Token(const std::wstring& raw, long long& out) {
    const std::wstring text = WideTrim(raw);
    if (text.empty() || WideEqualsNoCase(text, L"null")) return false;
    size_t i = 0;
    bool neg = false;
    if (text[i] == L'-') { neg = true; ++i; }
    else if (text[i] == L'+') { ++i; }
    if (i >= text.size() || text[i] < L'0' || text[i] > L'9') return false;
    // Cap near LLONG_MAX / 10 to avoid overflow.
    constexpr long long kMax = 9223372036854775807LL;
    long long value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        const int digit = text[i] - L'0';
        if (value > (kMax - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (neg) {
        // -LLONG_MAX is fine; -LLONG_MIN would need the extra digit we rejected above.
        out = -value;
    } else {
        out = value;
    }
    return true;
}

// OWN-97: parse "x,y,w,h,max" window geometry CSV (5 ints). Rejects trailing junk.
inline bool WideTryParseCsvInt5(
    const std::wstring& raw,
    int& x, int& y, int& w, int& h, int& maximized)
{
    const std::wstring text = WideTrim(raw);
    if (text.empty()) return false;
    int vals[5] = {};
    size_t start = 0;
    for (int i = 0; i < 5; ++i) {
        size_t comma = (i < 4) ? text.find(L',', start) : std::wstring::npos;
        std::wstring token = (comma == std::wstring::npos)
            ? text.substr(start)
            : text.substr(start, comma - start);
        if (!WideTryParseJsonIntToken(token, vals[i])) return false;
        if (comma == std::wstring::npos) {
            if (i != 4) return false;
            break;
        }
        start = comma + 1;
    }
    // Ensure no extra fields: if more commas after 5th field path, last token ate rest via substr.
    // Reject if more than 4 commas.
    size_t commas = 0;
    for (wchar_t ch : text) if (ch == L',') ++commas;
    if (commas != 4) return false;
    x = vals[0]; y = vals[1]; w = vals[2]; h = vals[3]; maximized = vals[4];
    return true;
}

// OWN-97: parse "YYYY-MM-DD HH:MM:SS.mmm" (at least date+hour+minute; sec/ms optional).
// Returns field count 5..7. Rejects empty.
inline int WideTryParseDateTimeParts(
    const std::wstring& raw,
    int& year, int& month, int& day,
    int& hour, int& minute, int& second, int& millisecond)
{
    year = month = day = hour = minute = second = millisecond = 0;
    const std::wstring text = WideTrim(raw);
    if (text.empty()) return 0;
    // Split on non-digit separators.
    int vals[7] = {};
    int count = 0;
    size_t i = 0;
    while (i < text.size() && count < 7) {
        while (i < text.size() && (text[i] < L'0' || text[i] > L'9')) ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && text[j] >= L'0' && text[j] <= L'9') ++j;
        int v = 0;
        if (!WideTryParseJsonIntToken(text.substr(i, j - i), v)) return 0;
        vals[count++] = v;
        i = j;
    }
    if (count < 5) return 0;
    year = vals[0];
    month = vals[1];
    day = vals[2];
    hour = vals[3];
    minute = vals[4];
    if (count >= 6) second = vals[5];
    if (count >= 7) millisecond = vals[6];
    return count;
}

