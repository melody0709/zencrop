#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardFileTypes.h"
#include "dashboard/DashboardOleDropTarget.h"
#include "dashboard/DashboardPdfPasswordDialog.h"
#include "dashboard/DashboardDialogLayout.h"
#include "dashboard/DashboardFolderImportOptionsDialog.h"
#include "dashboard/DashboardOutputArtifactOptionsDialog.h"
#include "dashboard/DashboardPdfOptionsDialog.h"
#include "dashboard/DashboardHistoryStore.h"
#include "dashboard/DashboardController.h"
#include "core/AppDataPaths.h"
#include "core/WideStringUtils.h"
#include "AlwaysOnTop.h"
#include "Settings.h"
#include "Strings.h"
#include "OcrUtils.h"
#include "OcrBlockPresentation.h"
#include "ocr/LocalRaster.h"
#include "OcrBlockJson.h"
#include "OcrEngine.h"
#include "BatchOcrManifest.h"
#include "BatchOcrWriter.h"
#include "BatchOcrImageLinks.h"
#include "PageRange.h"
#include "PdfPageRenderer.h"
#include "ocr/document/PaddleCloudDocumentProtocol.h"
#include "ocr/document/PaddleCloudDocumentTransport.h"
#include "ocr/batch/PaddleCloudDocumentMaterializer.h"
#include "image/BitmapCodec.h"
// Stage3 3-A-3: dead ScreenshotUtils include deleted (ocr_ui↛screenshot reverse).
#include "AppMessages.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardHostTypes.h"
#include "dashboard/DashboardHostInternals.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <wincodec.h>  // WIC for WebP support
#include "JsonUtils.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <cmath>
#include <atomic>
#include <thread>
#include <utility>
#include <functional>
#include <limits>
#include <new>
#include <mutex>
#include <chrono>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")  // WIC for WebP support

// Modern Dark Theme Colors (VS Code Dark+ inspired)
// D-I-1: Theme colors live in dashboard/DashboardTheme.h (shared multi-TU).

// D-I-3: DPI/font free helpers live in DashboardHostUtils.h
// D-I-4: kDashboardRecentOutputRootLimit in HostInternals

// D-I-2: MeasureButtonWidth → DashboardMeasureButtonWidth (DashboardDialogLayout.h).
int MeasureButtonWidth(HWND hwnd, HFONT font, int minWidth, int horizontalPadding) {
    return DashboardMeasureButtonWidth(hwnd, font, minWidth, horizontalPadding);
}

// D-I-3: WM_*/TIMER_*/ID_* live in dashboard/DashboardHostIds.h

#define ID_DASH_COPY 1001
#define ID_DASH_CLEAR 1002
#define ID_DASH_CLOSE 1003
#define ID_DASH_SEARCH 1004
#define ID_DASH_PREVIEW 1005
#define ID_DASH_SOURCE 1006
#define ID_DASH_PREV_RECORD 1007
#define ID_DASH_NEXT_RECORD 1008
#define ID_DASH_SOURCE_LIST 1009
#define ID_DASH_IMPORT 1010
#define ID_DASH_TEXT 1011
#define ID_DASH_JSON 1012
#define ID_DASH_OPEN_OUTPUT 1013
#define ID_DASH_RETRY_FAILED 1014
#define ID_DASH_PAUSE_BATCH 1017
#define ID_DASH_OCR_MODE 1019
#define ID_DASH_OUTPUT_FOLDER 1020
#define ID_DASH_LANG_TOGGLE 1023 // P1.5: 运行时语言切换
#define ID_DASH_SOURCE_PANEL_TOGGLE 1024
#define ID_DASH_RESULT_PANEL_TOGGLE 1025
#define ID_DASH_SOURCE_SORT 1026
#define ID_DASH_MINIMIZE 1029
#define ID_DASH_MAXIMIZE 1030

#define ID_SOURCE_CTX_COPY 1301
#define ID_SOURCE_CTX_OPEN_OUTPUT 1302
#define ID_SOURCE_CTX_RETRY_FAILED 1303
#define ID_SOURCE_CTX_DELETE 1304
#define ID_SOURCE_CTX_REVEAL 1305
#define ID_SOURCE_CTX_PAUSE_ITEM 1306
#define ID_SOURCE_CTX_RERUN_ITEM 1307
#define ID_SOURCE_SORT_NEWEST 1311
#define ID_SOURCE_SORT_OLDEST 1312

