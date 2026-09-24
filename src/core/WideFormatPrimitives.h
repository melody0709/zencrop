#pragma once

#include <format>
#include <string>

// Pure wide formatting for padding, coordinates, floating point, dates, times, and durations.

inline std::wstring WideFormatPad2(int value)
{
    return std::format(L"{:02d}", value);
}

inline std::wstring WideFormatPad3(int value)
{
    return std::format(L"{:03d}", value);
}

inline std::wstring WideFormatPad4(int value)
{
    return std::format(L"{:04d}", value);
}

inline std::wstring WideFormatPointLabel(long x, long y)
{
    return std::format(L"({}, {})", x, y);
}

inline std::wstring WideFormatPointXy(int x, int y)
{
    return std::format(L"{},{}", x, y);
}

inline std::wstring WideFormatBboxLtrb(long left, long top, long right, long bottom)
{
    return std::format(L"{},{} - {},{}", left, top, right, bottom);
}

inline std::wstring WideFormatFloat1(double value)
{
    return std::format(L"{:.1f}", value);
}

inline std::wstring WideFormatFloat2(double value)
{
    return std::format(L"{:.2f}", value);
}

inline std::wstring WideFormatFloat6(double value)
{
    return std::format(L"{:.6f}", value);
}

inline std::wstring WideFormatCsvInt5(int a, int b, int c, int d, int e)
{
    return std::format(L"{},{},{},{},{}", a, b, c, d, e);
}

inline std::wstring WideFormatSeconds1(double seconds)
{
    return std::format(L"{:.1f}s", seconds);
}

inline std::wstring WideFormatElapsedParenSeconds1(double seconds)
{
    return std::format(L"  ({:.1f}s)", seconds);
}

inline std::wstring WideFormatElapsedMs(unsigned long elapsedMs)
{
    return std::format(L"{}ms", elapsedMs);
}

inline std::wstring WideFormatMsSpaced(unsigned long ms)
{
    return std::format(L"{} ms", ms);
}

inline std::wstring WideFormatMmSs(unsigned minutes, unsigned seconds)
{
    return std::format(L"{:02d}:{:02d}", minutes, seconds);
}

inline std::wstring WideFormatDateParts(int year, int month, int day)
{
    return std::format(L"{:04d}-{:02d}-{:02d}", year, month, day);
}

inline std::wstring WideFormatDateTimeParts(
    int year, int month, int day,
    int hour, int minute, int second)
{
    return std::format(L"{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
        year, month, day, hour, minute, second);
}

inline std::wstring WideFormatDateTimeMinuteParts(
    unsigned year, unsigned month, unsigned day,
    unsigned hour, unsigned minute)
{
    return std::format(L"{:04d}-{:02d}-{:02d} {:02d}:{:02d}",
        year, month, day, hour, minute);
}

inline std::wstring WideFormatIsoUtcTimestamp(
    unsigned year, unsigned month, unsigned day,
    unsigned hour, unsigned minute, unsigned second, unsigned milliseconds)
{
    return std::format(L"{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z",
        year, month, day, hour, minute, second, milliseconds);
}

inline std::wstring WideFormatCompactStamp(
    int year, int month, int day,
    int hour, int minute, int second)
{
    return std::format(L".bad.{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}",
        year, month, day, hour, minute, second);
}

inline std::wstring WideFormatYmdCompact(int year, int month, int day)
{
    return std::format(L"{:04d}{:02d}{:02d}", year, month, day);
}

inline std::wstring WideFormatHmsCompact(int hour, int minute, int second)
{
    return std::format(L"{:02d}{:02d}{:02d}", hour, minute, second);
}

inline std::wstring WideFormatMs3(int milliseconds)
{
    return std::format(L"{:03d}", milliseconds);
}

inline std::wstring WideFormatMegabytes0(double megabytes)
{
    return std::format(L"{:.0f} MB", megabytes);
}

inline std::wstring WideFormatMegabytes1(double megabytes)
{
    return std::format(L"{:.1f} MB", megabytes);
}

inline std::wstring WideFormatMpEstimate(double totalMp, double maxPerPageMp)
{
    return std::format(L"{:.1f} MP total, {:.1f} MP max/page", totalMp, maxPerPageMp);
}

inline std::wstring WideFormatUtf16OffsetSuffix(unsigned long long offset)
{
    return std::format(L" at UTF-16 offset {}.", offset);
}

inline std::wstring WideFormatIntDotSuffix(const wchar_t* prefix, int value)
{
    return std::format(L"{}{}.", prefix ? prefix : L"", value);
}

inline std::wstring WideFormatIntColonDetail(
    const wchar_t* prefix, int value, const std::wstring& detail)
{
    return std::format(L"{}{}: {}", prefix ? prefix : L"", value, detail);
}
