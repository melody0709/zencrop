#include "ocr/ui/dashboard/DashboardOutputArtifactOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardDialogLayout.h"
#include "ocr/ui/dashboard/DashboardFolderImportOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "BatchOcrTypes.h"
#include "PdfRenderOptions.h"
#include "core/WideStringUtils.h"
#include "Strings.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <string>

#define ID_OUTPUT_OPTIONS_ROOT 1240
#define ID_OUTPUT_OPTIONS_BROWSE 1241
#define ID_OUTPUT_OPTIONS_PROFILE 1242
#define ID_OUTPUT_OPTIONS_LAYOUT_ENABLED 1243
#define ID_OUTPUT_OPTIONS_LAYOUT_FORMAT 1244
#define ID_OUTPUT_OPTIONS_LAYOUT_QUALITY 1245
#define ID_OUTPUT_OPTIONS_THUMB_POLICY 1246
#define ID_OUTPUT_OPTIONS_THUMB_FORMAT 1247
#define ID_OUTPUT_OPTIONS_THUMB_QUALITY 1248
#define ID_OUTPUT_OPTIONS_THUMB_EDGE 1249
#define ID_OUTPUT_OPTIONS_RESET 1250
#define ID_OUTPUT_OPTIONS_SAVE 1251
#define ID_OUTPUT_OPTIONS_CANCEL 1252
#define ID_OUTPUT_OPTIONS_ASSET_FORMAT 1253
#define ID_OUTPUT_OPTIONS_ASSET_QUALITY 1254

static constexpr const wchar_t* kOutputArtifactOptionsDialogClass =
    L"ZenCrop.OcrDashboard.OutputArtifactOptions";

struct OutputArtifactOptionsDialogState {
    HWND hwnd = nullptr;
    HWND rootLabel = nullptr;
    HWND rootEdit = nullptr;
    HWND rootBrowseBtn = nullptr;
    HWND profileLabel = nullptr;
    HWND profileCombo = nullptr;
    HWND layoutCheck = nullptr;
    HWND layoutFormatLabel = nullptr;
    HWND layoutFormatCombo = nullptr;
    HWND layoutQualityLabel = nullptr;
    HWND layoutQualityEdit = nullptr;
    HWND thumbnailPolicyLabel = nullptr;
    HWND thumbnailPolicyCombo = nullptr;
    HWND thumbnailFormatLabel = nullptr;
    HWND thumbnailFormatCombo = nullptr;
    HWND thumbnailQualityLabel = nullptr;
    HWND thumbnailQualityEdit = nullptr;
    HWND thumbnailEdgeLabel = nullptr;
    HWND thumbnailEdgeEdit = nullptr;
    HWND embeddedAssetLabel = nullptr;
    HWND embeddedAssetFormatCombo = nullptr;
    HWND embeddedAssetQualityLabel = nullptr;
    HWND embeddedAssetQualityEdit = nullptr;
    HWND hintText = nullptr;
    HWND resetBtn = nullptr;
    HWND saveBtn = nullptr;
    HWND cancelBtn = nullptr;
    HFONT font = nullptr;
    bool ownsFont = false;
    UINT dpi = kDashboardDialogDesignDpi;
    OcrOutputArtifactOptions* options = nullptr;
    std::wstring* outputRoot = nullptr;
    bool showOutputRoot = false;
    bool accepted = false;
    bool done = false;
    int scrollY = 0;
    int contentHeight = 0;
};


void DashboardApplyOutputArtifactProfile(
    OcrOutputArtifactOptions& options,
    DashboardOutputArtifactProfile profile)
{
    switch (profile) {
    case DashboardOutputArtifactProfile::Review:
        options.writeLayoutPreview = true;
        options.layoutPreviewFormat = PdfRenderImageFormat::WebP;
        options.layoutPreviewQuality = 85;
        options.pdfThumbnailPolicy = PdfThumbnailPolicy::Auto;
        options.pdfThumbnailFormat = PdfRenderImageFormat::WebP;
        options.pdfThumbnailQuality = 85;
        options.pdfThumbnailMaxPixelEdge = 768;
        options.embeddedAssetFormat = PdfRenderImageFormat::Auto;
        options.embeddedAssetQuality = 90;
        break;
    case DashboardOutputArtifactProfile::LosslessDebug:
        options.writeLayoutPreview = true;
        options.layoutPreviewFormat = PdfRenderImageFormat::Png;
        options.layoutPreviewQuality = 100;
        options.pdfThumbnailPolicy = PdfThumbnailPolicy::Always;
        options.pdfThumbnailFormat = PdfRenderImageFormat::Png;
        options.pdfThumbnailQuality = 100;
        options.pdfThumbnailMaxPixelEdge = 768;
        options.embeddedAssetFormat = PdfRenderImageFormat::Png;
        options.embeddedAssetQuality = 100;
        break;
    case DashboardOutputArtifactProfile::Compact:
    default:
        options.writeLayoutPreview = false;
        options.layoutPreviewFormat = PdfRenderImageFormat::WebP;
        options.layoutPreviewQuality = 85;
        options.pdfThumbnailPolicy = PdfThumbnailPolicy::Auto;
        options.pdfThumbnailFormat = PdfRenderImageFormat::WebP;
        options.pdfThumbnailQuality = 80;
        options.pdfThumbnailMaxPixelEdge = 512;
        options.embeddedAssetFormat = PdfRenderImageFormat::Auto;
        options.embeddedAssetQuality = 90;
        break;
    }
    options = NormalizeOcrOutputArtifactOptions(options);
}