// D-B-11: ID_PDF_OPTIONS_* moved into DashboardPdfOptionsDialog.cpp
// D-B-10: ID_OUTPUT_OPTIONS_* moved into DashboardOutputArtifactOptionsDialog.cpp
// D-B-9: ID_FOLDER_OPTIONS_* moved into DashboardFolderImportOptionsDialog.cpp
// D-B-8: ID_PDF_PASSWORD_* moved into DashboardPdfPasswordDialog.cpp
// D-B-7: DashboardOleDropTarget extracted to dashboard/DashboardOleDropTarget.{h,cpp}.

// D-I-4: result/async types + Post/Run helpers live in DashboardHostTypes.h

// D-B-11: PdfImportPreflightInfo + PdfOptionsDialogState moved to DashboardPdfOptionsDialog.*

// D-B-9: FolderImportOptionsDialogState moved to DashboardFolderImportOptionsDialog.cpp

// D-B-10: OutputArtifactOptionsDialogState moved to DashboardOutputArtifactOptionsDialog.cpp

// D-I-3: generation counter free as DashboardNextHostGeneration (HostUtils).

// D-B-11: kPdfOptionsDialogClass moved with PDF options dialog TU
// D-B-8: kPdfPasswordDialogClass moved with password dialog TU
// D-B-9: kFolderImportOptionsDialogClass moved with folder dialog TU
// D-B-10: kOutputArtifactOptionsDialogClass moved with artifact dialog TU
// D-I-4: kFolderImportDefaultMaxDepth in HostInternals
// D-B-9: kFolderImportMaxDepthLimit lives in folder dialog TU / DashboardFileTypes.

// D-I-4: DrawImageThumbnail free in HostInternals/StateAndHelpers
// D-I-4: DrawThumbnailPlaceholder free in HostInternals/StateAndHelpers
// D-B-8: password dialog helpers moved to DashboardPdfPasswordDialog.cpp
// D-B-11: LayoutPdfOptionsDialog moved with PDF options dialog TU
// D-B-10: LayoutOutputArtifactOptionsDialog moved with artifact dialog TU

// OWN-71/D-I-3: remaining thin wrappers (names differ from free Dashboard* APIs).
std::vector<std::wstring> SplitFolderExcludePatterns(const std::wstring& patterns) {
    return DashboardSplitFolderExcludePatterns(patterns);
}

bool FolderExcludePatternMatches(
    const std::wstring& pattern,
    const std::wstring& dirName,
    const std::wstring& fullPath)
{
    if (pattern.empty()) return false;
    // OWN-72: pure name-equality subset first; PathMatchSpec remains Win32 product.
    if (DashboardFolderExcludeNameEquals(pattern, dirName)) return true;
    if (PathMatchSpecW(dirName.c_str(), pattern.c_str())) return true;
    return !fullPath.empty() && PathMatchSpecW(fullPath.c_str(), pattern.c_str());
}

bool IsAllPageRangeText(const std::wstring& pageRange) {
    return DashboardIsAllPageRangeText(pageRange);
}

// OWN-72 thin wrappers → DashboardFileTypes pure helpers.
std::wstring TrimPreviewStem(std::wstring value) {
    return DashboardTrimPreviewStem(std::move(value));
}

std::wstring SanitizePreviewPathSegment(const std::wstring& input) {
    return DashboardSanitizePreviewPathSegment(input);
}

std::wstring PdfPreviewStem(const std::wstring& path) {
    return DashboardPdfPreviewStem(path);
}

std::wstring AppendPreviewDuplicateSuffix(const std::wstring& base, int suffix) {
    return DashboardAppendPreviewDuplicateSuffix(base, suffix);
}

std::vector<std::wstring> PreviewPdfOutputFolderNames(
    const std::wstring& outputRoot,
    const std::vector<PdfImportPreflightInfo>* preflight)
{
    std::vector<std::wstring> names;
    if (!preflight) return names;

    std::set<std::wstring> reservedLower;
    names.reserve(preflight->size());
    for (size_t i = 0; i < preflight->size(); i++) {
        std::wstring base = PdfPreviewStem((*preflight)[i].path);
        std::wstring selected = base;
        for (int suffix = 1; suffix < 1000; suffix++) {
            std::wstring candidate = AppendPreviewDuplicateSuffix(base, suffix);
            // OWN-119: pure path join (WideStringUtils).
            std::wstring full = outputRoot.empty() ? candidate : WideJoinPath(outputRoot, candidate);
            std::wstring lower = full;
            lower = WideToLower(std::move(lower)); // OWN-79
            if (!DashboardDirectoryExistsWide(full) && reservedLower.insert(lower).second) {
                selected = candidate;
                break;
            }
        }
        names.push_back(selected);
    }
    return names;
}

