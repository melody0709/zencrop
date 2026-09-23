#pragma once

#include "core/WideTextOps.h"
#include "core/WidePathUtils.h"
#include "core/WideColorUtils.h"
#include "core/WideJsonUtils.h"
#include <vector>

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

// OWN-109/111/112: pure date/time/UI/pad formatters (int parts only; no SYSTEMTIME).
inline std::wstring WideFormatPad2(int value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%02d", value);
    return buf;
}

inline std::wstring WideFormatPad4(int value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%04d", value);
    return buf;
}

inline std::wstring WideFormatPointLabel(long x, long y)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"(%ld, %ld)", x, y);
    return buf;
}


inline std::wstring WideFormatDateParts(int year, int month, int day)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%04d-%02d-%02d", year, month, day);
    return buf;
}

inline std::wstring WideFormatPercentLabel(int value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%d%%", value);
    return buf;
}

inline std::wstring WideFormatPxLabel(int value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%d px", value);
    return buf;
}

inline std::wstring WideFormatIntLabel(int value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%d", value);
    return buf;
}

inline std::wstring WideFormatCompactStamp(
    int year, int month, int day,
    int hour, int minute, int second)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L".bad.%04d%02d%02d-%02d%02d%02d",
        year, month, day, hour, minute, second);
    return buf;
}

inline std::wstring WideFormatDateTimeParts(
    int year, int month, int day,
    int hour, int minute, int second)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
        year, month, day, hour, minute, second);
    return buf;
}

inline std::wstring WideFormatOcrCropFileName(
    int hour, int minute, int second, int milliseconds)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"ocr_crop_%02d%02d%02d_%03d.png",
        hour, minute, second, milliseconds);
    return buf;
}

// OWN-113: pure UI/page/hex/elapsed/temp-name formatters (int/string parts only).
inline std::wstring WideFormatPad3(int value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%03d", value);
    return buf;
}

inline std::wstring WideFormatMmSs(unsigned minutes, unsigned seconds)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%02u:%02u", minutes, seconds);
    return buf;
}

inline std::wstring WideFormatOcrElapsedLabel(unsigned minutes, unsigned seconds)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"OCR %02u:%02u", minutes, seconds);
    return buf;
}

inline std::wstring WideFormatElapsedMs(unsigned long elapsedMs)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%lums", elapsedMs);
    return buf;
}

inline std::wstring WideFormatSeconds1(double seconds)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.1fs", seconds);
    return buf;
}

inline std::wstring WideFormatPageIndexName(int pageIndex)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"page_%04d", pageIndex);
    return buf;
}

inline std::wstring WideFormatImageIndexName(int index)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"image_%03d", index);
    return buf;
}

inline std::wstring WideFormatPageAssetStem(int pageIndex, int assetIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"page_%04d_img_%03d", pageIndex, assetIndex);
    return buf;
}

inline std::wstring WideFormatPageAssetPrefix(int pageIndex)
{
    wchar_t buf[40] = {};
    swprintf_s(buf, L"page_%04d_img_", pageIndex);
    return buf;
}

inline std::wstring WideFormatDupSuffix02(int suffix)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"_%02d", suffix);
    return buf;
}

inline std::wstring WideFormatHexByte02(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"0x%02X", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatHexU32(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"0x%08X", value);
    return buf;
}

inline std::wstring WideFormatUrlPercentByte(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%%%02X", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatUPlusCodepoint(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"U+%04X", value);
    return buf;
}

inline std::wstring WideFormatHash016(unsigned long long value)
{
    wchar_t buf[24] = {};
    swprintf_s(buf, L"%016llx", value);
    return buf;
}

inline std::wstring WideFormatMagnifierScale(double scale)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%.1fx", scale);
    return buf;
}

inline std::wstring WideFormatYmdCompact(int year, int month, int day)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%04d%02d%02d", year, month, day);
    return buf;
}

