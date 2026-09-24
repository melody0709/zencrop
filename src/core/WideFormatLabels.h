#pragma once

#include <format>
#include <string>

// Pure wide formatting for UI labels, titles, counters, and progress indicators.

inline std::wstring WideFormatSizeWxH(int width, int height)
{
    return std::format(L"{}x{}", width, height);
}

inline std::wstring WideFormatStatusCount(const wchar_t* label, int count)
{
    return std::format(L"{}: {}", label ? label : L"", count);
}

inline std::wstring WideFormatAnnId(int id)
{
    return std::format(L"#{}", id);
}

inline std::wstring WideFormatSlashCount(int a, int b)
{
    return std::format(L"{}/{}", a, b);
}

inline std::wstring WideFormatSlashCountU(unsigned a, unsigned b)
{
    return std::format(L"{}/{}", a, b);
}

inline std::wstring WideFormatSlashCountBar(int a, int b)
{
    return std::format(L"{}/{} | ", a, b);
}

inline std::wstring WideFormatSlashTotal(int total)
{
    return std::format(L"/{}", total);
}

inline std::wstring WideFormatParenSlashCount(int a, int b)
{
    return std::format(L"({}/{})", a, b);
}

inline std::wstring WideFormatMiddotSlashCount(int a, int b)
{
    return std::format(L" \x00b7 {}/{}", a, b);
}

inline std::wstring WideFormatCountLabel(int count, const wchar_t* label)
{
    return std::format(L"{} {}", count, label ? label : L"");
}

inline std::wstring WideFormatCountPrefix(const wchar_t* prefix, int count)
{
    return std::format(L"{}{}", prefix ? prefix : L"", count);
}

inline std::wstring WideFormatPrefixSlashCount(const wchar_t* prefix, int a, int b)
{
    return std::format(L"{}{}/{}", prefix ? prefix : L"", a, b);
}

inline std::wstring WideFormatDpiLabel(int dpi)
{
    return std::format(L"DPI: {}", dpi);
}

inline std::wstring WideFormatPageLabel(int pageIndex)
{
    return std::format(L"Page {}", pageIndex);
}

inline std::wstring WideFormatPageDotLabel(int pageIndex)
{
    return std::format(L"p.{}", pageIndex);
}

inline std::wstring WideFormatPdfPageDotLabel(int pageIndex)
{
    return std::format(L"PDF p.{}", pageIndex);
}

inline std::wstring WideFormatPageSlashLabel(int pageIndex)
{
    return std::format(L" / Page {}", pageIndex);
}

inline std::wstring WideFormatPageWarnPrefix(int pageNumber)
{
    return std::format(L"Page {}: ", pageNumber);
}

inline std::wstring WideFormatParenInt(int value)
{
    return std::format(L" ({})", value);
}

inline std::wstring WideFormatHashIndex(int indexOneBased)
{
    return std::format(L"#{}", indexOneBased);
}

inline std::wstring WideFormatTimesInt(int times)
{
    return std::format(L"{}x", times);
}

inline std::wstring WideFormatMagnifierScale(double scale)
{
    return std::format(L"{:.1f}x", scale);
}

inline std::wstring WideFormatZoomPercent0(double zoomFractionTimes100)
{
    return std::format(L"{:.0f}%", zoomFractionTimes100);
}

inline std::wstring WideFormatOcrElapsedLabel(unsigned minutes, unsigned seconds)
{
    return std::format(L"OCR {:02d}:{:02d}", minutes, seconds);
}

inline std::wstring WideFormatImageTitle(int indexOneBased)
{
    return std::format(L"Image {}", indexOneBased);
}

inline std::wstring WideFormatPdfTitle(int indexOneBased)
{
    return std::format(L"PDF {}", indexOneBased);
}

inline std::wstring WideFormatCaptureTitle(int indexOneBased)
{
    return std::format(L"Capture {}", indexOneBased);
}

inline std::wstring WideFormatHistoryPinHeader(int oneBasedIndex)
{
    return std::format(L"\U0001F4CC #{}  |  ", oneBasedIndex);
}

inline std::wstring WideFormatCropLabel(
    const wchar_t* format, int left, int top, int width, int height)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, 128, format ? format : L"%d, %d  %d x %d", left, top, width, height);
    return buf;
}

inline std::wstring WideFormatGroupsFailedSuffix(int failedGroups)
{
    return std::format(L"{} group(s) failed.", failedGroups);
}
