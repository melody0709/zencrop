#pragma once

#include "core/WidePathUtils.h"
#include <format>
#include <string>

// Pure wide formatting for temporary filenames, cache keys, paths, and extensions.

inline std::wstring WideJoinGlob(const std::wstring& dir, const wchar_t* pattern)
{
    return WideJoinPath(dir, pattern ? pattern : L"*");
}

inline std::wstring WideFormatOcrCropFileName(
    int hour, int minute, int second, int milliseconds)
{
    return std::format(L"ocr_crop_{:02d}{:02d}{:02d}_{:03d}.png",
        hour, minute, second, milliseconds);
}

inline std::wstring WideFormatOcrDropFileName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds)
{
    return std::format(L"ocr_drop_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}_{:03d}.png",
        year, month, day, hour, minute, second, milliseconds);
}

inline std::wstring WideFormatClipTempName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds,
    unsigned long pid, int attempt, const wchar_t* ext)
{
    return std::format(L"ZenCrop_clip_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}_{:03d}_{}_{:02d}{}",
        year, month, day, hour, minute, second, milliseconds,
        pid, attempt, ext ? ext : L"");
}

inline std::wstring WideFormatCodecTempName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds,
    unsigned long pid, unsigned counter, int attempt, const wchar_t* suffix)
{
    return std::format(L"ZenCrop_codec_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}_{:03d}_{}_{}_{:02d}{}",
        year, month, day, hour, minute, second, milliseconds,
        pid, counter, attempt, suffix ? suffix : L"");
}

inline std::wstring WideFormatPdfPreviewDirName(unsigned long pid, unsigned long long tick)
{
    return std::format(L"zencrop_pdf_preview_{}_{}", pid, tick);
}

inline std::wstring WideFormatTimedSeqPng(
    const wchar_t* prefix,
    int hour, int minute, int second, int milliseconds, unsigned seq)
{
    return std::format(L"{}_{:02d}{:02d}{:02d}_{:03d}_{:03d}.png",
        prefix ? prefix : L"item",
        hour, minute, second, milliseconds, seq);
}

inline std::wstring WideFormatPdfTempName(
    unsigned long pid, unsigned long long tick, unsigned counter)
{
    return std::format(L"pdf_{}_{}_{:03d}.pdf", pid, tick, counter);
}

inline std::wstring WideFormatTmpPidTick(unsigned long pid, unsigned long long tick)
{
    return std::format(L".tmp.{}.{}", pid, tick);
}

inline std::wstring WideFormatDotKindPidTick(
    const wchar_t* kind, unsigned long pid, unsigned long long tick)
{
    return std::format(L".{}.{}.{}", kind ? kind : L"", pid, tick);
}

inline std::wstring WideFormatCandidatePidTick(unsigned long pid, unsigned long long tick)
{
    return WideFormatDotKindPidTick(L"candidate", pid, tick);
}

inline std::wstring WideFormatBackupPidTick(unsigned long pid, unsigned long long tick)
{
    return WideFormatDotKindPidTick(L"backup", pid, tick);
}

inline std::wstring WideFormatPidTickCounterSuffix(
    unsigned long pid, unsigned long long tick, unsigned counter, const wchar_t* suffix)
{
    return std::format(L".{}.{}.{}{}",
        pid, tick, counter, suffix ? suffix : L".tmp");
}

inline std::wstring WideFormatPathHashPage(const std::wstring& path, int pageIndex)
{
    return std::format(L"{}#p{}", path, pageIndex);
}

inline std::wstring WideFormatThumbnailGenPrefix(unsigned long long generation)
{
    return std::format(L"thumbnail.g{}.", generation);
}

inline std::wstring WideFormatThumbSizeSuffix(int width, int height)
{
    return std::format(L"\n{}x{}", width, height);
}

inline std::wstring WideFormatColonPageKey(int pageIndex)
{
    return std::format(L":page:{}", pageIndex);
}

inline std::wstring WideFormatDupSuffix02(int suffix)
{
    return std::format(L"_{:02d}", suffix);
}

inline std::wstring WideFormatDuplicateSuffix(int ordinal)
{
    return std::format(L":duplicate:{}", ordinal);
}
