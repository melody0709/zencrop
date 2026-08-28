#pragma once
#include "ocr/ui/DashboardModels.h"
#include <set>
#include <string>
#include <vector>

// Stage 1 D-C-3: history / dismissed-manifest disk repository.
// Paths are injectable for tests; production uses exe-directory defaults.

class DashboardHistoryRepository {
public:
    DashboardHistoryRepository(std::wstring historyPath, std::wstring dismissedPath);

    static DashboardHistoryRepository ForDefaultLocation();
    static std::wstring DefaultHistoryPath();
    static std::wstring DefaultDismissedPath();

    const std::wstring& historyPath() const { return m_historyPath; }
    const std::wstring& dismissedPath() const { return m_dismissedPath; }

    // Same semantics as legacy LoadHistoryItemsFromDisk / SaveHistoryItemsToDisk.
    bool LoadItems(std::vector<OcrDashboardHistoryItem>& items) const;
    bool SaveItems(const std::vector<OcrDashboardHistoryItem>& items) const;

    bool LoadDismissedKeys(std::set<std::wstring>& keys) const;
    bool SaveDismissedKeys(const std::set<std::wstring>& keys) const;

private:
    std::wstring m_historyPath;
    std::wstring m_dismissedPath;
};
