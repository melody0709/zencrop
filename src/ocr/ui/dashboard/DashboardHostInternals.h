#pragma once

// D-I-4: free Host internals shared across multi-TU OcrDashboardWindow sections.
// Definitions live primarily in OcrDashboardWindow.StateAndHelpers.cpp.

#include "OcrDashboardWindow.h"
#include "dashboard/DashboardHostTypes.h"
#include "dashboard/DashboardTheme.h"
#include "BatchOcrTypes.h"
#include "image/BitmapCodec.h"

#include <functional>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <vector>
#include <wincodec.h>
#include <windows.h>

// Image load / TIFF expand
Gdiplus::Bitmap* LoadImageWithWIC(const std::wstring& filePath);
bool IsTiffImageFilePath(const std::wstring& filePath);
bool WriteWicBitmapSourceAsPng(
    IWICImagingFactory* factory,
    IWICBitmapSource* source,
    const std::wstring& destPath,
    std::wstring& error);
bool TryExpandMultiPageTiffToCache(
    const std::wstring& sourcePath,
    std::vector<std::wstring>& framePaths,
    bool& expanded,
    std::wstring& error);

// GDI helpers
Gdiplus::Color ToGpColor(COLORREF color, BYTE alpha = 255);
Gdiplus::Bitmap* LoadDashboardBitmapDetached(const std::wstring& filePath);

// Source-rail thumbnail cache
void InvalidateCachedSourceRailThumbnailPath(const std::wstring& imagePath);
std::shared_ptr<Gdiplus::Bitmap> GetCachedSourceRailThumbnail(
    const std::wstring& imagePath,
    int targetW,
    int targetH,
    bool allowDecode = true);
bool QueueSourceRailThumbnailDecode(
    const std::shared_ptr<DashboardAsyncDispatchState>& dispatchState,
    const std::wstring& imagePath,
    int targetW,
    int targetH);
bool DrawImageThumbnail(
    HDC hdc,
    const std::wstring& imagePath,
    const RECT& rc,
    COLORREF borderColor,
    bool allowDecode = true);
void DrawThumbnailPlaceholder(HDC hdc, const RECT& rc, COLORREF borderColor, COLORREF textColor);

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
size_t GetSourceRailThumbnailCacheEntryCountForTests();
void ClearSourceRailThumbnailCacheForTests();
#endif

// Folder scan
bool FolderExcludePatternMatches(
    const std::wstring& pattern,
    const std::wstring& dirName,
    const std::wstring& fullPath);
bool ShouldSkipScanDirectory(
    const WIN32_FIND_DATAW& fd,
    const std::wstring& fullPath,
    const std::vector<std::wstring>& excludePatterns);
bool CollectImageFilesRecursive(
    const std::wstring& dir,
    const std::function<bool(const std::wstring&)>& isSupportedImage,
    std::vector<std::wstring>& out,
    int maxDepth,
    const std::vector<std::wstring>& excludePatterns,
    int depth = 0);

// Labels / search / keys (thin Host wrappers over pure helpers)
std::wstring FormatElapsedShort(DWORD elapsedMs);
std::wstring BatchTaskStatusLabel(BatchOcrTaskStatus status);
COLORREF BatchTaskStatusColor(BatchOcrTaskStatus status);
BatchOcrTaskStatus DashboardSummarizePdfJobStatus(const BatchOcrPdfJob& job);
void AppendDashboardSearchField(std::wstring& blob, const std::wstring& value);
void AppendDashboardSearchField(std::wstring& blob, const wchar_t* value);
bool DashboardSearchBlobMatches(std::wstring blob, const std::wstring& needleLower);
bool DashboardBatchTaskMatchesFilter(
    const DashboardBatchTaskItem& task,
    const std::wstring& needleLower);
bool DashboardPdfJobMatchesFilter(
    const BatchOcrPdfJob& job,
    const std::wstring& needleLower);
bool DashboardPdfPageMatchesFilter(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page,
    const std::wstring& needleLower);
DashboardItemKey MakeHistorySourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    int historyIndex);
int HistoryIndexFromSourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const DashboardItemKey& key);

// Window-side free helpers (defs in OcrDashboardWindow.cpp or HostUtils)
inline constexpr int kFolderImportDefaultMaxDepth = 16;
inline constexpr size_t kDashboardRecentOutputRootLimit = 5;

std::vector<std::wstring> SplitFolderExcludePatterns(const std::wstring& patterns);
std::wstring GetWindowPositionFilePath();
std::wstring JoinDashboardWideList(const std::vector<std::wstring>& values, wchar_t separator);
std::vector<std::wstring> SplitDashboardWideList(const std::wstring& value, wchar_t separator);
bool DashboardSourceRailRowIsBatch(const DashboardSourceRailSelectableRow& row);
void DeleteDashboardPdfCoverCandidateIfOwned(const DashboardPdfCoverResult& result);
std::wstring DashboardPdfPagePauseKeyFromJob(const BatchOcrPdfJob& job, int pageIndex);
void PopulateOcrModeCombo(
    HWND combo,
    const std::wstring& selectedMode,
    bool includeDashboardPrefix = false);
