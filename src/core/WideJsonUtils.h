#pragma once

#include "core/WideStringUtils.h"

// JSON traversal and extraction helpers. Base wide string parsing remains in
// WideStringUtils.h; consumers of this heavier structural API include this header.

// OWN-77: pure JSON structural helpers (no HWND; hermetic-friendly).

// Indent string of `count` spaces (clamped at 0).
inline std::wstring WideJsonIndent(int count) {
    return std::wstring(static_cast<size_t>(count > 0 ? count : 0), L' ');
}

// Find matching close bracket for openCh/closeCh starting at `start` (must be openCh).
// Respects JSON string escapes. Returns npos on failure.
inline size_t WideJsonFindMatching(
    const std::wstring& s,
    size_t start,
    wchar_t openCh,
    wchar_t closeCh)
{
    if (start >= s.size() || s[start] != openCh) return std::wstring::npos;
    bool inString = false;
    int depth = 1;
    for (size_t i = start + 1; i < s.size(); ++i) {
        const wchar_t ch = s[i];
        if (inString) {
            if (ch == L'\\' && i + 1 < s.size()) {
                ++i;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }
        if (ch == L'"') {
            inString = true;
        } else if (ch == openCh) {
            ++depth;
        } else if (ch == closeCh) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::wstring::npos;
}

// Find start index of "\"key\"" field that is followed by ':' (string-aware scan).
// Matches historical OcrBlockJsonFindField semantics.
inline size_t WideJsonFindField(const std::wstring& obj, const std::wstring& key) {
    const std::wstring search = L"\"" + key + L"\"";
    bool inString = false;
    for (size_t i = 0; i < obj.size(); ++i) {
        const wchar_t ch = obj[i];
        if (inString) {
            if (ch == L'\\' && i + 1 < obj.size()) {
                ++i;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }
        if (ch != L'"') continue;
        if (i + search.size() <= obj.size() &&
            obj.compare(i, search.size(), search) == 0) {
            const size_t after = WideSkipJsonWhitespace(obj, i + search.size());
            if (after < obj.size() && obj[after] == L':') {
                return i;
            }
        }
        inString = true;
    }
    return std::wstring::npos;
}

// Extract JSON field value using string-aware field find + bracket matching.
// Prefer this over WideExtractJsonField when object may contain nested "key" strings.
inline std::wstring WideJsonExtractValue(const std::wstring& obj, const std::wstring& key) {
    const std::wstring search = L"\"" + key + L"\"";
    size_t pos = WideJsonFindField(obj, key);
    if (pos == std::wstring::npos) return L"";
    pos = obj.find(L':', pos + search.size());
    if (pos == std::wstring::npos) return L"";
    pos = WideSkipJsonWhitespace(obj, pos + 1);
    if (pos >= obj.size()) return L"";

    if (obj[pos] == L'"') {
        size_t end = pos + 1;
        while (end < obj.size()) {
            if (obj[end] == L'\\' && end + 1 < obj.size()) {
                end += 2;
                continue;
            }
            if (obj[end] == L'"') break;
            ++end;
        }
        return end < obj.size() ? obj.substr(pos + 1, end - pos - 1) : L"";
    }
    if (obj[pos] == L'[') {
        const size_t end = WideJsonFindMatching(obj, pos, L'[', L']');
        return end == std::wstring::npos ? L"" : obj.substr(pos, end - pos + 1);
    }
    if (obj[pos] == L'{') {
        const size_t end = WideJsonFindMatching(obj, pos, L'{', L'}');
        return end == std::wstring::npos ? L"" : obj.substr(pos, end - pos + 1);
    }

    size_t end = pos;
    while (end < obj.size() && obj[end] != L',' && obj[end] != L'}' &&
           obj[end] != L'\r' && obj[end] != L'\n') {
        ++end;
    }
    return obj.substr(pos, end - pos);
}

// Parse JSON int with null-token support (empty/"null" → fallback).
// Pure digit parse (no wcstoll); rejects non-integer trailing junk.
inline int WideJsonParseIntOrNull(const std::wstring& raw, int fallback = 0) {
    const std::wstring value = WideTrim(raw);
    if (value.empty() || WideEqualsNoCase(value, L"null")) return fallback;
    return WideParseJsonIntToken(value, fallback);
}

// Parse JSON bool with null-token support.
inline bool WideJsonParseBoolOrNull(const std::wstring& raw, bool fallback = false) {
    const std::wstring value = WideTrim(raw);
    if (value.empty() || WideEqualsNoCase(value, L"null")) return fallback;
    return WideParseJsonBoolToken(value, fallback);
}

// Extract top-level object field value (key must be at depth 1).
// Scans only outside nested objects/arrays/strings.
inline std::wstring WideJsonFindTopLevelValue(
    const std::wstring& json,
    const std::wstring& key)
{
    size_t pos = WideSkipJsonWhitespaceBom(json, 0);
    if (pos >= json.size() || json[pos] != L'{') return L"";
    ++pos;

    while (pos < json.size()) {
        pos = WideSkipJsonWhitespaceBom(json, pos);
        if (pos >= json.size() || json[pos] == L'}') break;
        if (json[pos] == L',') {
            ++pos;
            continue;
        }
        if (json[pos] != L'"') break;

        const size_t keyStart = pos + 1;
        const size_t keyEnd = WideSkipJsonString(json, pos);
        if (keyEnd <= keyStart) break;
        const std::wstring currentKey = json.substr(keyStart, keyEnd - keyStart - 1);
        pos = WideSkipJsonWhitespaceBom(json, keyEnd);
        if (pos >= json.size() || json[pos] != L':') break;
        ++pos;
        pos = WideSkipJsonWhitespaceBom(json, pos);
        if (pos >= json.size()) break;

        size_t valueEnd = pos;
        if (json[pos] == L'{') {
            valueEnd = WideJsonFindMatching(json, pos, L'{', L'}');
            if (valueEnd == std::wstring::npos) break;
            ++valueEnd;
        } else if (json[pos] == L'[') {
            valueEnd = WideJsonFindMatching(json, pos, L'[', L']');
            if (valueEnd == std::wstring::npos) break;
            ++valueEnd;
        } else if (json[pos] == L'"') {
            valueEnd = WideSkipJsonString(json, pos);
        } else {
            while (valueEnd < json.size() &&
                   json[valueEnd] != L',' && json[valueEnd] != L'}' &&
                   json[valueEnd] != L'\n' && json[valueEnd] != L'\r') {
                ++valueEnd;
            }
        }

        if (currentKey == key) {
            if (json[pos] == L'"' && valueEnd > pos + 1) {
                return json.substr(pos + 1, valueEnd - pos - 2);
            }
            return json.substr(pos, valueEnd - pos);
        }
        pos = valueEnd;
    }
    return L"";
}

// Collect top-level object items from a JSON array text ("[{...},{...}]").
inline std::vector<std::wstring> WideJsonObjectArrayItems(const std::wstring& arrayText) {
    std::vector<std::wstring> items;
    const size_t start = arrayText.find(L'[');
    if (start == std::wstring::npos) return items;

    bool inString = false;
    int objectDepth = 0;
    size_t objectStart = std::wstring::npos;
    for (size_t i = start + 1; i < arrayText.size(); ++i) {
        const wchar_t ch = arrayText[i];
        if (inString) {
            if (ch == L'\\' && i + 1 < arrayText.size()) {
                ++i;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }
        if (ch == L'"') {
            inString = true;
            continue;
        }
        if (ch == L'{') {
            if (objectDepth == 0) objectStart = i;
            ++objectDepth;
            continue;
        }
        if (ch == L'}') {
            if (objectDepth > 0) --objectDepth;
            if (objectDepth == 0 && objectStart != std::wstring::npos) {
                items.push_back(arrayText.substr(objectStart, i - objectStart + 1));
                objectStart = std::wstring::npos;
            }
            continue;
        }
        if (ch == L']' && objectDepth == 0) break;
    }
    return items;
}

// True when token is JSON null (case-insensitive, trimmed).
inline bool WideIsJsonNullToken(const std::wstring& raw) {
    return WideEqualsNoCase(WideTrim(raw), L"null");
}

// Clamp double-like integer string parse for opacity/thickness style fields.
// Pure; empty/null → fallback; out of [minV,maxV] → clamped.
inline int WideParseClampedIntToken(
    const std::wstring& raw,
    int fallback,
    int minV,
    int maxV)
{
    if (WideIsJsonNullToken(raw) || WideTrim(raw).empty()) return fallback;
    int value = WideParseJsonIntToken(raw, fallback);
    if (value < minV) return minV;
    if (value > maxV) return maxV;
    return value;
}
