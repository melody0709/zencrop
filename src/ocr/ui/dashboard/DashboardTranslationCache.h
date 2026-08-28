#pragma once

#include "core/Settings.h"
#include "translation/TranslationTypes.h"

#include <string>
#include <vector>

struct DashboardTranslationCacheEntry {
    std::wstring key;
    std::wstring sourceRevisionSha256;
    std::vector<translation::TranslationSegment> translations;
};

bool DashboardTranslationCacheBuildKey(
    const std::wstring& canonicalSourceMarkdown,
    const TranslationSettings& settings,
    std::wstring& key,
    std::wstring& sourceRevisionSha256,
    std::wstring& error);

bool DashboardTranslationCacheLoad(
    const std::wstring& key,
    const std::wstring& sourceRevisionSha256,
    DashboardTranslationCacheEntry& entry);

bool DashboardTranslationCacheSave(
    const DashboardTranslationCacheEntry& entry,
    std::wstring& error);