inline std::wstring WideFormatHmsCompact(int hour, int minute, int second)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%02d%02d%02d", hour, minute, second);
    return buf;
}

inline std::wstring WideFormatMs3(int milliseconds)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%03d", milliseconds);
    return buf;
}

// ZenCrop_clip_YYYYMMDD_HHMMSS_mmm_pid_attempt.ext (parts pure; PID/attempt product args).
inline std::wstring WideFormatClipTempName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds,
    unsigned long pid, int attempt, const wchar_t* ext)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"ZenCrop_clip_%04d%02d%02d_%02d%02d%02d_%03d_%lu_%02d%s",
        year, month, day, hour, minute, second, milliseconds,
        pid, attempt, ext ? ext : L"");
    return buf;
}

// ZenCrop_codec_YYYYMMDD_HHMMSS_mmm_pid_counter_attempt.suffix
inline std::wstring WideFormatCodecTempName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds,
    unsigned long pid, unsigned counter, int attempt, const wchar_t* suffix)
{
    wchar_t buf[180] = {};
    swprintf_s(buf, L"ZenCrop_codec_%04d%02d%02d_%02d%02d%02d_%03d_%lu_%u_%02d%s",
        year, month, day, hour, minute, second, milliseconds,
        pid, counter, attempt, suffix ? suffix : L"");
    return buf;
}

// ocr_drop_YYYYMMDD_HHMMSS_mmm.png
inline std::wstring WideFormatOcrDropFileName(
    int year, int month, int day,
    int hour, int minute, int second, int milliseconds)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"ocr_drop_%04d%02d%02d_%02d%02d%02d_%03d.png",
        year, month, day, hour, minute, second, milliseconds);
    return buf;
}

inline std::wstring WideFormatMegabytes1(double megabytes)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.1f MB", megabytes);
    return buf;
}

inline std::wstring WideFormatMegabytes0(double megabytes)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.0f MB", megabytes);
    return buf;
}

// OWN-114: pure hex/float/CSV/zoom/temp-name formatters (int/float/string parts only).
inline std::wstring WideFormatHexLower02(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%02x", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatHexUpper02(unsigned value)
{
    wchar_t buf[8] = {};
    swprintf_s(buf, L"%02X", value & 0xFFu);
    return buf;
}

inline std::wstring WideFormatHexLower(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%x", value);
    return buf;
}

inline std::wstring WideFormatHexUpper(unsigned value)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%X", value);
    return buf;
}

inline std::wstring WideFormatFloat1(double value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.1f", value);
    return buf;
}

inline std::wstring WideFormatFloat2(double value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.2f", value);
    return buf;
}

inline std::wstring WideFormatFloat6(double value)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"%.6f", value);
    return buf;
}

inline std::wstring WideFormatZoomPercent0(double zoomFractionTimes100)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.0f%%", zoomFractionTimes100);
    return buf;
}

inline std::wstring WideFormatElapsedParenSeconds1(double seconds)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"  (%.1fs)", seconds);
    return buf;
}

inline std::wstring WideFormatCsvInt5(int a, int b, int c, int d, int e)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%d,%d,%d,%d,%d", a, b, c, d, e);
    return buf;
}

inline std::wstring WideFormatUnsigned(unsigned value)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%u", value);
    return buf;
}

inline std::wstring WideFormatHotkeyJson(
    const wchar_t* winLit, const wchar_t* ctrlLit,
    const wchar_t* shiftLit, const wchar_t* altLit, int key)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"{\"win\": %s, \"ctrl\": %s, \"shift\": %s, \"alt\": %s, \"key\": %d}",
        winLit ? winLit : L"false",
        ctrlLit ? ctrlLit : L"false",
        shiftLit ? shiftLit : L"false",
        altLit ? altLit : L"false",
        key);
    return buf;
}

// YYYY-MM-DD HH:MM (SourceRail history stamp; no seconds).
inline std::wstring WideFormatDateTimeMinuteParts(
    unsigned year, unsigned month, unsigned day,
    unsigned hour, unsigned minute)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u",
        year, month, day, hour, minute);
    return buf;
}