bool DashboardOutputArtifactOptionsEqual(
    const OcrOutputArtifactOptions& left,
    const OcrOutputArtifactOptions& right)
{
    const auto a = NormalizeOcrOutputArtifactOptions(left);
    const auto b = NormalizeOcrOutputArtifactOptions(right);
    return a.writeLayoutPreview == b.writeLayoutPreview &&
        a.layoutPreviewFormat == b.layoutPreviewFormat &&
        a.layoutPreviewQuality == b.layoutPreviewQuality &&
        a.pdfThumbnailPolicy == b.pdfThumbnailPolicy &&
        a.pdfThumbnailFormat == b.pdfThumbnailFormat &&
        a.pdfThumbnailQuality == b.pdfThumbnailQuality &&
        a.pdfThumbnailMaxPixelEdge == b.pdfThumbnailMaxPixelEdge &&
        a.embeddedAssetFormat == b.embeddedAssetFormat &&
        a.embeddedAssetQuality == b.embeddedAssetQuality;
}

DashboardOutputArtifactProfile DashboardOutputArtifactProfileFor(
    const OcrOutputArtifactOptions& options)
{
    OcrOutputArtifactOptions preset;
    DashboardApplyOutputArtifactProfile(preset, DashboardOutputArtifactProfile::Compact);
    if (DashboardOutputArtifactOptionsEqual(options, preset)) return DashboardOutputArtifactProfile::Compact;
    DashboardApplyOutputArtifactProfile(preset, DashboardOutputArtifactProfile::Review);
    if (DashboardOutputArtifactOptionsEqual(options, preset)) return DashboardOutputArtifactProfile::Review;
    DashboardApplyOutputArtifactProfile(preset, DashboardOutputArtifactProfile::LosslessDebug);
    if (DashboardOutputArtifactOptionsEqual(options, preset)) return DashboardOutputArtifactProfile::LosslessDebug;
    return DashboardOutputArtifactProfile::Custom;
}

std::wstring DashboardFormatOutputArtifactSummary(const OcrOutputArtifactOptions& source) {
    const OcrOutputArtifactOptions options = NormalizeOcrOutputArtifactOptions(source);
    const wchar_t* profile = L"Custom";
    switch (DashboardOutputArtifactProfileFor(options)) {
    case DashboardOutputArtifactProfile::Compact: profile = L"Compact"; break;
    case DashboardOutputArtifactProfile::Review: profile = L"Review"; break;
    case DashboardOutputArtifactProfile::LosslessDebug: profile = L"Lossless"; break;
    case DashboardOutputArtifactProfile::Custom: break;
    }
    std::wstring text = profile;
    switch (options.pdfThumbnailPolicy) {
    case PdfThumbnailPolicy::Never:
        text += L" · no cover";
        break;
    case PdfThumbnailPolicy::Always:
        text += L" · cover on";
        break;
    case PdfThumbnailPolicy::Auto:
    default:
        text += L" · auto cover";
        break;
    }
    if (!options.writeLayoutPreview) {
        text += L" · no layout";
    } else {
        text += L" · ";
        text += PdfRenderImageFormatToString(options.layoutPreviewFormat);
        text += L" layout";
    }
    text += L" | assets ";
    if (options.embeddedAssetFormat == PdfRenderImageFormat::Auto) {
        text += L"auto";
    } else {
        text += PdfRenderImageFormatToString(options.embeddedAssetFormat);
        if (options.embeddedAssetFormat == PdfRenderImageFormat::Jpeg ||
            options.embeddedAssetFormat == PdfRenderImageFormat::WebP) {
            text += L" q";
            // OWN-123: pure int labels (WideStringUtils).
            text += WideFormatIntLabel(options.embeddedAssetQuality);
        }
    }
    return text;
}

std::wstring DashboardFormatOutputArtifactToolbarLabel(const OcrOutputArtifactOptions& source) {
    const bool zh = S::IsChinese();
    const OcrOutputArtifactOptions options = NormalizeOcrOutputArtifactOptions(source);
    const wchar_t* profile = zh ? L"自定义" : L"Custom";
    switch (DashboardOutputArtifactProfileFor(options)) {
    case DashboardOutputArtifactProfile::Compact:
        profile = zh ? L"紧凑" : L"Compact";
        break;
    case DashboardOutputArtifactProfile::Review:
        profile = zh ? L"审阅" : L"Review";
        break;
    case DashboardOutputArtifactProfile::LosslessDebug:
        profile = zh ? L"无损调试" : L"Lossless";
        break;
    case DashboardOutputArtifactProfile::Custom:
        break;
    }
    return std::wstring(zh ? L"输出：" : L"Output: ") + profile;
}

static void PopulateOutputArtifactFormatCombo(HWND combo, PdfRenderImageFormat format) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"WebP"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"JPEG"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PNG"));
    const PdfRenderImageFormat normalized = NormalizeArtifactImageFormat(format);
    int selection = normalized == PdfRenderImageFormat::Jpeg ? 1 :
        normalized == PdfRenderImageFormat::Png ? 2 : 0;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

static PdfRenderImageFormat OutputArtifactFormatFromCombo(HWND combo) {
    const int selection = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 0;
    if (selection == 1) return PdfRenderImageFormat::Jpeg;
    if (selection == 2) return PdfRenderImageFormat::Png;
    return PdfRenderImageFormat::WebP;
}

