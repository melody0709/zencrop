#pragma once

#include "LayoutEngine.h"
#include "OcrUtils.h"
#include "Settings.h"
#include "core/WideStringUtils.h"

#include <cstddef>
#include <string>

inline bool PaddleDocShouldIgnoreRegionInMarkdown(
    const LayoutRegion& region,
    const OcrSettings& settings)
{
    if ((region.classId == 10 || region.classId == 24) && !settings.docKeepFootnotes) {
        return true;
    }
    const auto* classInfo = GetLayoutClassInfo(region.classId);
    return classInfo && classInfo->ignoreInMarkdown && settings.docIgnorePageDecorations;
}

inline bool PaddleDocShouldSkipVlmForRegion(
    const LayoutRegion& region,
    const OcrSettings& settings)
{
    if (region.className == L"chart") return !settings.docRecognizeCharts;
    if (region.className == L"image" || region.className == L"header_image" ||
        region.className == L"footer_image") {
        return !settings.docRecognizeImages;
    }
    if (region.className == L"seal") return !settings.docRecognizeSeals;
    // Page decorations, content containers, numbers and footnotes are valid
    // recognition inputs. Markdown policy is applied only after recognition.
    return false;
}

inline bool PaddleDocLayoutCacheNeedsReload(
    bool engineAvailable,
    const std::wstring& cachedPath,
    const std::wstring& cachedFamilySetting,
    const std::wstring& requestedPath,
    const std::wstring& requestedFamilySetting)
{
    return !engineAvailable || cachedPath != requestedPath ||
        cachedFamilySetting != requestedFamilySetting;
}

inline bool PaddleDocRecognitionPageSucceeded(size_t failedGroups) {
    return failedGroups == 0;
}

inline std::wstring PaddleDocRecognitionFailureError(size_t failedGroups) {
    if (PaddleDocRecognitionPageSucceeded(failedGroups)) return L"";
    // OWN-127: pure groups-failed suffix (WideStringUtils).
    return L"Document recognition incomplete: " +
        WideFormatGroupsFailedSuffix(failedGroups);
}
