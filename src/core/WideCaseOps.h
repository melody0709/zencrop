#pragma once

#include <cwctype>
#include <string>

// Pure wide string casing operations.

inline std::wstring WideToLower(std::wstring value)
{
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value;
}

inline std::wstring WideToUpper(std::wstring value)
{
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towupper(ch));
    }
    return value;
}