// tiff_pNNNN page stem.
inline std::wstring WideFormatTiffPagePrefix(unsigned pageOneBased)
{
    wchar_t buf[24] = {};
    swprintf_s(buf, L"tiff_p%04u", pageOneBased);
    return buf;
}

// zencrop_pdf_preview_pid_tick
inline std::wstring WideFormatPdfPreviewDirName(unsigned long pid, unsigned long long tick)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"zencrop_pdf_preview_%lu_%llu", pid, tick);
    return buf;
}

// %.1f MP total, %.1f MP max/page
inline std::wstring WideFormatMpEstimate(double totalMp, double maxPerPageMp)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"%.1f MP total, %.1f MP max/page", totalMp, maxPerPageMp);
    return buf;
}

// ocr_virtual_HHMMSS_mmm_seq (no date; product may prefix date)
inline std::wstring WideFormatOcrVirtualStem(
    int hour, int minute, int second, int milliseconds, unsigned seq)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"ocr_virtual_%02d%02d%02d_%03d_%03u",
        hour, minute, second, milliseconds, seq);
    return buf;
}

// name_HHMMSS_mmm_seq.png (prefix product-owned)
inline std::wstring WideFormatTimedSeqPng(
    const wchar_t* prefix,
    int hour, int minute, int second, int milliseconds, unsigned seq)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"%s_%02d%02d%02d_%03d_%03u.png",
        prefix ? prefix : L"item",
        hour, minute, second, milliseconds, seq);
    return buf;
}

// pdf render temp: pdf_pid_tick_counter.pdf
inline std::wstring WideFormatPdfTempName(
    unsigned long pid, unsigned long long tick, unsigned counter)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"pdf_%lu_%llu_%03u.pdf", pid, tick, counter);
    return buf;
}

// True when haystack contains needle (case-sensitive; null-safe).


// ISO-8601 UTC timestamp YYYY-MM-DDTHH:MM:SS.mmmZ (int parts only; no SYSTEMTIME).
inline std::wstring WideFormatIsoUtcTimestamp(
    unsigned year, unsigned month, unsigned day,
    unsigned hour, unsigned minute, unsigned second, unsigned milliseconds)
{
    wchar_t buf[40] = {};
    swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        year, month, day, hour, minute, second, milliseconds);
    return buf;
}

// SettingsDialog-style HTTP status / port labels (compose pure ints + fixed copy).
inline std::wstring WideFormatHttpStatusRejected(int statusCode)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"Endpoint is reachable, but the token was rejected. (HTTP %d)", statusCode);
    return buf;
}

inline std::wstring WideFormatServerRunningOnPort(int port)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"Server is already running on port %d.", port);
    return buf;
}

inline std::wstring WideFormatServerStartedOnPort(int port)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"Server started successfully on port %d.\nIt is now ready for OCR.", port);
    return buf;
}

inline std::wstring WideFormatServerStartFailedOnPort(int port)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"Failed to start llama-server on port %d.\n\n", port);
    return buf;
}

// Debug PATH_TABLE load count (product debug logs).
inline std::wstring WideFormatPathTableLoadedCount(int count)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"[ToolbarIconRenderer] Loaded PATH_TABLE.tsv: %d entries\n", count);
    return buf;
}

inline std::wstring WideFormatMissingCodepoint(unsigned codepoint)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"[ToolbarIconRenderer] Missing codepoint 0x%04X\n", codepoint);
    return buf;
}

// Settings general JSON section (language + showTitlebar bool literal already pure).
inline std::wstring WideFormatGeneralSettingsJson(
    const wchar_t* langStr, const wchar_t* showTitlebarLit)
{
    wchar_t buf[256] = {};
    swprintf_s(buf, L"  \"general\": {\n    \"language\": \"%s\",\n    \"showTitlebar\": %s\n  }",
        langStr ? langStr : L"",
        showTitlebarLit ? showTitlebarLit : L"false");
    return buf;
}

