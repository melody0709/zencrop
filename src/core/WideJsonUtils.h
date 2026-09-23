#pragma once

// JSON traversal, extraction, serialization, and token parsing helpers.
// Pure, no HWND / Win32 dependencies.

#include "core/WideTextOps.h"

#include <cwchar>
#include <string>
#include <vector>

// Escape JSON string content (no surrounding quotes).
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

// Skip JSON whitespace from pos; returns new pos.
inline size_t WideSkipJsonWhitespace(const std::wstring& s, size_t pos) {
    while (pos < s.size() &&
           (s[pos] == L' ' || s[pos] == L'\t' || s[pos] == L'\r' || s[pos] == L'\n')) {
        ++pos;
    }
    return pos;
}

// Skip JSON whitespace including BOM.
inline size_t WideSkipJsonWhitespaceBom(const std::wstring& s, size_t pos) {
    while (pos < s.size() &&
           (s[pos] == 0xFEFF || s[pos] == L' ' || s[pos] == L'\t' ||
            s[pos] == L'\r' || s[pos] == L'\n')) {
        ++pos;
    }
    return pos;
}

// Skip a JSON string starting at pos (must point at opening '"').
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

// True when JSON text has a top-level-ish key token "\"key\"" followed by ':'.
inline bool WideHasJsonKey(const std::wstring& json, const std::wstring& key) {
    const std::wstring search = L"\"" + key + L"\"";
    size_t pos = json.find(search);
    if (pos == std::wstring::npos) return false;
    pos = json.find(L':', pos + search.size());
    return pos != std::wstring::npos;
}

// Extract a simple JSON field value after "key": (string without quotes, or raw token).
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

    size_t end = p;
    while (end < objStr.size() &&
           objStr[end] != L',' && objStr[end] != L'}' &&
           objStr[end] != L'\n' && objStr[end] != L'\r') {
        end++;
    }
    return objStr.substr(p, end - p);
}

// Parse JSON bool token. "true"/"1" -> true; "false"/"0" -> false; else fallback.
inline bool WideParseJsonBoolToken(const std::wstring& token, bool fallback = false) {
    const std::wstring lower = WideToLower(WideTrim(token));
    if (lower == L"true" || lower == L"1") return true;
    if (lower == L"false" || lower == L"0") return false;
    return fallback;
}

// JSON bool literal for save paths (no heap).
inline const wchar_t* WideJsonBoolLiteral(bool value) {
    return value ? L"true" : L"false";
}

// Parse JSON int token. Empty/invalid -> fallback.
inline int WideParseJsonIntToken(const std::wstring& token, int fallback = 0) {
    const std::wstring text = WideTrim(token);
    if (text.empty()) return fallback;
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

// Strict int parse — rejects empty/null/trailing junk/overflow.
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
        if (text[i] < L'0' || text[i] > L'9') return false;
        value = value * 10 + (text[i] - L'0');
        if (value > 2147483647LL) return false;
    }
    if (neg) value = -value;
    if (value < -2147483647LL) return false;
    out = static_cast<int>(value);
    return true;
}

// Strict int64 parse — rejects empty/null/trailing junk/overflow.
inline bool WideTryParseJsonInt64Token(const std::wstring& raw, long long& out) {
    const std::wstring text = WideTrim(raw);
    if (text.empty() || WideEqualsNoCase(text, L"null")) return false;
    size_t i = 0;
    bool neg = false;
    if (text[i] == L'-') { neg = true; ++i; }
    else if (text[i] == L'+') { ++i; }
    if (i >= text.size() || text[i] < L'0' || text[i] > L'9') return false;
    constexpr long long kMax = 9223372036854775807LL;
    long long value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        const int digit = text[i] - L'0';
        if (value > (kMax - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (neg) {
        out = -value;
    } else {
        out = value;
    }
    return true;
}

// Parse "x,y,w,h,max" window geometry CSV (5 ints). Rejects trailing junk.
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
    size_t commas = 0;
    for (wchar_t ch : text) if (ch == L',') ++commas;
    if (commas != 4) return false;
    x = vals[0]; y = vals[1]; w = vals[2]; h = vals[3]; maximized = vals[4];
    return true;
}

// Parse "YYYY-MM-DD HH:MM:SS.mmm" (at least date+hour+minute; sec/ms optional).
inline int WideTryParseDateTimeParts(
    const std::wstring& raw,
    int& year, int& month, int& day,
    int& hour, int& minute, int& second, int& millisecond)
{
    year = month = day = hour = minute = second = millisecond = 0;
    const std::wstring text = WideTrim(raw);
    if (text.empty()) return 0;
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

// JSON field line builders (indent 4 spaces; no trailing comma).
inline std::wstring WideJsonFieldString(const wchar_t* key, const wchar_t* escapedValue) {
    std::wstring out = L"    \"";
    out += key ? key : L"";
    out += L"\": \"";
    out += escapedValue ? escapedValue : L"";
    out += L"\"";
    return out;
}

inline std::wstring WideJsonFieldString(const wchar_t* key, const std::wstring& escapedValue) {
    return WideJsonFieldString(key, escapedValue.c_str());
}

inline std::wstring WideJsonFieldInt(const wchar_t* key, int value) {
    wchar_t buf[96] = {};
    swprintf_s(buf, L"    \"%s\": %d", key ? key : L"", value);
    return buf;
}

inline std::wstring WideJsonFieldUnsigned(const wchar_t* key, unsigned value) {
    wchar_t buf[96] = {};
    swprintf_s(buf, L"    \"%s\": %u", key ? key : L"", value);
    return buf;
}

inline std::wstring WideJsonFieldBool(const wchar_t* key, bool value) {
    std::wstring out = L"    \"";
    out += key ? key : L"";
    out += L"\": ";
    out += value ? L"true" : L"false";
    return out;
}

inline std::wstring WideJsonFieldBoolLit(const wchar_t* key, const wchar_t* boolLit) {
    std::wstring out = L"    \"";
    out += key ? key : L"";
    out += L"\": ";
    out += boolLit ? boolLit : L"false";
    return out;
}

inline std::wstring WideJsonFieldStringLiteral(const wchar_t* key, const wchar_t* literal) {
    return WideJsonFieldString(key, literal);
}

inline std::wstring WideJsonFieldInt2(const wchar_t* key, int value) {
    wchar_t buf[128] = {};
    swprintf_s(buf, L"  \"%s\": %d,\r\n", key ? key : L"", value);
    return buf;
}

inline std::wstring WideJsonFieldUll2(const wchar_t* key, unsigned long long value) {
    wchar_t buf[160] = {};
    swprintf_s(buf, L"  \"%s\": %llu,\r\n", key ? key : L"", value);
    return buf;
}

inline std::wstring WideJsonFieldIntCompact(const wchar_t* key, int value) {
    wchar_t buf[96] = {};
    swprintf_s(buf, L"\"%s\":%d", key ? key : L"", value);
    return buf;
}

inline std::wstring WideJsonFieldUllCompact(const wchar_t* key, unsigned long long value) {
    wchar_t buf[128] = {};
    swprintf_s(buf, L"\"%s\":%llu", key ? key : L"", value);
    return buf;
}

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

// Indent string of `count` spaces (clamped at 0).
inline std::wstring WideJsonIndent(int count) {
    return std::wstring(static_cast<size_t>(count > 0 ? count : 0), L' ');
}

// Find matching close bracket for openCh/closeCh starting at `start` (must be openCh).
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

// Find start index of "\"key\"" field that is followed by ':'.
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

// Parse JSON int with null-token support.
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
