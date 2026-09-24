#pragma once

#include <cwchar>
#include <cwctype>
#include <string>

// Pure wide string comparison operations.

inline bool WideEqualsNoCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (towlower(a[i]) != towlower(b[i])) {
            return false;
        }
    }
    return true;
}

inline bool WideEquals(const std::wstring& a, const std::wstring& b)
{
    return a == b;
}

inline bool WideEquals(const wchar_t* a, const wchar_t* b)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return wcscmp(a, b) == 0;
}

inline bool WideEquals(const wchar_t* a, const std::wstring& b)
{
    if (!a) {
        return b.empty();
    }
    return b == a;
}

inline bool WideEquals(const std::wstring& a, const wchar_t* b)
{
    if (!b) {
        return a.empty();
    }
    return a == b;
}

inline bool WideStartsWithNoCase(const std::wstring& value, const std::wstring& prefix)
{
    if (value.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (towlower(value[i]) != towlower(prefix[i])) {
            return false;
        }
    }
    return true;
}

inline bool WideContainsNoCase(const std::wstring& value, const std::wstring& needle)
{
    if (needle.empty()) {
        return true;
    }
    if (value.size() < needle.size()) {
        return false;
    }
    for (size_t i = 0; i + needle.size() <= value.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (towlower(value[i + j]) != towlower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

inline bool WideStartsWithNoCaseAt(const std::wstring& text, size_t pos, const wchar_t* prefix)
{
    if (!prefix) {
        return false;
    }
    if (*prefix == L'\0') {
        return true;
    }
    if (pos >= text.size()) {
        return false;
    }
    return WideStartsWithNoCase(text.c_str() + pos, prefix);
}