// Overlay settings JSON section.
inline std::wstring WideFormatOverlaySettingsJson(
    const wchar_t* colorHex, int thickness, const wchar_t* cropOnTopLit)
{
    wchar_t buf[256] = {};
    swprintf_s(buf, L"  \"overlay\": {\n    \"color\": \"%s\",\n    \"thickness\": %d,\n    \"cropOnTop\": %s\n  }",
        colorHex ? colorHex : L"#000000",
        thickness,
        cropOnTopLit ? cropOnTopLit : L"false");
    return buf;
}

// Always-on-top settings JSON section.
inline std::wstring WideFormatAotSettingsJson(
    const wchar_t* showBorderLit, const wchar_t* customColorLit,
    const wchar_t* colorHex, int opacity, int thickness,
    const wchar_t* roundedCornersLit, int inset)
{
    wchar_t buf[640] = {};
    swprintf_s(buf,
        L"  \"alwaysOnTop\": {\n    \"showBorder\": %s,\n    \"customColor\": %s,\n"
        L"    \"color\": \"%s\",\n    \"opacity\": %d,\n    \"thickness\": %d,\n"
        L"    \"roundedCorners\": %s,\n    \"inset\": %d\n  }",
        showBorderLit ? showBorderLit : L"false",
        customColorLit ? customColorLit : L"false",
        colorHex ? colorHex : L"#000000",
        opacity,
        thickness,
        roundedCornersLit ? roundedCornersLit : L"false",
        inset);
    return buf;
}

// Official async jobs endpoint reachable message.
inline std::wstring WideFormatHttpJobsEndpointReachable(int statusCode)
{
    wchar_t buf[320] = {};
    swprintf_s(buf,
        L"Official async jobs endpoint is reachable. (HTTP %d)\n\n"
        L"This test does not submit an OCR job; actual OCR will upload an image and poll by jobId.",
        statusCode);
    return buf;
}

// Crop label with product i18n format (expects four %d: left, top, width, height).
inline std::wstring WideFormatCropLabel(
    const wchar_t* format, int left, int top, int width, int height)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, 128, format ? format : L"%d, %d  %d x %d", left, top, width, height);
    return buf;
}


// LayoutEngine debug line: family + threshold profile + two floats.
inline std::wstring WideFormatLayoutEngineDebug(
    const wchar_t* family, const wchar_t* thresholdProfile,
    double textThresh, double tableThresh)
{
    wchar_t buf[320] = {};
    swprintf_s(buf,
        L"[LayoutEngine] family=%s thresholdProfile=%s text=%.2f table=%.2f\n",
        family ? family : L"",
        thresholdProfile ? thresholdProfile : L"",
        textThresh, tableThresh);
    return buf;
}

// Fixed string JSON field with hardcoded provider value (ppocrv6Provider).

// OWN-122: pure count-prefix / queue / page-meta / runtime-key wide formatters.
// "prefixN" e.g. "OCR x5", "Cloud 3", "Q12", "PQ5", "P3"
inline std::wstring WideFormatCountPrefix(const wchar_t* prefix, int count)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"%s%d", prefix ? prefix : L"", count);
    return buf;
}

// "a/b" with unsigned sizes (page progress, etc.)
inline std::wstring WideFormatSlashCountU(unsigned a, unsigned b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%u/%u", a, b);
    return buf;
}

// "Nx" magnification integer label (e.g. 2x, 3x)
inline std::wstring WideFormatTimesInt(int times)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%dx", times);
    return buf;
}

// "image:runtime:<index>:" prefix (caller appends path)
inline std::wstring WideFormatImageRuntimeKeyPrefix(int index)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"image:runtime:%d:", index);
    return buf;
}

// "pdf:runtime:<index>"
inline std::wstring WideFormatPdfRuntimeKey(int index)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"pdf:runtime:%d", index);
    return buf;
}

