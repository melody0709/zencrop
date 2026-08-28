#pragma once

#include "core/WideStringUtils.h"

// Status, Win32, and local-service presentation formatters. Consumers of this
// presentation layer include it explicitly instead of pulling it through the
// base text/path header.

// OWN-120: pure Win32 / page-asset / status-count wide formatters.

// "prefix: <err>" (Win32 GetLastError style product messages).
inline std::wstring WideFormatWin32ErrorSuffix(const wchar_t* prefix, unsigned long err)
{
    wchar_t buf[384] = {};
    swprintf_s(buf, L"%s: %lu", prefix ? prefix : L"", err);
    return buf;
}

// "prefix failed: <err>"
inline std::wstring WideFormatWin32Failed(const wchar_t* apiName, unsigned long err)
{
    wchar_t buf[384] = {};
    swprintf_s(buf, L"%s failed: %lu", apiName ? apiName : L"", err);
    return buf;
}

// "page N / asset M" style id (page 1-based or 0-based as caller chooses).
inline std::wstring WideFormatPageAssetId(int page, int assetOrder)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%d:%d", page, assetOrder);
    return buf;
}

// "WxH" size label.
inline std::wstring WideFormatSizeWxH(int width, int height)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"%dx%d", width, height);
    return buf;
}

// Status count row: "label: N"
inline std::wstring WideFormatStatusCount(const wchar_t* label, int count)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"%s: %d", label ? label : L"", count);
    return buf;
}

// Annotation id label "#N"
inline std::wstring WideFormatAnnId(int id)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"#%d", id);
    return buf;
}

// Join directory + glob pattern (e.g. dir + "\\*").
inline std::wstring WideJoinGlob(const std::wstring& dir, const wchar_t* pattern)
{
    return WideJoinPath(dir, pattern ? pattern : L"*");
}

// OWN-121: pure localhost / ms-spaced / slash-count / port wide formatters.
// "http://127.0.0.1:<port>"
inline std::wstring WideFormatLocalhostBase(int port)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"http://127.0.0.1:%d", port);
    return buf;
}

// "http://127.0.0.1:<port>/v1/chat/completions"
inline std::wstring WideFormatLocalhostChatCompletions(int port)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"http://127.0.0.1:%d/v1/chat/completions", port);
    return buf;
}

// "N ms" (spaced, for status rows)
inline std::wstring WideFormatMsSpaced(unsigned long ms)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"%lu ms", ms);
    return buf;
}

// "a/b" count pair
inline std::wstring WideFormatSlashCount(int a, int b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%d/%d", a, b);
    return buf;
}

// "N failed" / "N canceled" style count-with-label
inline std::wstring WideFormatCountLabel(int count, const wchar_t* label)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%d %s", count, label ? label : L"");
    return buf;
}

// "DPI: N"
inline std::wstring WideFormatDpiLabel(int dpi)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"DPI: %d", dpi);
    return buf;
}

// "Page N" title fragment
inline std::wstring WideFormatPageLabel(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"Page %d", pageIndex);
    return buf;
}
