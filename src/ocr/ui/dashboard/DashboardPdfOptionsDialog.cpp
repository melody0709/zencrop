#include "ocr/ui/dashboard/DashboardPdfOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardPdfOptionsDialogInternals.h"
#include "ocr/ui/dashboard/DashboardDialogLayout.h"
#include "ocr/ui/dashboard/DashboardFolderImportOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardOutputArtifactOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "ocr/ui/dashboard/DashboardPdfPasswordDialog.h"
#include "ocr/ui/OcrDashboardWindow.h"
#include "ocr/ui/DashboardModels.h"
#include "core/WideStringUtils.h"
#include "BatchOcrWriter.h"
#include "PageRange.h"
#include "PdfPageRenderer.h"
#include "Strings.h"

#include <windows.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>

#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>



namespace Theme {
    constexpr COLORREF bgInput   = RGB(30, 30, 30);
    constexpr COLORREF border    = RGB(60, 60, 60);
    constexpr COLORREF textMuted = RGB(100, 100, 100);
}

#define ID_PDF_OPTIONS_RANGE 1201
#define ID_PDF_OPTIONS_DPI 1202
#define ID_PDF_OPTIONS_OK 1203
#define ID_PDF_OPTIONS_CANCEL 1204
#define ID_PDF_OPTIONS_PREVIEW_TIMER 1205
#define ID_PDF_OPTIONS_PREV_PREVIEW 1206
#define ID_PDF_OPTIONS_NEXT_PREVIEW 1207
#define ID_PDF_OPTIONS_BROWSE_OUTPUT 1208
#define ID_PDF_OPTIONS_OCR_MODE 1209
#define ID_PDF_OPTIONS_MAX_EDGE 1230
#define ID_PDF_OPTIONS_MAX_MP 1231
#define ID_PDF_OPTIONS_FORMAT 1232
#define ID_PDF_OPTIONS_QUALITY 1233
#define ID_PDF_OPTIONS_CLOUD_CONSENT 1234
#define ID_PDF_OPTIONS_RESET_DEFAULTS 1235
#define ID_PDF_OPTIONS_SAVE_SETTINGS 1236
#define ID_PDF_OPTIONS_CHANGE_ARTIFACTS 1237

// PdfImportPreflightInfo + PdfOptionsDialogState live in DashboardPdfOptionsDialog.h


// Forward decl
static void LayoutPdfOptionsDialog(PdfOptionsDialogState* state);

// PDF-options-local thumbnail helpers (no SourceRail cache dependency).
static bool DrawImageThumbnail(
    HDC hdc,
    const std::wstring& imagePath,
    const RECT& rc,
    COLORREF borderColor,
    bool allowDecode = true)
{
    (void)allowDecode;
    if (rc.right <= rc.left || rc.bottom <= rc.top || imagePath.empty()) {
        return false;
    }
    Gdiplus::Bitmap image(imagePath.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok || image.GetWidth() == 0 || image.GetHeight() == 0) {
        return false;
    }
    const int targetW = rc.right - rc.left;
    const int targetH = rc.bottom - rc.top;
    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.DrawImage(&image, rc.left, rc.top, targetW, targetH);
    Gdiplus::Pen border(Gdiplus::Color(255,
        GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor)), 1.0f);
    graphics.DrawRectangle(&border,
        (int)rc.left, (int)rc.top, (int)(rc.right - rc.left - 1), (int)(rc.bottom - rc.top - 1));
    return true;
}