// ":duplicate:<ordinal>"
inline std::wstring WideFormatDuplicateSuffix(int ordinal)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L":duplicate:%d", ordinal);
    return buf;
}

// "P<N> · <elapsed>" meta suffix
inline std::wstring WideFormatPageMetaSuffix(int pageIndex, const std::wstring& elapsed)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"P%d · %s", pageIndex, elapsed.c_str());
    return buf;
}

// OWN-123: pure paren-slash / thumbnail-gen / hash-page / unsigned-label wide formatters.
// "(a/b)" progress fragment
inline std::wstring WideFormatParenSlashCount(int a, int b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"(%d/%d)", a, b);
    return buf;
}

// "thumbnail.g<generation>."
inline std::wstring WideFormatThumbnailGenPrefix(unsigned long long generation)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"thumbnail.g%llu.", generation);
    return buf;
}

// "<path>#p<pageIndex>"
inline std::wstring WideFormatPathHashPage(const std::wstring& path, int pageIndex)
{
    wchar_t pageBuf[32] = {};
    swprintf_s(pageBuf, L"#p%d", pageIndex);
    return path + pageBuf;
}

// unsigned long long as decimal string
inline std::wstring WideFormatUll(unsigned long long value)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"%llu", value);
    return buf;
}

// " / Page N" suffix
inline std::wstring WideFormatPageSlashLabel(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L" / Page %d", pageIndex);
    return buf;
}

// OWN-124: pure wide format helpers (no HWND; dual-write only).

// "page_<1-based>:block_<order>"
inline std::wstring WideFormatPageBlockId(int pageOneBased, int order)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"page_%d:block_%d", pageOneBased, order);
    return buf;
}

// "page_1:bbox_<n>"
inline std::wstring WideFormatPageBboxId(int n)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"page_1:bbox_%d", n);
    return buf;
}

// "page_1:layout_<n>:asset"
inline std::wstring WideFormatPageLayoutAssetId(int regionOneBased)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"page_1:layout_%d:asset", regionOneBased);
    return buf;
}

// "left,top - right,bottom"
inline std::wstring WideFormatBboxLtrb(long left, long top, long right, long bottom)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"%ld,%ld - %ld,%ld", left, top, right, bottom);
    return buf;
}

// ".tmp.<pid>.<tick>"
inline std::wstring WideFormatTmpPidTick(unsigned long pid, unsigned long long tick)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L".tmp.%lu.%llu", pid, tick);
    return buf;
}

// "#N" history/index label (1-based display)
inline std::wstring WideFormatHashIndex(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"#%d", indexOneBased);
    return buf;
}

// "  \"key\": N,\r\n" (2-space indent JSON int field line)

// "  \"key\": N,\r\n" for size_t-ish unsigned long long

// "\"key\":N" compact JSON int field (no spaces/indent)

// "\"key\":N" compact for unsigned long long

// OWN-125: pure source-rail / page-key / size-key / middot-slash wide formatters.

// ":page:<n>" stable source key suffix
inline std::wstring WideFormatColonPageKey(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L":page:%d", pageIndex);
    return buf;
}

// "\nWxH" thumbnail cache key suffix (path + "\n" + WxH)
inline std::wstring WideFormatThumbSizeSuffix(int width, int height)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"\n%dx%d", width, height);
    return buf;
}

// " · a/b" middot slash progress fragment
inline std::wstring WideFormatMiddotSlashCount(int a, int b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L" \x00b7 %d/%d", a, b);
    return buf;
}

// "P<n> · <elapsed>" live page meta (elapsed may contain middle-dot text)
inline std::wstring WideFormatPageMetaLive(int pageOneBased, const std::wstring& elapsed)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"P%d \x00b7 ", pageOneBased);
    return std::wstring(buf) + elapsed;
}

// "Image N" / "PDF N" / "Capture N" titled index labels
inline std::wstring WideFormatImageTitle(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"Image %d", indexOneBased);
    return buf;
}