static void PopulateEmbeddedAssetFormatCombo(HWND combo, PdfRenderImageFormat format) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (seal PNG, images JPEG)"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"WebP (convert)"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"JPEG (convert)"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PNG (convert)"));
    const PdfRenderImageFormat normalized = NormalizeOcrEmbeddedAssetImageFormat(format);
    const int selection = normalized == PdfRenderImageFormat::WebP ? 1 :
        normalized == PdfRenderImageFormat::Jpeg ? 2 :
        normalized == PdfRenderImageFormat::Png ? 3 : 0;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

static PdfRenderImageFormat EmbeddedAssetFormatFromCombo(HWND combo) {
    const int selection = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 0;
    if (selection == 1) return PdfRenderImageFormat::WebP;
    if (selection == 2) return PdfRenderImageFormat::Jpeg;
    if (selection == 3) return PdfRenderImageFormat::Png;
    return PdfRenderImageFormat::Auto;
}

static void PopulateOutputArtifactThumbnailPolicyCombo(HWND combo, PdfThumbnailPolicy policy) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (reuse page image when possible)"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Always save cover"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Never save cover"));
    const int selection = policy == PdfThumbnailPolicy::Always ? 1 :
        policy == PdfThumbnailPolicy::Never ? 2 : 0;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

static PdfThumbnailPolicy OutputArtifactThumbnailPolicyFromCombo(HWND combo) {
    const int selection = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 0;
    if (selection == 1) return PdfThumbnailPolicy::Always;
    if (selection == 2) return PdfThumbnailPolicy::Never;
    return PdfThumbnailPolicy::Auto;
}

static void PopulateOutputArtifactProfileCombo(HWND combo, DashboardOutputArtifactProfile profile) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Compact (recommended)"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Review"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Lossless debug"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom"));
    SendMessageW(combo, CB_SETCURSEL, static_cast<int>(profile), 0);
}

static void SetOutputArtifactOptionsDialogControls(
    OutputArtifactOptionsDialogState* state,
    const OcrOutputArtifactOptions& source)
{
    if (!state) return;
    const OcrOutputArtifactOptions options = NormalizeOcrOutputArtifactOptions(source);
    CheckDlgButton(state->hwnd, ID_OUTPUT_OPTIONS_LAYOUT_ENABLED,
        options.writeLayoutPreview ? BST_CHECKED : BST_UNCHECKED);
    PopulateOutputArtifactFormatCombo(state->layoutFormatCombo, options.layoutPreviewFormat);
    // OWN-123: pure int labels (WideStringUtils).
    SetWindowTextW(state->layoutQualityEdit, WideFormatIntLabel(options.layoutPreviewQuality).c_str());
    PopulateOutputArtifactThumbnailPolicyCombo(state->thumbnailPolicyCombo, options.pdfThumbnailPolicy);
    PopulateOutputArtifactFormatCombo(state->thumbnailFormatCombo, options.pdfThumbnailFormat);
    SetWindowTextW(state->thumbnailQualityEdit, WideFormatIntLabel(options.pdfThumbnailQuality).c_str());
    SetWindowTextW(state->thumbnailEdgeEdit, WideFormatIntLabel(options.pdfThumbnailMaxPixelEdge).c_str());
    PopulateEmbeddedAssetFormatCombo(state->embeddedAssetFormatCombo, options.embeddedAssetFormat);
    SetWindowTextW(state->embeddedAssetQualityEdit, WideFormatIntLabel(options.embeddedAssetQuality).c_str());
    PopulateOutputArtifactProfileCombo(state->profileCombo, DashboardOutputArtifactProfileFor(options));
}

static OcrOutputArtifactOptions CaptureOutputArtifactOptionsDialogControls(
    OutputArtifactOptionsDialogState* state)
{
    OcrOutputArtifactOptions options;
    if (!state) return options;
    options.writeLayoutPreview = IsDlgButtonChecked(
        state->hwnd, ID_OUTPUT_OPTIONS_LAYOUT_ENABLED) == BST_CHECKED;
    options.layoutPreviewFormat = OutputArtifactFormatFromCombo(state->layoutFormatCombo);
    // OWN-77: pure int parse (WideStringUtils).
    options.layoutPreviewQuality = WideParseJsonIntToken(DashboardGetWindowTextWide(state->layoutQualityEdit));
    options.pdfThumbnailPolicy = OutputArtifactThumbnailPolicyFromCombo(state->thumbnailPolicyCombo);
    options.pdfThumbnailFormat = OutputArtifactFormatFromCombo(state->thumbnailFormatCombo);
    options.pdfThumbnailQuality = WideParseJsonIntToken(DashboardGetWindowTextWide(state->thumbnailQualityEdit));
    options.pdfThumbnailMaxPixelEdge = static_cast<uint32_t>(
        (std::max)(0, WideParseJsonIntToken(DashboardGetWindowTextWide(state->thumbnailEdgeEdit))));
    options.embeddedAssetFormat = EmbeddedAssetFormatFromCombo(state->embeddedAssetFormatCombo);
    options.embeddedAssetQuality = WideParseJsonIntToken(DashboardGetWindowTextWide(state->embeddedAssetQualityEdit));
    return NormalizeOcrOutputArtifactOptions(options);
}