static void DrawThumbnailPlaceholder(HDC hdc, const RECT& rc, COLORREF borderColor, COLORREF textColor)
{
    HBRUSH bgBrush = CreateSolidBrush(Theme::bgInput);
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    RECT icon = {
        rc.left + w / 2 - (std::max)(12, w / 10),
        rc.top + h / 2 - (std::max)(9, h / 10),
        rc.left + w / 2 + (std::max)(12, w / 10),
        rc.top + h / 2 + (std::max)(9, h / 10)
    };
    Rectangle(hdc, icon.left, icon.top, icon.right, icon.bottom);
    MoveToEx(hdc, icon.left + 3, icon.bottom - 4, nullptr);
    LineTo(hdc, icon.left + (icon.right - icon.left) / 2, icon.top + 5);
    LineTo(hdc, icon.right - 3, icon.bottom - 4);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    RECT textRc = rc;
    textRc.top = icon.bottom + 2;
    DrawTextW(hdc, L"No image", -1, &textRc, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

// OCR mode pure helpers: DashboardIsCloudOcrMode / DashboardNormalizeOcrMode / DashboardOcrMode*
// live in DashboardFileTypes.h (included via headers). Password prompt is DashboardPromptForPdfPassword.

// OCR mode combo used by PDF options dialog
static void PopulateOcrModeCombo(
    HWND combo,
    const std::wstring& selectedMode,
    bool includeDashboardPrefix = false)
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

static int NormalizeFolderImportDepth(int depth) {
    return DashboardNormalizeFolderImportDepth(depth);
}



static void UpdatePdfPreviewNavControls(PdfOptionsDialogState* state) {
    if (!state || !state->preflight) return;
    size_t count = state->preflight->size();
    if (state->previewPdfIndex >= count && count > 0) {
        state->previewPdfIndex = count - 1;
    }

    if (state->previewPrevBtn) {
        EnableWindow(state->previewPrevBtn, count > 1 && state->previewPdfIndex > 0);
    }
    if (state->previewNextBtn) {
        EnableWindow(state->previewNextBtn, count > 1 && state->previewPdfIndex + 1 < count);
    }
    if (state->previewIndexText) {
        // OWN-123: pure int labels (WideStringUtils).
        std::wstring text = count > 0
            ? (WideFormatIntLabel((int)(state->previewPdfIndex + 1)) + L" / " +
               WideFormatIntLabel((int)count))
            : L"0 / 0";
        SetWindowTextW(state->previewIndexText, text.c_str());
    }
}

static void RefreshPdfOptionsPreview(PdfOptionsDialogState* state) {
    if (!state || !state->hwnd || !state->preflight) return;
    if (state->previewPdfIndex >= state->preflight->size()) {
        state->previewPdfIndex = 0;
    }

    std::wstring oldImagePath = state->previewImagePath;
    std::wstring oldTempDir = state->previewTempDir;
    std::wstring nextTempDir;
    std::wstring nextImagePath;
    std::wstring nextCaption;
    std::wstring range = state->rangeEdit ? DashboardGetWindowTextWide(state->rangeEdit) : L"all";

    DashboardPreparePdfOptionsPreview(
        *state->preflight,
        range.empty() ? L"all" : range,
        state->previewPdfIndex,
        nextTempDir,
        nextImagePath,
        nextCaption);

    state->previewTempDir = std::move(nextTempDir);
    state->previewImagePath = std::move(nextImagePath);
    state->previewCaption = std::move(nextCaption);

    DashboardDeletePdfPreviewTemp(oldImagePath, oldTempDir);

    if (state->previewCaptionText) {
        SetWindowTextW(state->previewCaptionText, state->previewCaption.c_str());
    }
    UpdatePdfPreviewNavControls(state);
    InvalidateRect(state->hwnd, &state->previewRc, FALSE);
}

// D-B-8 password dialog: DashboardPromptForPdfPassword thin forward is defined once above.



static void UpdatePdfOptionsOutputSummary(PdfOptionsDialogState* state) {
    if (!state) return;
    if (state->outputText) {
        SetWindowTextW(
            state->outputText,
            DashboardPdfFormatOutputText(state->outputRoot, state->preflight).c_str());
    }
    if (state->outputTreeText) {
        SetWindowTextW(
            state->outputTreeText,
            DashboardPdfFormatOutputTreeText(
                state->outputRoot,
                state->preflight,
                state->options ? state->options->pdfImageFormat : PdfRenderImageFormat::Auto,
                state->options ? state->options->outputArtifacts : OcrOutputArtifactOptions()).c_str());
    }
}

static void UpdatePdfOptionsArtifactSummary(PdfOptionsDialogState* state) {
    if (!state || !state->options || !state->artifactsSummary) return;
    SetWindowTextW(
        state->artifactsSummary,
        DashboardFormatOutputArtifactSummary(state->options->outputArtifacts).c_str());
}



static int GetPdfDialogInt(HWND edit, int fallback) {
    if (!edit) return fallback;
    std::wstring text = DashboardTrimWide(DashboardGetWindowTextWide(edit));
    if (text.empty()) return fallback;
    // OWN-77: pure int parse (WideStringUtils).
    return WideParseJsonIntToken(text, fallback);
}

static PdfRenderImageFormat GetPdfDialogFormat(PdfOptionsDialogState* state) {
    if (!state || !state->formatCombo) {
        return state && state->options ? state->options->pdfImageFormat : PdfRenderImageFormat::Auto;
    }
    int sel = (int)SendMessageW(state->formatCombo, CB_GETCURSEL, 0, 0);
    switch (sel) {
    case 1: return PdfRenderImageFormat::Png;
    case 2: return PdfRenderImageFormat::Jpeg;
    case 3: return PdfRenderImageFormat::WebP;
    case 0:
    default:
        return PdfRenderImageFormat::Auto;
    }
}

static int PdfDialogFormatToComboIndex(PdfRenderImageFormat format) {
    switch (format) {
    case PdfRenderImageFormat::Png: return 1;
    case PdfRenderImageFormat::Jpeg: return 2;
    case PdfRenderImageFormat::WebP: return 3;
    case PdfRenderImageFormat::Auto:
    default:
        return 0;
    }
}

static void PopulatePdfFormatCombo(HWND combo, PdfRenderImageFormat format) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PNG"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"JPEG"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"WebP"));
    SendMessageW(combo, CB_SETCURSEL, PdfDialogFormatToComboIndex(format), 0);
}

static void UpdatePdfQualityControlState(PdfOptionsDialogState* state) {
    if (!state) return;
    bool enabled = GetPdfDialogFormat(state) != PdfRenderImageFormat::Png;
    if (state->qualityLabel) EnableWindow(state->qualityLabel, enabled);
    if (state->qualityEdit) EnableWindow(state->qualityEdit, enabled);
}

static bool PdfOptionsCloudConsentShouldBeVisible(const PdfOptionsDialogState* state) {
    if (!state || !state->modeCombo) return false;
    const int selection = state->modeCombo
        ? static_cast<int>(SendMessageW(state->modeCombo, CB_GETCURSEL, 0, 0))
        : 0;
    return DashboardIsCloudOcrMode(DashboardOcrModeFromComboIndex(selection));
}

static void UpdatePdfCloudConsentControlState(PdfOptionsDialogState* state) {
    if (!state || !state->cloudConsentCheck) return;
    const bool cloudMode = PdfOptionsCloudConsentShouldBeVisible(state);
    ShowWindow(state->cloudConsentCheck, cloudMode ? SW_SHOW : SW_HIDE);
    EnableWindow(state->cloudConsentCheck, cloudMode ? TRUE : FALSE);
    if (state->hwnd) LayoutPdfOptionsDialog(state);
}

static bool ValidatePdfRenderControls(
    HWND owner,
    int maxEdge,
    int maxMp,
    int quality)
{
    if (maxEdge != 0 && (maxEdge < 1000 || maxEdge > 12000)) {
        MessageBoxW(owner, L"Max edge (px) must be 0 or between 1000 and 12000.", L"ZenCrop", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (maxMp != 0 && (maxMp < 1 || maxMp > 100)) {
        MessageBoxW(owner, L"Max page size (MP) must be 0 or between 1 and 100.", L"ZenCrop", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (quality < 1 || quality > 100) {
        MessageBoxW(owner, L"Quality must be between 1 and 100.", L"ZenCrop", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

static void UpdatePdfOptionsSelectionSummary(PdfOptionsDialogState* state) {
    if (!state) return;

    int selectedPages = 0;
    std::wstring error;
    std::wstring range = state->rangeEdit ? DashboardGetWindowTextWide(state->rangeEdit) : L"all";
    if (DashboardPdfCountSelectedPages(range, state->preflight, selectedPages, error)) {
        if (state->selectedText) {
            SetWindowTextW(
                state->selectedText,
                DashboardPdfFormatSelectedPageText(selectedPages > 0 ? selectedPages : state->totalPageCount, state->totalPageCount).c_str());
        }
    } else {
        if (state->selectedText) {
            SetWindowTextW(state->selectedText, L"Selected pages: invalid range");
        }
    }

    if (state->estimateText) {
        int dpi = GetPdfDialogInt(state->dpiEdit, kDefaultPdfRenderDpi);
        if (dpi <= 0) dpi = kDefaultPdfRenderDpi;
        int maxEdge = GetPdfDialogInt(state->maxEdgeEdit, (int)kDefaultPdfMaxPixelEdge);
        int maxMp = GetPdfDialogInt(state->maxMpEdit, (int)kDefaultPdfMaxMegapixels);
        SetWindowTextW(
            state->estimateText,
            DashboardPdfFormatEstimateText(
                range,
                dpi,
                ClampPdfRenderMaxPixelEdge(maxEdge),
                ClampPdfRenderMaxMegapixels(maxMp),
                GetPdfDialogFormat(state),
                state->preflight).c_str());
    }
}

static void ResetPdfOptionsDialogDefaults(PdfOptionsDialogState* state) {
    if (!state || !state->hwnd) return;

    // OWN-123: pure int labels (WideStringUtils).
    SetWindowTextW(state->rangeEdit, L"all");
    SetWindowTextW(state->dpiEdit, WideFormatIntLabel(kDefaultPdfRenderDpi).c_str());
    SetWindowTextW(state->maxEdgeEdit, WideFormatIntLabel(kDefaultPdfMaxPixelEdge).c_str());
    SetWindowTextW(state->maxMpEdit, WideFormatIntLabel(kDefaultPdfMaxMegapixels).c_str());
    SetWindowTextW(state->qualityEdit, WideFormatIntLabel(kDefaultPdfImageQuality).c_str());
    PopulatePdfFormatCombo(state->formatCombo, PdfRenderImageFormat::Auto);
    if (state->options) {
        state->options->pdfImageFormat = PdfRenderImageFormat::Auto;
        DashboardApplyOutputArtifactProfile(
            state->options->outputArtifacts,
            DashboardOutputArtifactProfile::Compact);
    }

    UpdatePdfQualityControlState(state);
    UpdatePdfOptionsSelectionSummary(state);
    UpdatePdfOptionsArtifactSummary(state);
    UpdatePdfOptionsOutputSummary(state);
    KillTimer(state->hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER);
    SetTimer(state->hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER, 1, nullptr);
}

static bool CapturePdfOptionsDialogControls(PdfOptionsDialogState* state) {
    if (!state || !state->hwnd || !state->options) return false;

    std::wstring range = DashboardTrimWide(DashboardGetWindowTextWide(state->rangeEdit));
    if (range.empty()) range = L"all";
    // OWN-77: pure int parse (WideStringUtils).
    int dpi = WideParseJsonIntToken(DashboardTrimWide(DashboardGetWindowTextWide(state->dpiEdit)));
    if (!DashboardValidatePdfOptions(state->hwnd, range, dpi, state->preflight)) return false;

    int maxEdge = GetPdfDialogInt(state->maxEdgeEdit, (int)kDefaultPdfMaxPixelEdge);
    int maxMp = GetPdfDialogInt(state->maxMpEdit, (int)kDefaultPdfMaxMegapixels);
    int quality = GetPdfDialogInt(state->qualityEdit, kDefaultPdfImageQuality);
    if (!ValidatePdfRenderControls(state->hwnd, maxEdge, maxMp, quality)) return false;

    state->options->pageRange = range;
    state->options->pdfRenderDpi = dpi;
    state->options->pdfMaxPixelEdge = ClampPdfRenderMaxPixelEdge(maxEdge);
    state->options->pdfMaxMegapixels = ClampPdfRenderMaxMegapixels(maxMp);
    state->options->pdfImageFormat = GetPdfDialogFormat(state);
    state->options->pdfImageQuality = ClampPdfRenderImageQuality(quality);
    int modeSel = state->modeCombo
        ? (int)SendMessageW(state->modeCombo, CB_GETCURSEL, 0, 0)
        : 0;
    state->options->ocrMode = DashboardOcrModeFromComboIndex(modeSel);
    state->options->rememberCloudFullPdfConsent =
        IsDlgButtonChecked(state->hwnd, ID_PDF_OPTIONS_CLOUD_CONSENT) == BST_CHECKED;
    return true;
}

static SIZE GetPdfOptionsDialogClientSize(UINT dpi) {
    SIZE size = {};
    size.cx = DashboardScaleDialogValue(920, dpi);
    size.cy = DashboardScaleDialogValue(690, dpi);
    return size;
}

SIZE DashboardGetPdfOptionsDialogWindowSize(UINT dpi) {
    SIZE client = GetPdfOptionsDialogClientSize(dpi);
    RECT rc = { 0, 0, client.cx, client.cy };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi > 0 ? dpi : kDashboardDialogDesignDpi);
    SIZE size = {};
    size.cx = rc.right - rc.left;
    size.cy = rc.bottom - rc.top;
    return size;
}

// D-B-11: use DashboardClampDialogSizeToWorkArea from DashboardDialogLayout.h

static void ApplyPdfOptionsDialogFont(PdfOptionsDialogState* state) {
    if (!state) return;
    HWND controls[] = {
        state->titleText,
        state->countText,
        state->previewTitle,
        state->rangeLabel,
        state->rangeEdit,
        state->selectedText,
        state->dpiLabel,
        state->dpiEdit,
        state->maxEdgeLabel,
        state->maxEdgeEdit,
        state->maxMpLabel,
        state->maxMpEdit,
        state->formatLabel,
        state->formatCombo,
        state->qualityLabel,
        state->qualityEdit,
        state->modeLabel,
        state->modeCombo,
        state->cloudConsentCheck,
        state->artifactsLabel,
        state->artifactsSummary,
        state->artifactsChangeBtn,
        state->estimateLabel,
        state->estimateText,
        state->outputLabel,
        state->outputText,
        state->outputBrowseBtn,
        state->outputTreeText,
        state->previewCaptionText,
        state->previewPrevBtn,
        state->previewNextBtn,
        state->previewIndexText,
        state->resetDefaultsBtn,
        state->saveSettingsBtn,
        state->okBtn,
        state->cancelBtn
    };
    for (HWND control : controls) {
        DashboardSetControlFont(control, state->font);
    }
}

static int GetPdfOptionsDialogLineHeight(PdfOptionsDialogState* state) {
    int fallback = DashboardScaleDialogValue(24, state ? state->dpi : kDashboardDialogDesignDpi);
    if (!state || !state->hwnd || !state->font) return fallback;

    HDC hdc = GetDC(state->hwnd);
    if (!hdc) return fallback;

    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, state->font));
    TEXTMETRICW tm = {};
    int lineH = fallback;
    if (GetTextMetricsW(hdc, &tm)) {
        lineH = max(lineH, tm.tmHeight + tm.tmExternalLeading);
    }
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(state->hwnd, hdc);
    return lineH;
}

static void LayoutPdfOptionsDialog(PdfOptionsDialogState* state) {
    if (!state || !state->hwnd) return;

    RECT client = {};
    GetClientRect(state->hwnd, &client);
    int clientW = max(1, client.right - client.left);
    int clientH = max(1, client.bottom - client.top);

    int lineH = GetPdfOptionsDialogLineHeight(state);
    int margin = DashboardScaleDialogValue(22, state->dpi);
    int gap = DashboardScaleDialogValue(30, state->dpi);
    int rowGap = DashboardScaleDialogValue(8, state->dpi);
    int labelH = lineH + DashboardScaleDialogValue(6, state->dpi);
    int titleH = lineH + DashboardScaleDialogValue(8, state->dpi);
    int editH = max(lineH + DashboardScaleDialogValue(10, state->dpi), DashboardScaleDialogValue(32, state->dpi));
    int buttonW = DashboardScaleDialogValue(100, state->dpi);
    int buttonH = max(lineH + DashboardScaleDialogValue(10, state->dpi), DashboardScaleDialogValue(34, state->dpi));
    int buttonGap = DashboardScaleDialogValue(14, state->dpi);
    int browseW = DashboardScaleDialogValue(96, state->dpi);

    int previewW = min(DashboardScaleDialogValue(320, state->dpi), max(DashboardScaleDialogValue(260, state->dpi), clientW / 3));
    int leftW = max(DashboardScaleDialogValue(360, state->dpi), clientW - margin * 2 - gap - previewW);
    int previewX = margin + leftW + gap;
    if (previewX + previewW > clientW - margin) {
        previewW = max(DashboardScaleDialogValue(220, state->dpi), clientW - margin - previewX);
    }

    int titleY = margin;
    int countY = titleY + titleH + DashboardScaleDialogValue(2, state->dpi);
    int rowY = countY + labelH + DashboardScaleDialogValue(18, state->dpi);

    int previewTitleY = titleY;
    int previewY = previewTitleY + labelH + DashboardScaleDialogValue(8, state->dpi);
    int previewH = min(DashboardScaleDialogValue(230, state->dpi), max(DashboardScaleDialogValue(150, state->dpi), clientH / 3));

    int labelW = min(DashboardScaleDialogValue(116, state->dpi), max(DashboardScaleDialogValue(92, state->dpi), leftW / 4));
    int editX = margin + labelW + DashboardScaleDialogValue(10, state->dpi);
    int editW = max(DashboardScaleDialogValue(120, state->dpi), leftW - labelW - DashboardScaleDialogValue(10, state->dpi));

    int selectedY = rowY + editH + DashboardScaleDialogValue(8, state->dpi);
    int dpiY = selectedY + labelH + rowGap;
    int rasterEditY = dpiY + labelH + DashboardScaleDialogValue(4, state->dpi);
    int formatY = rasterEditY + editH + rowGap;
    int modeY = formatY + editH + rowGap;
    int consentY = modeY + editH + DashboardScaleDialogValue(8, state->dpi);
    const bool cloudConsentVisible = state->cloudConsentCheck &&
        PdfOptionsCloudConsentShouldBeVisible(state);
    int consentH = cloudConsentVisible ? max(labelH, DashboardScaleDialogValue(26, state->dpi)) : 0;
    int artifactsY = consentY + consentH + (cloudConsentVisible ? DashboardScaleDialogValue(8, state->dpi) : 0);
    int artifactsH = editH;
    int estimateY = artifactsY + artifactsH + DashboardScaleDialogValue(8, state->dpi);
    int estimateH = lineH * 3 + DashboardScaleDialogValue(8, state->dpi);

    int previewCaptionY = previewY + previewH + DashboardScaleDialogValue(8, state->dpi);
    int previewCaptionH = labelH;
    int previewNavY = previewCaptionY + previewCaptionH + DashboardScaleDialogValue(6, state->dpi);
    int previewBtnW = DashboardScaleDialogValue(64, state->dpi);
    int previewBtnH = max(lineH + DashboardScaleDialogValue(8, state->dpi), DashboardScaleDialogValue(30, state->dpi));
    int buttonY = max(margin, clientH - margin - buttonH);

    int outputY = max(estimateY + estimateH + DashboardScaleDialogValue(10, state->dpi),
        previewNavY + previewBtnH + DashboardScaleDialogValue(28, state->dpi));
    int outputTextH = labelH;
    int outputTreeY = outputY + outputTextH + DashboardScaleDialogValue(8, state->dpi);
    int minTreeH = DashboardScaleDialogValue(48, state->dpi);
    int treeBottomGap = DashboardScaleDialogValue(10, state->dpi);
    int outputTreeH = buttonY - outputTreeY - treeBottomGap;
    // Do not move Output upward to satisfy a tree minimum: that used to put
    // the tree and the Output row directly over Estimate. On small work areas
    // the tree is the only elastic region; controls above and below stay
    // readable and the default dialog height keeps a useful tree visible.
    const bool showOutputTree = outputTreeH >= minTreeH;
    outputTreeH = max(1, outputTreeH);

    MoveWindow(state->titleText, margin, titleY, leftW, titleH, TRUE);
    MoveWindow(state->countText, margin, countY, leftW, labelH, TRUE);
    MoveWindow(state->previewTitle, previewX, previewTitleY, previewW, labelH, TRUE);

    MoveWindow(state->rangeLabel, margin, rowY + DashboardScaleDialogValue(4, state->dpi), labelW, labelH, TRUE);
    MoveWindow(state->rangeEdit, editX, rowY, editW, editH, TRUE);
    MoveWindow(state->selectedText, editX, selectedY, editW, labelH, TRUE);

    int smallGap = DashboardScaleDialogValue(8, state->dpi);
    int smallLabelW = DashboardScaleDialogValue(70, state->dpi);
    int smallEditW = DashboardScaleDialogValue(76, state->dpi);
    int dpiEditW = DashboardScaleDialogValue(70, state->dpi);
    int rasterColumnGap = DashboardScaleDialogValue(12, state->dpi);
    int rasterColumnW = max(1, (leftW - rasterColumnGap * 2) / 3);
    int dpiColumnX = margin;
    int maxEdgeColumnX = dpiColumnX + rasterColumnW + rasterColumnGap;
    int maxMpColumnX = maxEdgeColumnX + rasterColumnW + rasterColumnGap;
    MoveWindow(state->dpiLabel, dpiColumnX, dpiY, rasterColumnW, labelH, TRUE);
    MoveWindow(state->dpiEdit, dpiColumnX, rasterEditY, dpiEditW, editH, TRUE);
    MoveWindow(state->maxEdgeLabel, maxEdgeColumnX, dpiY, rasterColumnW, labelH, TRUE);
    MoveWindow(state->maxEdgeEdit, maxEdgeColumnX, rasterEditY, smallEditW, editH, TRUE);
    MoveWindow(state->maxMpLabel, maxMpColumnX, dpiY, rasterColumnW, labelH, TRUE);
    MoveWindow(state->maxMpEdit, maxMpColumnX, rasterEditY, smallEditW, editH, TRUE);

    MoveWindow(state->formatLabel, margin, formatY + DashboardScaleDialogValue(4, state->dpi), labelW, labelH, TRUE);
    int formatW = DashboardScaleDialogValue(110, state->dpi);
    int qualityLabelX = editX + formatW + smallGap;
    int qualityEditX = qualityLabelX + smallLabelW;
    MoveWindow(state->formatCombo, editX, formatY, formatW, editH * 5, TRUE);
    MoveWindow(state->qualityLabel, qualityLabelX, formatY + DashboardScaleDialogValue(4, state->dpi), smallLabelW, labelH, TRUE);
    MoveWindow(state->qualityEdit, qualityEditX, formatY, smallEditW, editH, TRUE);

    MoveWindow(state->modeLabel, margin, modeY + DashboardScaleDialogValue(4, state->dpi), labelW, labelH, TRUE);
    MoveWindow(state->modeCombo, editX, modeY, min(editW, DashboardScaleDialogValue(260, state->dpi)),
        editH * 6, TRUE);
    if (cloudConsentVisible) {
        MoveWindow(state->cloudConsentCheck, editX, consentY, editW, consentH, TRUE);
    }
    MoveWindow(state->artifactsLabel, margin, artifactsY + DashboardScaleDialogValue(4, state->dpi), labelW, labelH, TRUE);
    MoveWindow(state->artifactsSummary, editX, artifactsY + DashboardScaleDialogValue(4, state->dpi),
        max(DashboardScaleDialogValue(120, state->dpi), editW - DashboardScaleDialogValue(98, state->dpi)), labelH, TRUE);
    MoveWindow(state->artifactsChangeBtn, editX + editW - DashboardScaleDialogValue(90, state->dpi), artifactsY,
        DashboardScaleDialogValue(90, state->dpi), editH, TRUE);

    MoveWindow(state->estimateLabel, margin, estimateY + DashboardScaleDialogValue(2, state->dpi), labelW, labelH, TRUE);
    MoveWindow(state->estimateText, editX, estimateY, editW, estimateH, TRUE);

    RECT oldPreview = state->previewRc;
    state->previewRc = { previewX, previewY, previewX + previewW, previewY + previewH };
    MoveWindow(state->previewCaptionText, previewX, previewCaptionY, previewW, previewCaptionH, TRUE);
    MoveWindow(state->previewPrevBtn, previewX, previewNavY, previewBtnW, previewBtnH, TRUE);
    MoveWindow(state->previewIndexText,
        previewX + previewBtnW + DashboardScaleDialogValue(8, state->dpi),
        previewNavY + DashboardScaleDialogValue(4, state->dpi),
        max(DashboardScaleDialogValue(64, state->dpi), previewW - previewBtnW * 2 - DashboardScaleDialogValue(16, state->dpi)),
        labelH,
        TRUE);
    MoveWindow(state->previewNextBtn, previewX + previewW - previewBtnW, previewNavY, previewBtnW, previewBtnH, TRUE);

    MoveWindow(state->outputLabel, margin, outputY + DashboardScaleDialogValue(2, state->dpi), labelW, labelH, TRUE);
    int browseX = clientW - margin - browseW;
    int outputTextW = max(
        DashboardScaleDialogValue(160, state->dpi),
        browseX - buttonGap - editX);
    MoveWindow(state->outputText, editX, outputY + DashboardScaleDialogValue(2, state->dpi),
        outputTextW, outputTextH, TRUE);
    MoveWindow(state->outputBrowseBtn, browseX, outputY, browseW, buttonH, TRUE);
    MoveWindow(state->outputTreeText, margin, outputTreeY,
        max(DashboardScaleDialogValue(240, state->dpi), clientW - margin * 2), outputTreeH, TRUE);
    if (state->outputTreeText) {
        ShowWindow(state->outputTreeText, showOutputTree ? SW_SHOW : SW_HIDE);
    }

    int cancelW = DashboardScaleDialogValue(104, state->dpi);
    int okW = DashboardScaleDialogValue(122, state->dpi);
    int cancelX = clientW - margin - cancelW;
    int okX = cancelX - buttonGap - okW;
    int resetW = DashboardScaleDialogValue(132, state->dpi);
    int saveW = DashboardScaleDialogValue(156, state->dpi);
    MoveWindow(state->resetDefaultsBtn, margin, buttonY, resetW, buttonH, TRUE);
    MoveWindow(state->saveSettingsBtn, margin + resetW + buttonGap, buttonY, saveW, buttonH, TRUE);
    MoveWindow(state->okBtn, okX, buttonY, okW, buttonH, TRUE);
    MoveWindow(state->cancelBtn, cancelX, buttonY, cancelW, buttonH, TRUE);

    RECT dirty = {};
    UnionRect(&dirty, &oldPreview, &state->previewRc);
    InflateRect(&dirty, DashboardScaleDialogValue(2, state->dpi), DashboardScaleDialogValue(2, state->dpi));
    InvalidateRect(state->hwnd, &dirty, TRUE);
}

static LRESULT CALLBACK PdfOptionsDialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PdfOptionsDialogState* state = reinterpret_cast<PdfOptionsDialogState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<PdfOptionsDialogState*>(cs ? cs->lpCreateParams : nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state || !state->options) return -1;

        state->hwnd = hwnd;
        int margin = DashboardScaleDialogValue(18, state->dpi);
        int labelW = DashboardScaleDialogValue(98, state->dpi);
        int editH = DashboardScaleDialogValue(26, state->dpi);
        int rowY = DashboardScaleDialogValue(66, state->dpi);
        int editX = margin + labelW + DashboardScaleDialogValue(10, state->dpi);
        int editW = DashboardScaleDialogValue(350, state->dpi);
        int previewX = DashboardScaleDialogValue(500, state->dpi);
        int previewY = DashboardScaleDialogValue(42, state->dpi);
        int previewW = DashboardScaleDialogValue(242, state->dpi);
        int previewH = DashboardScaleDialogValue(170, state->dpi);
        int outputTreeY = DashboardScaleDialogValue(330, state->dpi);
        int outputTreeH = DashboardScaleDialogValue(136, state->dpi);
        state->previewRc = { previewX, previewY, previewX + previewW, previewY + previewH };
        if (state->previewCaption.empty()) {
            state->previewCaption = L"Preview unavailable";
        }

        state->titleText = CreateWindowExW(
            0, L"STATIC", L"PDF import",
            WS_CHILD | WS_VISIBLE,
            margin, DashboardScaleDialogValue(16, state->dpi),
            DashboardScaleDialogValue(450, state->dpi), DashboardScaleDialogValue(24, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->titleText, state->font);

        // OWN-123: pure int labels (WideStringUtils).
        std::wstring countText = L"Files: " + WideFormatIntLabel(max(1, state->pdfCount));
        if (state->totalPageCount > 0) {
            countText += L" | Pages: ";
            countText += WideFormatIntLabel(state->totalPageCount);
        }
        state->countText = CreateWindowExW(
            0, L"STATIC", countText.c_str(),
            WS_CHILD | WS_VISIBLE,
            margin, DashboardScaleDialogValue(36, state->dpi),
            DashboardScaleDialogValue(450, state->dpi), DashboardScaleDialogValue(20, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->countText, state->font);

        state->previewTitle = CreateWindowExW(
            0, L"STATIC", L"Page preview",
            WS_CHILD | WS_VISIBLE,
            previewX, DashboardScaleDialogValue(16, state->dpi),
            previewW, DashboardScaleDialogValue(20, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->previewTitle, state->font);

        state->previewCaptionText = CreateWindowExW(
            0, L"STATIC", state->previewCaption.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
            previewX, previewY + previewH + DashboardScaleDialogValue(8, state->dpi),
            previewW, DashboardScaleDialogValue(34, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->previewCaptionText, state->font);

        int previewNavY = previewY + previewH + DashboardScaleDialogValue(48, state->dpi);
        int previewBtnW = DashboardScaleDialogValue(58, state->dpi);
        int previewBtnH = DashboardScaleDialogValue(26, state->dpi);
        state->previewPrevBtn = CreateWindowExW(
            0, L"BUTTON", L"Prev",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            previewX, previewNavY,
            previewBtnW, previewBtnH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_PREV_PREVIEW), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->previewPrevBtn, state->font);

        state->previewIndexText = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            previewX + previewBtnW + DashboardScaleDialogValue(8, state->dpi), previewNavY + DashboardScaleDialogValue(4, state->dpi),
            DashboardScaleDialogValue(78, state->dpi), DashboardScaleDialogValue(20, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->previewIndexText, state->font);

        state->previewNextBtn = CreateWindowExW(
            0, L"BUTTON", L"Next",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            previewX + previewW - previewBtnW, previewNavY,
            previewBtnW, previewBtnH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_NEXT_PREVIEW), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->previewNextBtn, state->font);
        UpdatePdfPreviewNavControls(state);

        state->rangeLabel = CreateWindowExW(
            0, L"STATIC", L"Page range",
            WS_CHILD | WS_VISIBLE,
            margin, rowY + DashboardScaleDialogValue(4, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->rangeLabel, state->font);

        state->rangeEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", state->options->pageRange.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            editX, rowY, editW, editH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_RANGE), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->rangeEdit, state->font);
        SendMessageW(state->rangeEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"all"));

        int selectedY = rowY + editH + DashboardScaleDialogValue(6, state->dpi);
        state->selectedText = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            editX, selectedY,
            editW, DashboardScaleDialogValue(20, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->selectedText, state->font);

        int dpiY = selectedY + DashboardScaleDialogValue(28, state->dpi);
        state->dpiLabel = CreateWindowExW(
            0, L"STATIC", L"DPI (default 100)",
            WS_CHILD | WS_VISIBLE,
            margin, dpiY + DashboardScaleDialogValue(4, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->dpiLabel, state->font);

        // OWN-123: pure int labels (WideStringUtils).
        std::wstring dpiText = WideFormatIntLabel(state->options->pdfRenderDpi > 0 ? state->options->pdfRenderDpi : kDefaultPdfRenderDpi);
        state->dpiEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", dpiText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX, dpiY, DashboardScaleDialogValue(82, state->dpi), editH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_DPI), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->dpiEdit, state->font);

        state->maxEdgeLabel = CreateWindowExW(
            0, L"STATIC", L"Max edge (px)",
            WS_CHILD | WS_VISIBLE,
            editX + DashboardScaleDialogValue(90, state->dpi), dpiY + DashboardScaleDialogValue(4, state->dpi),
            DashboardScaleDialogValue(80, state->dpi), DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->maxEdgeLabel, state->font);

        std::wstring maxEdgeText = WideFormatIntLabel(state->options->pdfMaxPixelEdge);
        state->maxEdgeEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", maxEdgeText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX + DashboardScaleDialogValue(170, state->dpi), dpiY, DashboardScaleDialogValue(82, state->dpi), editH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_MAX_EDGE), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->maxEdgeEdit, state->font);

        state->maxMpLabel = CreateWindowExW(
            0, L"STATIC", L"Max page size (MP)",
            WS_CHILD | WS_VISIBLE,
            editX + DashboardScaleDialogValue(260, state->dpi), dpiY + DashboardScaleDialogValue(4, state->dpi),
            DashboardScaleDialogValue(70, state->dpi), DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->maxMpLabel, state->font);

        std::wstring maxMpText = WideFormatIntLabel(state->options->pdfMaxMegapixels);
        state->maxMpEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", maxMpText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX + DashboardScaleDialogValue(330, state->dpi), dpiY, DashboardScaleDialogValue(70, state->dpi), editH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_MAX_MP), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->maxMpEdit, state->font);

        int formatY = dpiY + editH + DashboardScaleDialogValue(12, state->dpi);
        state->formatLabel = CreateWindowExW(
            0, L"STATIC", L"Page format",
            WS_CHILD | WS_VISIBLE,
            margin, formatY + DashboardScaleDialogValue(4, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->formatLabel, state->font);

        state->formatCombo = CreateWindowExW(
            0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
            editX, formatY, DashboardScaleDialogValue(110, state->dpi), DashboardScaleDialogValue(140, state->dpi),
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_FORMAT), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->formatCombo, state->font);
        PopulatePdfFormatCombo(state->formatCombo, state->options->pdfImageFormat);

        state->qualityLabel = CreateWindowExW(
            0, L"STATIC", L"Quality",
            WS_CHILD | WS_VISIBLE,
            editX + DashboardScaleDialogValue(122, state->dpi), formatY + DashboardScaleDialogValue(4, state->dpi),
            DashboardScaleDialogValue(80, state->dpi), DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->qualityLabel, state->font);

        // OWN-123: pure int labels (WideStringUtils).
        std::wstring qualityText = WideFormatIntLabel(ClampPdfRenderImageQuality(state->options->pdfImageQuality));
        state->qualityEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", qualityText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX + DashboardScaleDialogValue(202, state->dpi), formatY, DashboardScaleDialogValue(70, state->dpi), editH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_QUALITY), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->qualityEdit, state->font);
        UpdatePdfQualityControlState(state);

        int modeY = formatY + editH + DashboardScaleDialogValue(12, state->dpi);
        state->modeLabel = CreateWindowExW(
            0, L"STATIC", L"OCR model",
            WS_CHILD | WS_VISIBLE,
            margin, modeY + DashboardScaleDialogValue(4, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->modeLabel, state->font);

        state->modeCombo = CreateWindowExW(
            0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
            editX, modeY, DashboardScaleDialogValue(240, state->dpi), DashboardScaleDialogValue(150, state->dpi),
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_OCR_MODE), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->modeCombo, state->font);
        PopulateOcrModeCombo(state->modeCombo, state->options->ocrMode);

        int consentY = modeY + editH + DashboardScaleDialogValue(8, state->dpi);
        state->cloudConsentCheck = CreateWindowExW(
            0, L"BUTTON", L"Always upload eligible original PDFs to Cloud (don't ask again)",
            WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
            editX, consentY, editW, DashboardScaleDialogValue(28, state->dpi),
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_CLOUD_CONSENT), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->cloudConsentCheck, state->font);
        CheckDlgButton(
            hwnd,
            ID_PDF_OPTIONS_CLOUD_CONSENT,
            state->options->rememberCloudFullPdfConsent ? BST_CHECKED : BST_UNCHECKED);

        int artifactsY = consentY + DashboardScaleDialogValue(36, state->dpi);
        state->artifactsLabel = CreateWindowExW(
            0, L"STATIC", L"Derived output",
            WS_CHILD | WS_VISIBLE,
            margin, artifactsY + DashboardScaleDialogValue(4, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->artifactsLabel, state->font);
        state->artifactsSummary = CreateWindowExW(
            0, L"STATIC", DashboardFormatOutputArtifactSummary(state->options->outputArtifacts).c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
            editX, artifactsY + DashboardScaleDialogValue(4, state->dpi),
            DashboardScaleDialogValue(270, state->dpi), DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->artifactsSummary, state->font);
        state->artifactsChangeBtn = CreateWindowExW(
            0, L"BUTTON", L"Change...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            editX + DashboardScaleDialogValue(284, state->dpi), artifactsY,
            DashboardScaleDialogValue(88, state->dpi), DashboardScaleDialogValue(30, state->dpi),
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_CHANGE_ARTIFACTS), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->artifactsChangeBtn, state->font);

        int estimateY = artifactsY + DashboardScaleDialogValue(38, state->dpi);
        state->estimateLabel = CreateWindowExW(
            0, L"STATIC", L"Estimate",
            WS_CHILD | WS_VISIBLE,
            margin, estimateY + DashboardScaleDialogValue(2, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->estimateLabel, state->font);

        state->estimateText = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            editX, estimateY,
            editW, DashboardScaleDialogValue(62, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->estimateText, state->font);
        UpdatePdfOptionsSelectionSummary(state);

        int outputY = DashboardScaleDialogValue(286, state->dpi);
        state->outputLabel = CreateWindowExW(
            0, L"STATIC", L"Output",
            WS_CHILD | WS_VISIBLE,
            margin, outputY + DashboardScaleDialogValue(2, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->outputLabel, state->font);

        std::wstring outputText = DashboardPdfFormatOutputText(state->outputRoot, state->preflight);
        state->outputText = CreateWindowExW(
            0, L"STATIC", outputText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
            editX, outputY + DashboardScaleDialogValue(2, state->dpi),
            DashboardScaleDialogValue(614, state->dpi), DashboardScaleDialogValue(36, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->outputText, state->font);

        state->outputBrowseBtn = CreateWindowExW(
            0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            DashboardScaleDialogValue(676, state->dpi), outputY,
            DashboardScaleDialogValue(92, state->dpi), DashboardScaleDialogValue(30, state->dpi),
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_BROWSE_OUTPUT), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->outputBrowseBtn, state->font);

        std::wstring outputTreeText = DashboardPdfFormatOutputTreeText(
            state->outputRoot,
            state->preflight,
            state->options->pdfImageFormat,
            state->options->outputArtifacts);
        state->outputTreeText = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", outputTreeText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            margin, outputTreeY,
            DashboardScaleDialogValue(724, state->dpi), outputTreeH,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->outputTreeText, state->font);

        int buttonY = DashboardScaleDialogValue(488, state->dpi);
        int buttonW = DashboardScaleDialogValue(92, state->dpi);
        int buttonH = DashboardScaleDialogValue(30, state->dpi);
        state->resetDefaultsBtn = CreateWindowExW(
            0, L"BUTTON", L"Reset defaults",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            margin, buttonY, DashboardScaleDialogValue(132, state->dpi), buttonH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_RESET_DEFAULTS), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->resetDefaultsBtn, state->font);
        state->saveSettingsBtn = CreateWindowExW(
            0, L"BUTTON", L"Save as defaults",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            margin + DashboardScaleDialogValue(146, state->dpi), buttonY,
            DashboardScaleDialogValue(132, state->dpi), buttonH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_SAVE_SETTINGS), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->saveSettingsBtn, state->font);
        state->okBtn = CreateWindowExW(
            0, L"BUTTON", L"Start import",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            DashboardScaleDialogValue(546, state->dpi), buttonY, buttonW, buttonH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_OK), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->okBtn, state->font);
        state->cancelBtn = CreateWindowExW(
            0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            DashboardScaleDialogValue(650, state->dpi), buttonY, buttonW, buttonH,
            hwnd, reinterpret_cast<HMENU>(ID_PDF_OPTIONS_CANCEL), GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->cancelBtn, state->font);

        // All controls now exist. Resolve Cloud visibility from the selected OCR
        // mode and perform the first (and only) authoritative initial layout.
        UpdatePdfCloudConsentControlState(state);
        DashboardCenterWindowOnOwner(hwnd, GetWindow(hwnd, GW_OWNER));
        return 0;
    }
    case WM_SIZE:
        if (state) {
            LayoutPdfOptionsDialog(state);
        }
        return 0;
    case WM_DPICHANGED:
        if (state) {
            UINT newDpi = HIWORD(wParam);
            if (newDpi > 0 && newDpi != state->dpi) {
                state->dpi = newDpi;
                if (state->ownsFont && state->font) {
                    DeleteObject(state->font);
                    state->font = nullptr;
                }
                state->font = DashboardCreateDialogFont(20, state->dpi);
                state->ownsFont = state->font != nullptr;
                ApplyPdfOptionsDialogFont(state);
            }
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SIZE nextSize = {
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top
                };
                nextSize = DashboardClampDialogSizeToWorkArea(nextSize, hwnd, state->dpi);
                SetWindowPos(hwnd, nullptr,
                    suggested->left,
                    suggested->top,
                    nextSize.cx,
                    nextSize.cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            LayoutPdfOptionsDialog(state);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (state && state->previewRc.right > state->previewRc.left) {
            if (!DrawImageThumbnail(hdc, state->previewImagePath, state->previewRc, Theme::border)) {
                DrawThumbnailPlaceholder(hdc, state->previewRc, Theme::border, Theme::textMuted);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (state && control == state->outputTreeText) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (state && control == state->outputTreeText) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        break;
    }
    case WM_COMMAND:
        if (!state || !state->options) break;
        if ((LOWORD(wParam) == ID_PDF_OPTIONS_RANGE ||
             LOWORD(wParam) == ID_PDF_OPTIONS_DPI ||
             LOWORD(wParam) == ID_PDF_OPTIONS_MAX_EDGE ||
             LOWORD(wParam) == ID_PDF_OPTIONS_MAX_MP ||
             LOWORD(wParam) == ID_PDF_OPTIONS_QUALITY) &&
            HIWORD(wParam) == EN_CHANGE) {
            UpdatePdfOptionsSelectionSummary(state);
            if (LOWORD(wParam) == ID_PDF_OPTIONS_RANGE) {
                SetTimer(hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER, 350, nullptr);
            }
            return 0;
        }
        if (LOWORD(wParam) == ID_PDF_OPTIONS_FORMAT && HIWORD(wParam) == CBN_SELCHANGE) {
            state->options->pdfImageFormat = GetPdfDialogFormat(state);
            UpdatePdfQualityControlState(state);
            UpdatePdfOptionsSelectionSummary(state);
            UpdatePdfOptionsOutputSummary(state);
            return 0;
        }
        if (LOWORD(wParam) == ID_PDF_OPTIONS_OCR_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
            UpdatePdfCloudConsentControlState(state);
            UpdatePdfOptionsSelectionSummary(state);
            return 0;
        }
        switch (LOWORD(wParam)) {
        case ID_PDF_OPTIONS_PREV_PREVIEW:
            if (state->previewPdfIndex > 0) {
                KillTimer(hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER);
                state->previewPdfIndex--;
                RefreshPdfOptionsPreview(state);
            }
            return 0;
        case ID_PDF_OPTIONS_NEXT_PREVIEW:
            if (state->preflight && state->previewPdfIndex + 1 < state->preflight->size()) {
                KillTimer(hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER);
                state->previewPdfIndex++;
                RefreshPdfOptionsPreview(state);
            }
            return 0;
        case ID_PDF_OPTIONS_CHANGE_ARTIFACTS:
            if (state->options && state->editOutputArtifacts &&
                state->editOutputArtifacts(state->options->outputArtifacts)) {
                UpdatePdfOptionsArtifactSummary(state);
                UpdatePdfOptionsOutputSummary(state);
            }
            return 0;
        case ID_PDF_OPTIONS_BROWSE_OUTPUT: {
            std::wstring selectedRoot;
            if (DashboardSelectOcrOptionsOutputRoot(hwnd, state->outputRoot, nullptr, selectedRoot)) {
                state->outputRoot = selectedRoot;
                UpdatePdfOptionsOutputSummary(state);
                LayoutPdfOptionsDialog(state);
            }
            return 0;
        }
        case ID_PDF_OPTIONS_RESET_DEFAULTS:
            ResetPdfOptionsDialogDefaults(state);
            return 0;
        case ID_PDF_OPTIONS_SAVE_SETTINGS:
            if (!CapturePdfOptionsDialogControls(state)) return 0;
            if (state->saveSettings) {
                state->saveSettings(*state->options);
            }
            return 0;
        case ID_PDF_OPTIONS_OK: {
            if (!CapturePdfOptionsDialogControls(state)) return 0;
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        case ID_PDF_OPTIONS_CANCEL:
            state->accepted = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == ID_PDF_OPTIONS_PREVIEW_TIMER) {
            KillTimer(hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER);
            RefreshPdfOptionsPreview(state);
            return 0;
        }
        break;
    case WM_CLOSE:
        KillTimer(hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER);
        if (state) {
            state->accepted = false;
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        KillTimer(hwnd, ID_PDF_OPTIONS_PREVIEW_TIMER);
        if (state) {
            state->hwnd = nullptr;
            state->done = true;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool DashboardRegisterPdfOptionsDialogClass() {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = PdfOptionsDialogWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wcex.lpszClassName = kDashboardPdfOptionsDialogClass;
    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}


// D-B-R2: modal run owns CreateWindow + message loop. Window only seeds + callbacks.
DashboardPdfOptionsDialogRunResult DashboardRunPdfImportOptionsDialog(
    const DashboardPdfOptionsDialogRunInput& input)
{
    DashboardPdfOptionsDialogRunResult result;
    if (!input.options || !input.preflight) {
        result.dialogFailedOpen = true;
        return result;
    }
    DashboardPdfImportOptions& options = *input.options;
    std::vector<PdfImportPreflightInfo>& preflight = *input.preflight;
    HWND owner = input.owner;
    UINT dpi = input.dpi > 0 ? input.dpi : kDashboardDialogDesignDpi;

    if (!DashboardRegisterPdfOptionsDialogClass()) {
        result.dialogFailedOpen = true;
        // Fallback path: validate only (legacy when class reg fails).
        DashboardValidatePdfOptions(
            owner,
            options.pageRange,
            options.pdfRenderDpi,
            &preflight,
            &result.selectedPageCount);
        return result;
    }

    PdfOptionsDialogState state;
    state.options = &options;
    state.editOutputArtifacts = input.editOutputArtifacts;
    state.preflight = &preflight;
    if (input.outputRoot) state.outputRoot = *input.outputRoot;
    state.pdfCount = input.pdfs ? (int)input.pdfs->size() : 0;
    state.totalPageCount = input.totalPageCount;
    state.dpi = dpi;
    state.font = DashboardCreateDialogFont(20, state.dpi);
    state.ownsFont = state.font != nullptr;
    if (!state.font) {
        state.font = input.fallbackFont;
        state.ownsFont = false;
    }
    state.saveSettings = input.saveSettings;

    DashboardPreparePdfOptionsPreview(
        preflight,
        options.pageRange.empty() ? L"all" : options.pageRange,
        state.previewPdfIndex,
        state.previewTempDir,
        state.previewImagePath,
        state.previewCaption);

    SIZE dialogSize = DashboardClampDialogSizeToWorkArea(
        DashboardGetPdfOptionsDialogWindowSize(state.dpi), owner, state.dpi);
    int w = dialogSize.cx;
    int h = dialogSize.cy;

    BOOL parentWasEnabled = owner ? IsWindowEnabled(owner) : FALSE;
    if (owner) EnableWindow(owner, FALSE);

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kPdfOptionsDialogClass,
        L"PDF options",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        owner, nullptr, GetModuleHandleW(nullptr), &state);

    if (!dialog) {
        if (owner && parentWasEnabled) EnableWindow(owner, TRUE);
        if (state.ownsFont) DeleteObject(state.font);
        DashboardDeletePdfPreviewTemp(state.previewImagePath, state.previewTempDir);
        result.dialogFailedOpen = true;
        return result;
    }

    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    // Optional test-drive injection (product builds pass null).
    if (input.testDrive && input.testDrive->enabled) {
        const DashboardPdfImportOptions& driven = input.testDrive->drivenOptions;
        if (state.rangeEdit) {
            std::wstring range = driven.pageRange.empty() ? L"all" : driven.pageRange;
            SetWindowTextW(state.rangeEdit, range.c_str());
        }
        if (state.dpiEdit) {
            int dpiValue = driven.pdfRenderDpi > 0 ? driven.pdfRenderDpi : kDefaultPdfRenderDpi;
            SetWindowTextW(state.dpiEdit, WideFormatIntLabel(dpiValue).c_str());
        }
        if (state.maxEdgeEdit) {
            SetWindowTextW(state.maxEdgeEdit, WideFormatIntLabel(driven.pdfMaxPixelEdge).c_str());
        }
        if (state.maxMpEdit) {
            SetWindowTextW(state.maxMpEdit, WideFormatIntLabel(driven.pdfMaxMegapixels).c_str());
        }
        if (state.formatCombo) {
            SendMessageW(
                state.formatCombo,
                CB_SETCURSEL,
                PdfDialogFormatToComboIndex(driven.pdfImageFormat),
                0);
        }
        if (state.qualityEdit) {
            SetWindowTextW(state.qualityEdit, WideFormatIntLabel(driven.pdfImageQuality).c_str());
        }
        if (state.modeCombo && !driven.ocrMode.empty()) {
            SendMessageW(
                state.modeCombo,
                CB_SETCURSEL,
                DashboardOcrModeToComboIndex(driven.ocrMode),
                0);
            UpdatePdfCloudConsentControlState(&state);
        }
        if (state.cloudConsentCheck) {
            CheckDlgButton(
                dialog,
                ID_PDF_OPTIONS_CLOUD_CONSENT,
                driven.rememberCloudFullPdfConsent ? BST_CHECKED : BST_UNCHECKED);
        }
        if (input.testDrive->saveThenCancel) {
            HWND saveButton = GetDlgItem(dialog, ID_PDF_OPTIONS_SAVE_SETTINGS);
            SendMessageW(
                dialog,
                WM_COMMAND,
                MAKEWPARAM(ID_PDF_OPTIONS_SAVE_SETTINGS, BN_CLICKED),
                reinterpret_cast<LPARAM>(saveButton));
            if (input.testDrive->saveStayedOpenOut) {
                *input.testDrive->saveStayedOpenOut =
                    IsWindow(dialog) && state.hwnd == dialog && !state.done;
            }
            HWND cancelButton = GetDlgItem(dialog, ID_PDF_OPTIONS_CANCEL);
            SendMessageW(
                dialog,
                WM_COMMAND,
                MAKEWPARAM(ID_PDF_OPTIONS_CANCEL, BN_CLICKED),
                reinterpret_cast<LPARAM>(cancelButton));
        } else {
            const int commandId = input.testDrive->cancel
                ? ID_PDF_OPTIONS_CANCEL
                : ID_PDF_OPTIONS_OK;
            HWND commandSource = GetDlgItem(dialog, commandId);
            SendMessageW(
                dialog,
                WM_COMMAND,
                MAKEWPARAM(commandId, BN_CLICKED),
                reinterpret_cast<LPARAM>(commandSource));
        }
    }

    MSG msg = {};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (state.hwnd && IsWindow(state.hwnd)) {
        DestroyWindow(state.hwnd);
    }
    if (owner && parentWasEnabled) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (state.ownsFont) DeleteObject(state.font);
    DashboardDeletePdfPreviewTemp(state.previewImagePath, state.previewTempDir);

    result.accepted = state.accepted;
    if (state.accepted && input.outputRoot) {
        *input.outputRoot = state.outputRoot;
    }
    if (state.accepted) {
        DashboardValidatePdfOptions(
            owner,
            options.pageRange,
            options.pdfRenderDpi,
            &preflight,
            &result.selectedPageCount);
    }
    return result;
}