// OCR mode pure helpers live in DashboardFileTypes; call sites use Dashboard* APIs.
void PopulateOcrModeCombo(
    HWND combo,
    const std::wstring& selectedMode,
    bool includeDashboardPrefix)
{
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
        includeDashboardPrefix ? L"OCR: Windows OCR" : L"Windows OCR"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
        includeDashboardPrefix ? L"OCR: PaddleOCR Cloud" : L"PaddleOCR Cloud"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
        includeDashboardPrefix ? L"OCR: PaddleOCR-VL 1.6 Local" : L"PaddleOCR-VL 1.6 Local"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(
        includeDashboardPrefix ? L"OCR: PP-OCRv6 Local" : L"PP-OCRv6 Local"));
    SendMessageW(combo, CB_SETCURSEL, DashboardOcrModeToComboIndex(selectedMode), 0);
}

OcrDashboardWindow* OcrDashboardWindow::s_instance = nullptr;

const wchar_t* OcrDashboardWindow::ClassName = L"ZenCrop.OcrDashboard";
const wchar_t* OcrDashboardWindow::ImageAreaClassName = L"ZenCrop.OcrDashboard.ImageArea";
const wchar_t* OcrDashboardWindow::SourceRailClassName = L"ZenCrop.OcrDashboard.SourceRail";
const wchar_t* OcrDashboardWindow::SplitterTrackerClassName = L"ZenCrop.OcrDashboard.SplitterTracker";
const wchar_t* OcrDashboardWindow::SplitterHitClassName = L"ZenCrop.OcrDashboard.SplitterHit";

std::wstring GetWindowPositionFilePath() {
    return ZenCropAppDataFilePath(L"ocr_dashboard_pos.ini");
}

std::wstring JoinDashboardWideList(const std::vector<std::wstring>& values, wchar_t separator) {
    std::wstring result;
    for (const auto& value : values) {
        if (value.empty()) continue;
        if (!result.empty()) result.push_back(separator);
        result += value;
    }
    return result;
}

std::vector<std::wstring> SplitDashboardWideList(const std::wstring& value, wchar_t separator) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(separator, start);
        std::wstring part = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!part.empty()) parts.push_back(std::move(part));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return parts;
}

std::wstring DashboardPdfPagePauseKeyFromJob(const BatchOcrPdfJob& job, int pageIndex) {
    std::wstring jobKey = DashboardPdfJobTreeKey(job);
    if (jobKey.empty() || pageIndex <= 0) return L"";
    // OWN-123: pure int labels (WideStringUtils).
    return jobKey + L"#page:" + WideFormatIntLabel(pageIndex);
}


// Keep this dashboard as one translation unit while splitting the large implementation into navigable sections.
// D-I-4: StateAndHelpers is a real TU (OcrDashboardWindow.StateAndHelpers.cpp).
// D-I-3: EntryPoints is a real TU (OcrDashboardWindow.EntryPoints.cpp).
// Stage 1 D-A: test harness lives under tests/support (not production).
// Contract tests pass -I tests/support and define ZENCROP_DASHBOARD_WINDOW_TESTS.
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
#include "OcrDashboardWindow.Tests.inl"
#endif
// D-I-3: Lifecycle is a real TU (OcrDashboardWindow.Lifecycle.cpp).
// D-I-2: Layout is a real TU (OcrDashboardWindow.Layout.cpp).
// D-I-4: SourceRail is a real TU (OcrDashboardWindow.SourceRail.cpp).
// D-I-4: ImagePreview is a real TU (OcrDashboardWindow.ImagePreview.cpp).
// D-I-3: Blocks is a real TU (OcrDashboardWindow.Blocks.cpp).
// D-I-3: Import is a real TU (OcrDashboardWindow.Import.cpp).
// D-I-4: Batch is a real TU (OcrDashboardWindow.Batch.cpp).
// D-I-4: Messages is a real TU (OcrDashboardWindow.Messages.cpp).
// D-I-1: HistoryPaint is a real TU (OcrDashboardWindow.HistoryPaint.cpp).