inline std::wstring WideFormatPdfTitle(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"PDF %d", indexOneBased);
    return buf;
}

inline std::wstring WideFormatCaptureTitle(int indexOneBased)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"Capture %d", indexOneBased);
    return buf;
}

// "a/b | " status prefix used by source-rail paint
inline std::wstring WideFormatSlashCountBar(int a, int b)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%d/%d | ", a, b);
    return buf;
}

// "/" + count fragment (filtered header "visible/total")
inline std::wstring WideFormatSlashTotal(int total)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"/%d", total);
    return buf;
}

// "p.N" page-dot label (preview strip)
inline std::wstring WideFormatPageDotLabel(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"p.%d", pageIndex);
    return buf;
}

// "PDF p.N" selection page label
inline std::wstring WideFormatPdfPageDotLabel(int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"PDF p.%d", pageIndex);
    return buf;
}

// "prefixA/B" e.g. "D3/10" drop progress fingerprint
inline std::wstring WideFormatPrefixSlashCount(const wchar_t* prefix, int a, int b)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%s%d/%d", prefix ? prefix : L"", a, b);
    return buf;
}

// "EX:<id>:" external progress fingerprint fragment
inline std::wstring WideFormatExProgressPrefix(int progressId)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"EX:%d:", progressId);
    return buf;
}

// "OCR:<key>:<page>|" current OCR fingerprint fragment (key may be empty)
inline std::wstring WideFormatOcrFpSuffix(const std::wstring& stableKey, int pageIndex)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L":%d|", pageIndex);
    return L"OCR:" + stableKey + buf;
}

// " (N)" parenthesized int (Win32 error suffix, counts)
inline std::wstring WideFormatParenInt(int value)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L" (%d)", value);
    return buf;
}

// ".<kind>.<pid>.<tick>" temp/candidate/backup path fragment
inline std::wstring WideFormatDotKindPidTick(
    const wchar_t* kind, unsigned long pid, unsigned long long tick)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L".%s.%lu.%llu", kind ? kind : L"", pid, tick);
    return buf;
}

// ".candidate.<pid>.<tick>"
inline std::wstring WideFormatCandidatePidTick(unsigned long pid, unsigned long long tick)
{
    return WideFormatDotKindPidTick(L"candidate", pid, tick);
}

// ".backup.<pid>.<tick>"
inline std::wstring WideFormatBackupPidTick(unsigned long pid, unsigned long long tick)
{
    return WideFormatDotKindPidTick(L"backup", pid, tick);
}

// OWN-126: pure provider-asset / page-warn / offset-error wide formatters.

// "page_<n>:provider_asset_<order>"
inline std::wstring WideFormatPageProviderAssetId(int pageNumber, int localOrder)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"page_%d:provider_asset_%d", pageNumber, localOrder);
    return buf;
}

// "page_<n>:<kind>_<order>" generic page-kind-order id
inline std::wstring WideFormatPageKindOrderId(
    int pageNumber, const wchar_t* kind, int order)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"page_%d:%s_%d", pageNumber, kind ? kind : L"", order);
    return buf;
}

// "zencrop-asset://provider/page_<n>/asset_<order>"
inline std::wstring WideFormatProviderAssetUri(int pageNumber, int localOrder)
{
    wchar_t buf[128] = {};
    swprintf_s(buf, L"zencrop-asset://provider/page_%d/asset_%d", pageNumber, localOrder);
    return buf;
}

// "Page <n>: " warning prefix
inline std::wstring WideFormatPageWarnPrefix(int pageNumber)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"Page %d: ", pageNumber);
    return buf;
}

// " at UTF-16 offset <n>." JSONL error suffix
inline std::wstring WideFormatUtf16OffsetSuffix(unsigned long long offset)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L" at UTF-16 offset %llu.", offset);
    return buf;
}

