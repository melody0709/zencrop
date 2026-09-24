#pragma once

#include <format>
#include <string>

// Pure wide formatting for numeric labels, hex values, and hashes.

inline std::wstring WideFormatHexByte02(unsigned value)
{
    return std::format(L"0x{:02X}", value & 0xFFu);
}

inline std::wstring WideFormatHexU32(unsigned value)
{
    return std::format(L"0x{:08X}", value);
}

inline std::wstring WideFormatHexLower02(unsigned value)
{
    return std::format(L"{:02x}", value & 0xFFu);
}

inline std::wstring WideFormatHexUpper02(unsigned value)
{
    return std::format(L"{:02X}", value & 0xFFu);
}

inline std::wstring WideFormatHexLower(unsigned value)
{
    return std::format(L"{:x}", value);
}

inline std::wstring WideFormatHexUpper(unsigned value)
{
    return std::format(L"{:X}", value);
}

inline std::wstring WideFormatUrlPercentByte(unsigned value)
{
    return std::format(L"%{:02X}", value & 0xFFu);
}

inline std::wstring WideFormatUPlusCodepoint(unsigned value)
{
    return std::format(L"U+{:04X}", value);
}

inline std::wstring WideFormatHash016(unsigned long long value)
{
    return std::format(L"{:016x}", value);
}

inline std::wstring WideFormatUll(unsigned long long value)
{
    return std::format(L"{}", value);
}

inline std::wstring WideFormatUnsigned(unsigned value)
{
    return std::format(L"{}", value);
}

inline std::wstring WideFormatIntLabel(int value)
{
    return std::format(L"{}", value);
}

inline std::wstring WideFormatPercentLabel(int value)
{
    return std::format(L"{}%", value);
}

inline std::wstring WideFormatPxLabel(int value)
{
    return std::format(L"{} px", value);
}

inline std::wstring WideFormatFunctionKey(int n)
{
    return std::format(L"F{}", n);
}