static void UpdateOutputArtifactOptionsDialogEnablement(OutputArtifactOptionsDialogState* state) {
    if (!state) return;
    const bool layoutEnabled = IsDlgButtonChecked(
        state->hwnd, ID_OUTPUT_OPTIONS_LAYOUT_ENABLED) == BST_CHECKED;
    const PdfRenderImageFormat layoutFormat = OutputArtifactFormatFromCombo(state->layoutFormatCombo);
    const PdfThumbnailPolicy thumbnailPolicy = OutputArtifactThumbnailPolicyFromCombo(state->thumbnailPolicyCombo);
    const bool thumbnailEnabled = thumbnailPolicy != PdfThumbnailPolicy::Never;
    const PdfRenderImageFormat thumbnailFormat = OutputArtifactFormatFromCombo(state->thumbnailFormatCombo);
    const PdfRenderImageFormat embeddedAssetFormat =
        EmbeddedAssetFormatFromCombo(state->embeddedAssetFormatCombo);
    EnableWindow(state->layoutFormatCombo, layoutEnabled);
    EnableWindow(state->layoutQualityEdit, layoutEnabled && layoutFormat != PdfRenderImageFormat::Png);
    EnableWindow(state->thumbnailFormatCombo, thumbnailEnabled);
    EnableWindow(state->thumbnailQualityEdit, thumbnailEnabled && thumbnailFormat != PdfRenderImageFormat::Png);
    EnableWindow(state->thumbnailEdgeEdit, thumbnailEnabled);
    EnableWindow(
        state->embeddedAssetQualityEdit,
        embeddedAssetFormat == PdfRenderImageFormat::Jpeg ||
            embeddedAssetFormat == PdfRenderImageFormat::WebP);
}

static void SyncOutputArtifactOptionsDialogProfile(OutputArtifactOptionsDialogState* state) {
    if (!state) return;
    const OcrOutputArtifactOptions options = CaptureOutputArtifactOptionsDialogControls(state);
    SendMessageW(state->profileCombo, CB_SETCURSEL,
        static_cast<WPARAM>(DashboardOutputArtifactProfileFor(options)), 0);
    UpdateOutputArtifactOptionsDialogEnablement(state);
}

static void ApplyOutputArtifactOptionsDialogFont(OutputArtifactOptionsDialogState* state) {
    if (!state) return;
    const HWND controls[] = {
        state->rootLabel,
        state->rootEdit,
        state->rootBrowseBtn,
        state->profileLabel,
        state->profileCombo,
        state->layoutCheck,
        state->layoutFormatLabel,
        state->layoutFormatCombo,
        state->layoutQualityLabel,
        state->layoutQualityEdit,
        state->thumbnailPolicyLabel,
        state->thumbnailPolicyCombo,
        state->thumbnailFormatLabel,
        state->thumbnailFormatCombo,
        state->thumbnailQualityLabel,
        state->thumbnailQualityEdit,
        state->thumbnailEdgeLabel,
        state->thumbnailEdgeEdit,
        state->embeddedAssetLabel,
        state->embeddedAssetFormatCombo,
        state->embeddedAssetQualityLabel,
        state->embeddedAssetQualityEdit,
        state->hintText,
        state->resetBtn,
        state->saveBtn,
        state->cancelBtn
    };
    for (HWND control : controls) {
        DashboardSetControlFont(control, state->font);
    }
}

static int GetOutputArtifactOptionsDialogLineHeight(OutputArtifactOptionsDialogState* state) {
    int fallback = DashboardScaleDialogValue(24, state ? state->dpi : kDashboardDialogDesignDpi);
    if (!state || !state->hwnd || !state->font) return fallback;

    HDC hdc = GetDC(state->hwnd);
    if (!hdc) return fallback;

    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, state->font));
    TEXTMETRICW tm = {};
    int lineH = fallback;
    if (GetTextMetricsW(hdc, &tm)) {
        lineH = max(lineH, tm.tmHeight + tm.tmExternalLeading);
    }
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(state->hwnd, hdc);
    return lineH;
}