// "<prefix><n>." int-suffixed sentence end (error tails)
inline std::wstring WideFormatIntDotSuffix(const wchar_t* prefix, int value)
{
    wchar_t buf[160] = {};
    swprintf_s(buf, L"%s%d.", prefix ? prefix : L"", value);
    return buf;
}

// "<prefix><n>: <detail>" int-midfix with trailing detail
inline std::wstring WideFormatIntColonDetail(
    const wchar_t* prefix, int value, const std::wstring& detail)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"%s%d: ", prefix ? prefix : L"", value);
    return std::wstring(buf) + detail;
}

// "page_<n>" bare page id fragment
inline std::wstring WideFormatPageId(int pageNumber)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"page_%d", pageNumber);
    return buf;
}

// "asset_<n>" bare asset id fragment
inline std::wstring WideFormatAssetId(int order)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"asset_%d", order);
    return buf;
}

// "provider_asset_<n>" provider local asset fragment
inline std::wstring WideFormatProviderAssetLocal(int order)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"provider_asset_%d", order);
    return buf;
}

// ".<pid>.<tick>.<counter><suffix>" temp sibling path tail
inline std::wstring WideFormatPidTickCounterSuffix(
    unsigned long pid, unsigned long long tick, unsigned counter, const wchar_t* suffix)
{
    wchar_t buf[160] = {};
    swprintf_s(
        buf,
        L".%lu.%llu.%u%s",
        pid,
        tick,
        counter,
        suffix ? suffix : L".tmp");
    return buf;
}

// OWN-127: pure block-id / page-prefix / ann / function-key / http / json-index formatters.

// "block_<n>" bare block id fragment
inline std::wstring WideFormatBlockId(int order)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"block_%d", order);
    return buf;
}

// "page_<n>:" page prefix for id composition
inline std::wstring WideFormatPagePrefix(int pageNumber)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"page_%d:", pageNumber);
    return buf;
}

// "ann_<n>" annotation id
inline std::wstring WideFormatAnnIdPlain(unsigned long long id)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"ann_%llu", id);
    return buf;
}

// "legacy_<n>" legacy annotation id
inline std::wstring WideFormatLegacyId(int index)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"legacy_%d", index);
    return buf;
}

// "F<n>" function key label (F1..)
inline std::wstring WideFormatFunctionKey(int n)
{
    wchar_t buf[16] = {};
    swprintf_s(buf, L"F%d", n);
    return buf;
}

// "HTTP <n>" http status fragment
inline std::wstring WideFormatHttpStatus(int statusCode)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"HTTP %d", statusCode);
    return buf;
}

// "{\"index\":N" compact json index field open
inline std::wstring WideFormatJsonIndexOpen(int index)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"{\"index\":%d", index);
    return buf;
}

// "x,y" point pair for migration serialization
inline std::wstring WideFormatPointXy(int x, int y)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%d,%d", x, y);
    return buf;
}

// "Failed to create job directory (N)."
inline std::wstring WideFormatJobDirError(unsigned long err)
{
    wchar_t buf[96] = {};
    swprintf_s(buf, L"Failed to create job directory (%lu).", err);
    return buf;
}

// "Endpoint returned HTTP N" dialog message prefix
inline std::wstring WideFormatEndpointHttp(int statusCode)
{
    wchar_t buf[80] = {};
    swprintf_s(buf, L"Endpoint returned HTTP %d", statusCode);
    return buf;
}

// "group_<n>" region group id
inline std::wstring WideFormatGroupId(int order)
{
    wchar_t buf[48] = {};
    swprintf_s(buf, L"group_%d", order);
    return buf;
}

// "N group(s) failed." failed groups suffix
inline std::wstring WideFormatGroupsFailedSuffix(int failedGroups)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%d group(s) failed.", failedGroups);
    return buf;
}

// "📌 #<n>  |  " history paint header prefix
inline std::wstring WideFormatHistoryPinHeader(int oneBasedIndex)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"\U0001F4CC #%d  |  ", oneBasedIndex);
    return buf;
}
