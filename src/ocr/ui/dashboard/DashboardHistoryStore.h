#pragma once
#include "ocr/ui/DashboardModels.h"
#include <set>
#include <string>
#include <vector>

// Stage 1 D-C: pure history JSON / dismissal-key parsing (no Window / no HWND).

bool DashboardHistoryIsEmptyJson(const std::wstring& json);
bool DashboardHistoryIsStructurallyCompleteJson(const std::wstring& json);
std::vector<OcrDashboardHistoryItem> DashboardHistoryParseJson(const std::wstring& json);
std::wstring DashboardHistorySerializeJson(const std::vector<OcrDashboardHistoryItem>& items);

std::wstring DashboardHistorySerializeDismissedManifests(const std::set<std::wstring>& manifestKeys);
bool DashboardHistoryParseDismissedManifestKeys(const std::wstring& json, std::set<std::wstring>& manifestKeys);

// Path / dismissal key helpers (pure string transforms).
std::wstring DashboardHistoryNormalizePath(std::wstring path);
std::wstring DashboardHistoryDismissalBaseKey(const std::wstring& manifestPath);
std::wstring DashboardHistoryNormalizeDismissalKey(std::wstring key);
std::wstring DashboardHistoryBuildImageDismissalKey(
    const std::wstring& manifestPath,
    const std::wstring& sourceInstanceId,
    const std::wstring& createdAt,
    const std::wstring& sourcePath);
std::wstring DashboardHistoryBuildPdfDismissalKey(
    const std::wstring& manifestPath,
    const std::wstring& createdAt,
    const std::wstring& sourcePath);
std::wstring DashboardHistoryBuildHistoryItemDismissalKey(
    const std::wstring& originManifestPath,
    const std::wstring& sourceInstanceId);

// D-C-S4: pure dismissed-key membership (primary key, else legacy path-wide base key).
bool DashboardHistoryIsDismissalKeyPresent(
    const std::vector<std::wstring>& dismissedKeys,
    const std::wstring& key);

// True when primary image/pdf dismissal key or its legacy base key is present.
bool DashboardHistoryIsImageJobDismissed(
    const std::vector<std::wstring>& dismissedKeys,
    const std::wstring& manifestPath,
    const std::wstring& sourceInstanceId,
    const std::wstring& createdAt,
    const std::wstring& sourcePath);

bool DashboardHistoryIsPdfJobDismissed(
    const std::vector<std::wstring>& dismissedKeys,
    const std::wstring& manifestPath,
    const std::wstring& createdAt,
    const std::wstring& sourcePath);