static void LayoutOutputArtifactOptionsDialog(OutputArtifactOptionsDialogState* state) {
    if (!state || !state->hwnd) return;

    RECT client = {};
    GetClientRect(state->hwnd, &client);
    const int clientW = max(1, client.right - client.left);
    const int clientH = max(1, client.bottom - client.top);
    const int margin = DashboardScaleDialogValue(18, state->dpi);
    const int labelW = min(DashboardScaleDialogValue(100, state->dpi),
        max(DashboardScaleDialogValue(64, state->dpi), clientW / 4));
    const int editX = margin + labelW + DashboardScaleDialogValue(8, state->dpi);
    const int right = max(editX + 1, clientW - margin);
    const int fieldW = max(1, right - editX);
    const int lineH = GetOutputArtifactOptionsDialogLineHeight(state);
    const int editH = max(DashboardScaleDialogValue(28, state->dpi),
        lineH + DashboardScaleDialogValue(2, state->dpi));
    const int rowGap = DashboardScaleDialogValue(18, state->dpi);
    const int smallGap = DashboardScaleDialogValue(8, state->dpi);
    const int childIndent = DashboardScaleDialogValue(24, state->dpi);
    const int formatLabelW = DashboardScaleDialogValue(74, state->dpi);
    const int formatComboW = min(DashboardScaleDialogValue(126, state->dpi), fieldW);
    const int qualityLabelW = DashboardScaleDialogValue(64, state->dpi);
    const int qualityEditW = DashboardScaleDialogValue(72, state->dpi);
    const int edgeLabelW = DashboardScaleDialogValue(70, state->dpi);
    const int edgeEditW = DashboardScaleDialogValue(72, state->dpi);

    int y = margin;
    int rootY = -1;
    if (state->showOutputRoot) {
        rootY = y;
        y += editH + rowGap;
    }
    const int profileY = y;
    y += editH + rowGap;
    const int layoutCheckY = y;
    y += editH + DashboardScaleDialogValue(6, state->dpi);
    const int layoutFieldsY = y;
    y += editH + rowGap;
    const int thumbnailPolicyY = y;
    y += editH + DashboardScaleDialogValue(14, state->dpi);
    const int thumbnailFieldsY = y;
    y += editH + DashboardScaleDialogValue(14, state->dpi);
    const int embeddedAssetY = y;
    y += editH + rowGap;
    const int hintY = y;
    const int hintH = DashboardScaleDialogValue(64, state->dpi);
    y += hintH;
    const int minimumButtonsY = y;
    state->contentHeight = minimumButtonsY + editH + margin;

    const int maxScroll = max(0, state->contentHeight - clientH);
    state->scrollY = min(max(0, state->scrollY), maxScroll);
    SCROLLINFO scroll = { sizeof(scroll) };
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMin = 0;
    scroll.nMax = max(0, state->contentHeight - 1);
    scroll.nPage = static_cast<UINT>(clientH);
    scroll.nPos = state->scrollY;
    SetScrollInfo(state->hwnd, SB_VERT, &scroll, TRUE);

    // Keep the action buttons flush with the lower margin whenever the dialog
    // is tall enough. On constrained work areas they join the scrollable content
    // instead, so every control remains reachable.
    const int buttonsY = max(
        minimumButtonsY,
        clientH - margin - editH + state->scrollY);

    const auto top = [state](int absoluteY) {
        return absoluteY - state->scrollY;
    };
    const auto move = [&](HWND control, int x, int absoluteY, int width, int height) {
        if (control) MoveWindow(control, x, top(absoluteY), max(1, width), max(1, height), TRUE);
    };
    const int browseW = min(DashboardScaleDialogValue(94, state->dpi),
        max(DashboardScaleDialogValue(72, state->dpi), fieldW / 4));
    const int rootEditW = max(1, fieldW - browseW - smallGap);
    const int layoutQualityLabelX = min(
        editX + formatComboW + DashboardScaleDialogValue(16, state->dpi),
        right - qualityLabelW - qualityEditW - smallGap);
    const int layoutQualityEditX = min(
        layoutQualityLabelX + qualityLabelW + smallGap,
        right - qualityEditW);
    const int thumbnailQualityLabelX = layoutQualityLabelX;
    const int thumbnailQualityEditX = layoutQualityEditX;
    const int thumbnailEdgeLabelX = min(
        thumbnailQualityEditX + qualityEditW + DashboardScaleDialogValue(18, state->dpi),
        right - edgeLabelW - edgeEditW - smallGap);
    const int thumbnailEdgeEditX = min(
        thumbnailEdgeLabelX + edgeLabelW + smallGap,
        right - edgeEditW);
    const int embeddedAssetComboW = min(DashboardScaleDialogValue(250, state->dpi), fieldW);
    const int embeddedAssetQualityLabelX = min(
        editX + embeddedAssetComboW + DashboardScaleDialogValue(16, state->dpi),
        right - qualityLabelW - qualityEditW - smallGap);
    const int embeddedAssetQualityEditX = min(
        embeddedAssetQualityLabelX + qualityLabelW + smallGap,
        right - qualityEditW);

    if (rootY >= 0) {
        move(state->rootLabel, margin, rootY + DashboardScaleDialogValue(5, state->dpi), labelW, editH);
        move(state->rootEdit, editX, rootY, rootEditW, editH);
        move(state->rootBrowseBtn, editX + rootEditW + smallGap, rootY, browseW, editH);
    }
    move(state->profileLabel, margin, profileY + DashboardScaleDialogValue(5, state->dpi), labelW, editH);
    move(state->profileCombo, editX, profileY, fieldW, editH * 5);

    move(state->layoutCheck, margin, layoutCheckY, min(fieldW + labelW, DashboardScaleDialogValue(390, state->dpi)), editH);
    move(state->layoutFormatLabel, margin + childIndent,
        layoutFieldsY + DashboardScaleDialogValue(5, state->dpi), formatLabelW, editH);
    move(state->layoutFormatCombo, editX, layoutFieldsY, formatComboW, editH * 4);
    move(state->layoutQualityLabel, layoutQualityLabelX,
        layoutFieldsY + DashboardScaleDialogValue(5, state->dpi), qualityLabelW, editH);
    move(state->layoutQualityEdit, layoutQualityEditX, layoutFieldsY, qualityEditW, editH);

    move(state->thumbnailPolicyLabel, margin,
        thumbnailPolicyY + DashboardScaleDialogValue(5, state->dpi), labelW, editH);
    move(state->thumbnailPolicyCombo, editX, thumbnailPolicyY, fieldW, editH * 4);
    move(state->thumbnailFormatLabel, margin + childIndent,
        thumbnailFieldsY + DashboardScaleDialogValue(5, state->dpi), formatLabelW, editH);
    move(state->thumbnailFormatCombo, editX, thumbnailFieldsY, formatComboW, editH * 4);
    move(state->thumbnailQualityLabel, thumbnailQualityLabelX,
        thumbnailFieldsY + DashboardScaleDialogValue(5, state->dpi), qualityLabelW, editH);
    move(state->thumbnailQualityEdit, thumbnailQualityEditX, thumbnailFieldsY, qualityEditW, editH);
    move(state->thumbnailEdgeLabel, thumbnailEdgeLabelX,
        thumbnailFieldsY + DashboardScaleDialogValue(5, state->dpi), edgeLabelW, editH);
    move(state->thumbnailEdgeEdit, thumbnailEdgeEditX, thumbnailFieldsY, edgeEditW, editH);

    move(state->embeddedAssetLabel, margin,
        embeddedAssetY + DashboardScaleDialogValue(5, state->dpi), labelW, editH);
    move(state->embeddedAssetFormatCombo, editX, embeddedAssetY, embeddedAssetComboW, editH * 5);
    move(state->embeddedAssetQualityLabel, embeddedAssetQualityLabelX,
        embeddedAssetY + DashboardScaleDialogValue(5, state->dpi), qualityLabelW, editH);
    move(state->embeddedAssetQualityEdit, embeddedAssetQualityEditX,
        embeddedAssetY, qualityEditW, editH);

    move(state->hintText, margin, hintY, max(1, clientW - margin * 2), hintH);
    const int cancelW = DashboardScaleDialogValue(86, state->dpi);
    const int saveW = DashboardScaleDialogValue(86, state->dpi);
    const int cancelX = right - cancelW;
    const int saveX = cancelX - smallGap - saveW;
    move(state->resetBtn, margin, buttonsY, DashboardScaleDialogValue(162, state->dpi), editH);
    move(state->saveBtn, saveX, buttonsY, saveW, editH);
    move(state->cancelBtn, cancelX, buttonsY, cancelW, editH);
}

