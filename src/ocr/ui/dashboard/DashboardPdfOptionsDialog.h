#pragma once

// D-B-11: PDF import options dialog + preflight/validate/preview helpers.
// Extracted from OcrDashboardWindow.cpp (Stage1 D-B).

#include "BatchOcrTypes.h"
#include "PdfPageRenderer.h"
#include "PdfRenderOptions.h"
#include "ocr/ui/dashboard/DashboardState.h"  // DashboardPdfCloudRiskPolicy

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

// Per-import PDF options snapshot. Owned by dialog domain (D-B-R2); Window seeds/applies.
struct DashboardPdfImportOptions {
    std::wstring pageRange = L"all";
    int pdfRenderDpi = kDefaultPdfRenderDpi;
    uint32_t pdfMaxPixelEdge = kDefaultPdfMaxPixelEdge;
    uint32_t pdfMaxMegapixels = kDefaultPdfMaxMegapixels;
    PdfRenderImageFormat pdfImageFormat = PdfRenderImageFormat::Auto;
    int pdfImageQuality = kDefaultPdfImageQuality;
    // Per-import snapshot. Starts from Dashboard defaults; frozen into PDF job on import start.
    OcrOutputArtifactOptions outputArtifacts;
    std::wstring ocrMode;
    std::vector<std::wstring> pdfPasswords;
    std::vector<int> pdfPageCounts;
    std::vector<bool> pdfRequiresPasswords;
    bool cloudFullPdfConsentGranted = false;
    bool rememberCloudFullPdfConsent = false;
};

struct PdfImportPreflightInfo {
    std::wstring path;
    int pageCount = 0;
    bool requiresPassword = false;
    std::wstring password;
    std::wstring error;
    std::vector<PdfPreflightPageInfo> pages;
};

struct PdfOptionsDialogState {
    HWND hwnd = nullptr;
    HWND titleText = nullptr;
    HWND countText = nullptr;
    HWND previewTitle = nullptr;
    HWND rangeLabel = nullptr;
    HWND rangeEdit = nullptr;
    HWND dpiLabel = nullptr;
    HWND dpiEdit = nullptr;
    HWND maxEdgeLabel = nullptr;
    HWND maxEdgeEdit = nullptr;
    HWND maxMpLabel = nullptr;
    HWND maxMpEdit = nullptr;
    HWND formatLabel = nullptr;
    HWND formatCombo = nullptr;
    HWND qualityLabel = nullptr;
    HWND qualityEdit = nullptr;
    HWND modeLabel = nullptr;
    HWND modeCombo = nullptr;
    HWND cloudConsentCheck = nullptr;
    HWND artifactsLabel = nullptr;
    HWND artifactsSummary = nullptr;
    HWND artifactsChangeBtn = nullptr;
    HWND selectedText = nullptr;
    HWND estimateLabel = nullptr;
    HWND estimateText = nullptr;
    HWND outputLabel = nullptr;
    HWND outputText = nullptr;
    HWND outputBrowseBtn = nullptr;
    HWND outputTreeText = nullptr;
    HWND previewCaptionText = nullptr;
    HWND previewPrevBtn = nullptr;
    HWND previewNextBtn = nullptr;
    HWND previewIndexText = nullptr;
    HWND resetDefaultsBtn = nullptr;
    HWND saveSettingsBtn = nullptr;
    HWND okBtn = nullptr;
    HWND cancelBtn = nullptr;
    HFONT font = nullptr;
    bool ownsFont = false;
    DashboardPdfImportOptions* options = nullptr;
    std::function<bool(OcrOutputArtifactOptions&)> editOutputArtifacts;
    const std::vector<PdfImportPreflightInfo>* preflight = nullptr;
    std::wstring outputRoot;
    std::wstring previewImagePath;
    std::wstring previewTempDir;
    std::wstring previewCaption;
    RECT previewRc = {};
    size_t previewPdfIndex = 0;
    int pdfCount = 0;
    int totalPageCount = 0;
    UINT dpi = 144;
    bool accepted = false;
    bool done = false;
    std::function<void(const DashboardPdfImportOptions&)> saveSettings;
};

std::wstring DashboardFormatPdfCloudConfirmPrompt(
    int selectedPageCount,
    int totalPageCount,
    int pdfCount,
    const DashboardPdfCloudRiskPolicy& risk);

bool DashboardCollectPdfImportPreflight(
    HWND owner,
    const std::vector<std::wstring>& pdfs,
    UINT dpi,
    HFONT font,
    std::vector<PdfImportPreflightInfo>& preflight,
    int& totalPageCount);

bool DashboardValidatePdfOptions(
    HWND owner,
    const std::wstring& pageRange,
    int pdfRenderDpi,
    const std::vector<PdfImportPreflightInfo>* preflight = nullptr,
    int* selectedPageCount = nullptr);

bool DashboardPreparePdfOptionsPreview(
    const std::vector<PdfImportPreflightInfo>& preflight,
    const std::wstring& pageRange,
    size_t previewPdfIndex,
    std::wstring& previewTempDir,
    std::wstring& previewImagePath,
    std::wstring& previewCaption);

void DashboardDeletePdfPreviewTemp(
    const std::wstring& previewImagePath,
    const std::wstring& previewTempDir);

bool DashboardRegisterPdfOptionsDialogClass();

SIZE DashboardGetPdfOptionsDialogWindowSize(UINT dpi);

// D-B-R2: typed modal run — owns CreateWindow + message loop.
// Window seeds `options`/`outputRoot` and provides callbacks; does not own dialog HWND loop.
// Optional test-drive injection for hermetic window tests (product passes null).
struct DashboardPdfOptionsDialogTestDrive {
    bool enabled = false;
    bool cancel = false;
    bool saveThenCancel = false;
    DashboardPdfImportOptions drivenOptions;
    bool* saveStayedOpenOut = nullptr;
};

struct DashboardPdfOptionsDialogRunInput {
    HWND owner = nullptr;
    UINT dpi = 0;
    HFONT fallbackFont = nullptr;
    const std::vector<std::wstring>* pdfs = nullptr;
    // Filled by preflight inside the run (or by caller for tests).
    std::vector<PdfImportPreflightInfo>* preflight = nullptr;
    int totalPageCount = 0;
    DashboardPdfImportOptions* options = nullptr;
    std::wstring* outputRoot = nullptr;
    std::function<bool(OcrOutputArtifactOptions&)> editOutputArtifacts;
    std::function<void(const DashboardPdfImportOptions&)> saveSettings;
    const DashboardPdfOptionsDialogTestDrive* testDrive = nullptr;
};

struct DashboardPdfOptionsDialogRunResult {
    bool accepted = false;
    bool dialogFailedOpen = false; // class registration or CreateWindow failed
    int selectedPageCount = 0;
};

DashboardPdfOptionsDialogRunResult DashboardRunPdfImportOptionsDialog(
    const DashboardPdfOptionsDialogRunInput& input);

inline constexpr const wchar_t* kDashboardPdfOptionsDialogClass =
    L"ZenCrop.OcrDashboard.PdfOptions";

// Compatibility alias for Import.inl call sites during D-B-11.
inline constexpr const wchar_t* kPdfOptionsDialogClass = kDashboardPdfOptionsDialogClass;
