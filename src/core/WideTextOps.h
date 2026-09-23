#pragma once

// Wide string text operations (case, trim, comparison, search, auth headers).
// Pure, no Win32 / HWND dependencies.

#include <cwctype>
#include <cwchar>
#include <string>
#include <vector>

// Trim leading/trailing whitespace (iswspace) and leading BOM (U+FEFF).
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

// Normalize label tokens — '-' and whitespace -> '_', then lower.
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

// C-string vs wstring case-sensitive.
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

// Case-insensitive substring search. Empty needle -> true.
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

// True when text contains unresolved local OCR asset references.
inline bool WideContainsUnresolvedOcrAssetReference(const std::wstring& text) {
    const std::wstring lower = WideToLower(text);
    return lower.find(L"zencrop-asset://") != std::wstring::npos ||
        lower.find(L"http://127.0.0.1") != std::wstring::npos ||
        lower.find(L"http://localhost") != std::wstring::npos;
}

// Clamp integer into [lo, hi] (assumes lo <= hi).
inline int WideClampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

// Strip auth scheme prefix "Bearer " / "Token " (case-insensitive); return remainder trimmed.
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

// Build "Authorization: bearer <token>" header value.
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

// True when haystack starts with needle at pos (case-insensitive).
inline bool WideStartsWithNoCaseAt(const std::wstring& text, size_t pos, const wchar_t* prefix) {
    if (!prefix) return false;
    if (*prefix == L'\0') return true;
    if (pos >= text.size()) return false;
    return WideStartsWithNoCase(text.c_str() + pos, prefix);
}

// URL terminator chars used by asset/link scanners.
inline bool WideIsUrlTerminator(wchar_t ch) {
    return iswspace(ch) || ch == L')' || ch == L']' || ch == L'}' ||
        ch == L'"' || ch == L'\'' || ch == L'<' || ch == L'>' ||
        ch == L'`' || ch == L';' || ch == L',';
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

// Substring check (case-sensitive; null-safe).
inline bool WideContains(const wchar_t* haystack, const wchar_t* needle) {
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    return wcsstr(haystack, needle) != nullptr;
}
inline bool WideContains(const std::wstring& haystack, const wchar_t* needle) {
    return WideContains(haystack.c_str(), needle);
}

// True when url contains /api/v2/ocr/jobs
inline bool WideIsPaddleOcrJobsUrlPath(const std::wstring& url) {
    return WideContainsNoCase(url, L"/api/v2/ocr/jobs");
}