static LRESULT CALLBACK OutputArtifactOptionsDialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<OutputArtifactOptionsDialogState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<OutputArtifactOptionsDialogState*>(cs ? cs->lpCreateParams : nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state || !state->options) return -1;
        state->hwnd = hwnd;
        const int margin = DashboardScaleDialogValue(18, state->dpi);
        const int labelW = DashboardScaleDialogValue(100, state->dpi);
        const int editX = margin + labelW + DashboardScaleDialogValue(8, state->dpi);
        const int fullFieldW = DashboardScaleDialogValue(494, state->dpi);
        const int rootEditW = DashboardScaleDialogValue(392, state->dpi);
        const int editH = DashboardScaleDialogValue(28, state->dpi);
        int y = margin;
        auto make = [&](const wchar_t* klass, const wchar_t* text, DWORD style, int x, int top, int w, int h, int id) {
            HWND control = CreateWindowExW(
                (WideEquals(klass, L"EDIT") ? WS_EX_CLIENTEDGE : 0),
                klass, text, WS_CHILD | WS_VISIBLE | style,
                x, top, w, h, hwnd,
                id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
                GetModuleHandleW(nullptr), nullptr);
            DashboardSetControlFont(control, state->font);
            return control;
        };
        if (state->showOutputRoot) {
            state->rootLabel = make(L"STATIC", L"Folder", 0, margin, y + 5, labelW, editH, 0);
            state->rootEdit = make(L"EDIT", state->outputRoot ? state->outputRoot->c_str() : L"", ES_AUTOHSCROLL | WS_TABSTOP,
                editX, y, rootEditW, editH, ID_OUTPUT_OPTIONS_ROOT);
            state->rootBrowseBtn = make(L"BUTTON", L"Browse...", WS_TABSTOP,
                editX + rootEditW + DashboardScaleDialogValue(8, state->dpi), y, DashboardScaleDialogValue(94, state->dpi), editH, ID_OUTPUT_OPTIONS_BROWSE);
            y += editH + DashboardScaleDialogValue(18, state->dpi);
        }
        state->profileLabel = make(L"STATIC", L"Profile", 0, margin, y + 5, labelW, editH, 0);
        state->profileCombo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            editX, y, fullFieldW, editH * 5, ID_OUTPUT_OPTIONS_PROFILE);
        y += editH + DashboardScaleDialogValue(18, state->dpi);

        state->layoutCheck = make(L"BUTTON", L"Generate layout preview image", BS_AUTOCHECKBOX | WS_TABSTOP,
            margin, y, DashboardScaleDialogValue(390, state->dpi), editH, ID_OUTPUT_OPTIONS_LAYOUT_ENABLED);
        y += editH + DashboardScaleDialogValue(6, state->dpi);
        state->layoutFormatLabel = make(L"STATIC", L"Format", 0, margin + DashboardScaleDialogValue(24, state->dpi), y + 5, DashboardScaleDialogValue(74, state->dpi), editH, 0);
        state->layoutFormatCombo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            editX, y, DashboardScaleDialogValue(126, state->dpi), editH * 4, ID_OUTPUT_OPTIONS_LAYOUT_FORMAT);
        state->layoutQualityLabel = make(L"STATIC", L"Quality", 0, editX + DashboardScaleDialogValue(142, state->dpi), y + 5, DashboardScaleDialogValue(64, state->dpi), editH, 0);
        state->layoutQualityEdit = make(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX + DashboardScaleDialogValue(214, state->dpi), y, DashboardScaleDialogValue(72, state->dpi), editH, ID_OUTPUT_OPTIONS_LAYOUT_QUALITY);
        y += editH + DashboardScaleDialogValue(18, state->dpi);

        state->thumbnailPolicyLabel = make(L"STATIC", L"PDF cover", 0, margin, y + 5, labelW, editH, 0);
        state->thumbnailPolicyCombo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            editX, y, fullFieldW, editH * 4, ID_OUTPUT_OPTIONS_THUMB_POLICY);
        y += editH + DashboardScaleDialogValue(14, state->dpi);
        state->thumbnailFormatLabel = make(L"STATIC", L"Format", 0, margin + DashboardScaleDialogValue(24, state->dpi), y + 5, DashboardScaleDialogValue(74, state->dpi), editH, 0);
        state->thumbnailFormatCombo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            editX, y, DashboardScaleDialogValue(126, state->dpi), editH * 4, ID_OUTPUT_OPTIONS_THUMB_FORMAT);
        state->thumbnailQualityLabel = make(L"STATIC", L"Quality", 0, editX + DashboardScaleDialogValue(142, state->dpi), y + 5, DashboardScaleDialogValue(64, state->dpi), editH, 0);
        state->thumbnailQualityEdit = make(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX + DashboardScaleDialogValue(214, state->dpi), y, DashboardScaleDialogValue(72, state->dpi), editH, ID_OUTPUT_OPTIONS_THUMB_QUALITY);
        state->thumbnailEdgeLabel = make(L"STATIC", L"Max edge", 0, editX + DashboardScaleDialogValue(304, state->dpi), y + 5, DashboardScaleDialogValue(70, state->dpi), editH, 0);
        state->thumbnailEdgeEdit = make(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX + DashboardScaleDialogValue(382, state->dpi), y, DashboardScaleDialogValue(72, state->dpi), editH, ID_OUTPUT_OPTIONS_THUMB_EDGE);
        y += editH + DashboardScaleDialogValue(14, state->dpi);
        state->embeddedAssetLabel = make(L"STATIC", L"OCR assets", 0,
            margin, y + 5, labelW, editH, 0);
        state->embeddedAssetFormatCombo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            editX, y, DashboardScaleDialogValue(250, state->dpi), editH * 5, ID_OUTPUT_OPTIONS_ASSET_FORMAT);
        state->embeddedAssetQualityLabel = make(L"STATIC", L"Quality", 0,
            editX + DashboardScaleDialogValue(266, state->dpi), y + 5, DashboardScaleDialogValue(64, state->dpi), editH, 0);
        state->embeddedAssetQualityEdit = make(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            editX + DashboardScaleDialogValue(338, state->dpi), y, DashboardScaleDialogValue(72, state->dpi), editH, ID_OUTPUT_OPTIONS_ASSET_QUALITY);
        y += editH + DashboardScaleDialogValue(18, state->dpi);
        state->hintText = make(
            L"STATIC",
            L"Auto: seals use PNG; images/charts use JPEG.\r\nWebP, JPEG, and PNG convert generated OCR crops.",
            SS_LEFT,
            margin,
            y,
            DashboardScaleDialogValue(590, state->dpi),
            DashboardScaleDialogValue(64, state->dpi),
            0);
        y += DashboardScaleDialogValue(64, state->dpi);
        state->resetBtn = make(L"BUTTON", L"Restore compact", WS_TABSTOP, margin, y, DashboardScaleDialogValue(162, state->dpi), editH, ID_OUTPUT_OPTIONS_RESET);
        state->saveBtn = make(L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP,
            DashboardScaleDialogValue(436, state->dpi), y, DashboardScaleDialogValue(86, state->dpi), editH, ID_OUTPUT_OPTIONS_SAVE);
        state->cancelBtn = make(L"BUTTON", L"Cancel", WS_TABSTOP,
            DashboardScaleDialogValue(534, state->dpi), y, DashboardScaleDialogValue(86, state->dpi), editH, ID_OUTPUT_OPTIONS_CANCEL);
        SetOutputArtifactOptionsDialogControls(state, *state->options);
        UpdateOutputArtifactOptionsDialogEnablement(state);
        LayoutOutputArtifactOptionsDialog(state);
        return 0;
    }
    case WM_SIZE:
        if (state) LayoutOutputArtifactOptionsDialog(state);
        return 0;
    case WM_VSCROLL:
        if (state && lParam == 0) {
            SCROLLINFO scroll = { sizeof(scroll) };
            scroll.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &scroll);
            const int maxScroll = max(0, state->contentHeight -
                max(1, static_cast<int>(scroll.nPage)));
            int next = state->scrollY;
            switch (LOWORD(wParam)) {
            case SB_LINEUP:
                next -= DashboardScaleDialogValue(28, state->dpi);
                break;
            case SB_LINEDOWN:
                next += DashboardScaleDialogValue(28, state->dpi);
                break;
            case SB_PAGEUP:
                next -= max(1, static_cast<int>(scroll.nPage) - DashboardScaleDialogValue(28, state->dpi));
                break;
            case SB_PAGEDOWN:
                next += max(1, static_cast<int>(scroll.nPage) - DashboardScaleDialogValue(28, state->dpi));
                break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK:
                next = scroll.nTrackPos;
                break;
            case SB_TOP:
                next = 0;
                break;
            case SB_BOTTOM:
                next = maxScroll;
                break;
            default:
                return 0;
            }
            state->scrollY = min(max(0, next), maxScroll);
            LayoutOutputArtifactOptionsDialog(state);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state) {
            const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (wheelDelta != 0) {
                state->scrollY -= MulDiv(
                    wheelDelta,
                    DashboardScaleDialogValue(56, state->dpi),
                    WHEEL_DELTA);
                LayoutOutputArtifactOptionsDialog(state);
            }
            return 0;
        }
        break;
    case WM_DPICHANGED:
        if (state) {
            const UINT nextDpi = HIWORD(wParam);
            if (nextDpi > 0 && nextDpi != state->dpi) {
                HFONT nextFont = DashboardCreateDialogFont(20, nextDpi);
                if (nextFont) {
                    HFONT previousFont = state->font;
                    const bool deletePrevious = state->ownsFont;
                    state->font = nextFont;
                    state->ownsFont = true;
                    ApplyOutputArtifactOptionsDialogFont(state);
                    if (deletePrevious && previousFont) DeleteObject(previousFont);
                }
                state->dpi = nextDpi;
            }
            if (const auto* suggested = reinterpret_cast<const RECT*>(lParam)) {
                SIZE nextSize = {
                    max(1L, suggested->right - suggested->left),
                    max(1L, suggested->bottom - suggested->top)
                };
                nextSize = DashboardClampDialogSizeToWorkArea(nextSize, hwnd, state->dpi);
                SetWindowPos(hwnd, nullptr,
                    suggested->left,
                    suggested->top,
                    nextSize.cx,
                    nextSize.cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            LayoutOutputArtifactOptionsDialog(state);
        }
        return 0;
    case WM_COMMAND: {
        if (!state) break;
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == ID_OUTPUT_OPTIONS_PROFILE && notification == CBN_SELCHANGE) {
            const int selection = static_cast<int>(SendMessageW(state->profileCombo, CB_GETCURSEL, 0, 0));
            if (selection >= 0 && selection < static_cast<int>(DashboardOutputArtifactProfile::Custom)) {
                OcrOutputArtifactOptions options = CaptureOutputArtifactOptionsDialogControls(state);
                DashboardApplyOutputArtifactProfile(options, static_cast<DashboardOutputArtifactProfile>(selection));
                SetOutputArtifactOptionsDialogControls(state, options);
            }
            UpdateOutputArtifactOptionsDialogEnablement(state);
            return 0;
        }
        if (id == ID_OUTPUT_OPTIONS_LAYOUT_ENABLED ||
            id == ID_OUTPUT_OPTIONS_LAYOUT_FORMAT ||
            id == ID_OUTPUT_OPTIONS_THUMB_POLICY ||
            id == ID_OUTPUT_OPTIONS_THUMB_FORMAT ||
            id == ID_OUTPUT_OPTIONS_LAYOUT_QUALITY ||
            id == ID_OUTPUT_OPTIONS_THUMB_QUALITY ||
            id == ID_OUTPUT_OPTIONS_THUMB_EDGE ||
            id == ID_OUTPUT_OPTIONS_ASSET_FORMAT ||
            id == ID_OUTPUT_OPTIONS_ASSET_QUALITY) {
            if (notification == BN_CLICKED || notification == CBN_SELCHANGE || notification == EN_CHANGE) {
                SyncOutputArtifactOptionsDialogProfile(state);
                return 0;
            }
        }
        if (id == ID_OUTPUT_OPTIONS_BROWSE && state->showOutputRoot && state->outputRoot) {
            std::wstring selected;
            if (DashboardSelectOcrOptionsOutputRoot(hwnd, DashboardGetWindowTextWide(state->rootEdit), L"Choose OCR output folder", selected)) {
                SetWindowTextW(state->rootEdit, selected.c_str());
            }
            return 0;
        }
        if (id == ID_OUTPUT_OPTIONS_RESET) {
            OcrOutputArtifactOptions options;
            DashboardApplyOutputArtifactProfile(options, DashboardOutputArtifactProfile::Compact);
            SetOutputArtifactOptionsDialogControls(state, options);
            UpdateOutputArtifactOptionsDialogEnablement(state);
            return 0;
        }
        if (id == ID_OUTPUT_OPTIONS_SAVE) {
            *state->options = CaptureOutputArtifactOptionsDialogControls(state);
            if (state->showOutputRoot && state->outputRoot) {
                *state->outputRoot = DashboardTrimWide(DashboardGetWindowTextWide(state->rootEdit));
            }
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == ID_OUTPUT_OPTIONS_CANCEL) {
            state->accepted = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        if (state) {
            state->accepted = false;
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (state) state->hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool RegisterOutputArtifactOptionsDialogClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = OutputArtifactOptionsDialogWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wcex.lpszClassName = kOutputArtifactOptionsDialogClass;
    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    registered = true;
    return true;
}

bool DashboardShowOutputArtifactOptionsDialog(
    HWND owner,
    OcrOutputArtifactOptions& options,
    std::wstring* outputRoot,
    UINT dpi,
    HFONT fallbackFont)
{
    if (!RegisterOutputArtifactOptionsDialogClass()) return false;
    OutputArtifactOptionsDialogState state;
    state.options = &options;
    state.outputRoot = outputRoot;
    state.showOutputRoot = outputRoot != nullptr;
    state.dpi = dpi > 0 ? dpi : kDashboardDialogDesignDpi;
    state.font = DashboardCreateDialogFont(20, state.dpi);
    state.ownsFont = state.font != nullptr;
    if (!state.font) {
        state.font = fallbackFont;
        state.ownsFont = false;
    }
    const int width = DashboardScaleDialogValue(650, state.dpi);
    const int height = DashboardScaleDialogValue(state.showOutputRoot ? 460 : 410, state.dpi);
    RECT rc = {0, 0, width, height};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_DLGMODALFRAME, state.dpi);
    SIZE dialogSize = { rc.right - rc.left, rc.bottom - rc.top };
    dialogSize = DashboardClampDialogSizeToWorkArea(dialogSize, owner, state.dpi);
    BOOL ownerWasEnabled = owner ? IsWindowEnabled(owner) : FALSE;
    if (owner) EnableWindow(owner, FALSE);
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kOutputArtifactOptionsDialogClass,
        L"OCR Output Options",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        dialogSize.cx, dialogSize.cy,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) {
        if (owner && ownerWasEnabled) EnableWindow(owner, TRUE);
        if (state.ownsFont) DeleteObject(state.font);
        return false;
    }
    DashboardCenterWindowOnOwner(dialog, owner);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG msg = {};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (state.hwnd && IsWindow(state.hwnd)) DestroyWindow(state.hwnd);
    if (owner && ownerWasEnabled) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (state.ownsFont) DeleteObject(state.font);
    return state.accepted;
}

