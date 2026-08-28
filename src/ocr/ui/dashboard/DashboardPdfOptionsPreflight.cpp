#include "ocr/ui/dashboard/DashboardPdfOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardPdfOptionsDialogInternals.h"
#include "ocr/ui/dashboard/DashboardPdfPasswordDialog.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "ocr/ui/dashboard/DashboardDialogLayout.h"
#include "core/WideStringUtils.h"
#include "PageRange.h"
#include "PdfPageRenderer.h"
#include "PdfRenderOptions.h"
#include "BatchOcrWriter.h"
#include "Strings.h"

#include <windows.h>
#include <algorithm>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// D-B-CLOSE-2: preflight / validate / estimate / format helpers (from god dialog TU).

static bool DashboardDirectoryExistsWide(const std::wstring& path)
{
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::vector<std::wstring> PreviewPdfOutputFolderNames(
    const std::wstring& outputRoot,
    const std::vector<PdfImportPreflightInfo>* preflight)
{
    std::vector<std::wstring> names;
    if (!preflight) return names;

    std::set<std::wstring> reservedLower;
    names.reserve(preflight->size());
    for (size_t i = 0; i < preflight->size(); i++) {
        std::wstring base = DashboardPdfPreviewStem((*preflight)[i].path);
        std::wstring selected = base;
        for (int suffix = 1; suffix < 1000; suffix++) {
            std::wstring candidate = DashboardAppendPreviewDuplicateSuffix(base, suffix);
            std::wstring full = outputRoot.empty()
                ? candidate
                : DashboardJoinPathWide(outputRoot, candidate);
            std::wstring lower = full;
            for (auto& ch : lower) {
                if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
            }
            if (!DashboardDirectoryExistsWide(full) && reservedLower.insert(lower).second) {
                selected = candidate;
                break;
            }
        }
        names.push_back(selected);
    }
    return names;
}

std::wstring DashboardFormatPdfCloudConfirmPrompt(
    int selectedPageCount,
    int totalPageCount,
    int pdfCount,
    const DashboardPdfCloudRiskPolicy& riskPolicy)
{
    int uploadPages = selectedPageCount > 0 ? selectedPageCount : totalPageCount;
    const bool zh = S::IsChinese();
    std::wstring prompt = zh
        ? L"当前 OCR 引擎是 PaddleOCR Cloud。\n\n符合条件的文件将直接上传 PDF 原文件，"
          L"不会先拆成逐页图片。即使只选择部分页面，Cloud 仍会收到完整 PDF 容器。"
          L"加密、超限或服务不支持的文件会明确回退为逐页图片。"
        : L"Current OCR engine is PaddleOCR Cloud.\n\nEligible files will upload the original PDF "
          L"directly instead of first splitting it into page images. Even for a partial page range, "
          L"the complete PDF container is uploaded. Encrypted, oversized, or unsupported files will "
          L"explicitly fall back to rendered page images.";
    prompt += zh ? L"\n\n本次选择页数：" : L"\n\nSelected pages: ";
    // OWN-123: pure int labels (WideStringUtils).
    prompt += WideFormatIntLabel(uploadPages);
    if (pdfCount > 0) {
        prompt += zh ? L"，PDF 文件数：" : L"; PDF files: ";
        prompt += WideFormatIntLabel(pdfCount);
    }
    if (totalPageCount > 0) {
        prompt += zh ? L"，总页数：" : L"; total pages: ";
        prompt += WideFormatIntLabel(totalPageCount);
    }
    prompt += L".";

    DashboardPdfCloudRiskLevel risk = DashboardClassifyPdfCloudRisk(uploadPages, riskPolicy);
    if (risk == DashboardPdfCloudRiskLevel::VeryLarge) {
        prompt += zh
            ? L"\n\n这是很大的 Cloud OCR 批次，请确认费用、限流和文档隐私。"
            : L"\n\nVery large Cloud OCR batch. Review cost, rate limits, and document privacy before continuing.";
    } else if (risk == DashboardPdfCloudRiskLevel::Large) {
        prompt += zh
            ? L"\n\n这是较大的 Cloud OCR 批次，请确认费用和文档隐私。"
            : L"\n\nLarge Cloud OCR batch. Review cost and document privacy before continuing.";
    }

    prompt += zh ? L"\n继续吗？" : L"\nContinue?";
    return prompt;
}

bool DashboardCollectPdfImportPreflight(
    HWND owner,
    const std::vector<std::wstring>& pdfs,
    UINT dpi,
    HFONT font,
    std::vector<PdfImportPreflightInfo>& preflight,
    int& totalPageCount)
{
    preflight.clear();
    totalPageCount = 0;
    preflight.reserve(pdfs.size());

    for (const auto& pdf : pdfs) {
        PdfImportPreflightInfo info;
        info.path = pdf;

        std::wstring password;
        PdfPreflightResult result = PdfPageRenderer::Inspect(pdf);
        bool inspected = result.success;
        constexpr int kMaxPdfPasswordAttempts = 3;
        if (!inspected && result.requiresPassword) {
            std::wstring previousPasswordError;
            for (int attempt = 1; attempt <= kMaxPdfPasswordAttempts; attempt++) {
                std::wstring enteredPassword;
                if (!DashboardPromptForPdfPassword(
                        owner,
                        dpi,
                        font,
                        DashboardFileNameFromPath(pdf),
                        attempt,
                        kMaxPdfPasswordAttempts,
                        attempt > 1 ? previousPasswordError : L"",
                        enteredPassword)) {
                    return false;
                }

                result = PdfPageRenderer::Inspect(pdf, enteredPassword);
                if (result.success) {
                    inspected = true;
                    password = enteredPassword;
                    break;
                }
                if (!result.requiresPassword) {
                    break;
                }
                previousPasswordError = result.error.empty()
                    ? L"Password was not accepted."
                    : result.error;
            }
        }

        info.pageCount = result.pageCount;
        info.requiresPassword = result.requiresPassword;
        info.password = inspected ? password : L"";
        info.error = result.error;
        info.pages = result.pages;

        if (!inspected) {
            std::wstring msg = L"Cannot import PDF: " + DashboardFileNameFromPath(pdf);
            msg += L"\n";
            if (result.requiresPassword) {
                msg += L"Password was not accepted after 3 attempts.";
                if (!result.error.empty()) {
                    msg += L"\n";
                    msg += result.error;
                }
            } else if (!result.error.empty()) {
                msg += result.error;
            } else {
                msg += L"PDF preflight failed.";
            }
            MessageBoxW(owner, msg.c_str(), L"ZenCrop", MB_OK | MB_ICONWARNING);
            return false;
        }

        totalPageCount += max(0, result.pageCount);
        preflight.push_back(std::move(info));
    }

    if (!pdfs.empty() && totalPageCount <= 0) {
        MessageBoxW(owner, L"PDF preflight found no pages.", L"ZenCrop", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

std::wstring DashboardPdfFormatSelectedPageText(int selectedPageCount, int totalPageCount) {
    if (selectedPageCount <= 0) return L"Selected pages: invalid range";

    // OWN-123: pure int labels (WideStringUtils).
    std::wstring text = L"Selected pages: ";
    text += WideFormatIntLabel(selectedPageCount);
    if (totalPageCount > 0) {
        text += L" / ";
        text += WideFormatIntLabel(totalPageCount);
    }
    return text;
}

std::wstring DashboardPdfFormatOutputText(
    const std::wstring& outputRoot,
    const std::vector<PdfImportPreflightInfo>* preflight)
{
    if (outputRoot.empty()) return L"Output: not selected";
    if (!preflight || preflight->empty()) return L"Output: " + outputRoot;

    std::vector<std::wstring> folderNames = PreviewPdfOutputFolderNames(outputRoot, preflight);
    if (folderNames.empty()) return L"Output: " + outputRoot;

    if (folderNames.size() == 1) {
        // OWN-119: pure path join (WideStringUtils).
        return L"Output: " + WideJoinPath(outputRoot, folderNames.front());
    }

    std::wstring text = L"Output: ";
    text += outputRoot;
    text += L"\\";
    text += folderNames[0];
    text += L", ";
    text += folderNames[1];
    if (folderNames.size() > 2) {
        text += L", ...";
    }
    text += L" (";
    // OWN-123: pure int labels (WideStringUtils).
    text += WideFormatIntLabel((int)folderNames.size());
    text += L" folders)";
    return text;
}

std::wstring DashboardPdfFormatOutputTreeText(
    const std::wstring& outputRoot,
    const std::vector<PdfImportPreflightInfo>* preflight,
    PdfRenderImageFormat imageFormat,
    const OcrOutputArtifactOptions& artifactSource)
{
    if (outputRoot.empty()) {
        return L"Output tree:\r\n  No output folder selected.";
    }

    std::vector<std::wstring> folderNames = PreviewPdfOutputFolderNames(outputRoot, preflight);
    if (folderNames.empty()) {
        return L"Output tree:\r\n  " + outputRoot;
    }

    const OcrOutputArtifactOptions artifacts = NormalizeOcrOutputArtifactOptions(artifactSource);
    const PdfRenderImageFormat thumbnailFormat =
        NormalizeArtifactImageFormat(artifacts.pdfThumbnailFormat);
    const std::wstring thumbnailName =
        std::wstring(L"thumbnail") + PdfRenderImageFormatExtension(thumbnailFormat);
    const std::wstring layoutName =
        std::wstring(L"page_0001.layout") +
        PdfRenderImageFormatExtension(NormalizeArtifactImageFormat(artifacts.layoutPreviewFormat));

    std::wstringstream ss;
    auto appendDerivedArtifacts = [&]() {
        if (artifacts.pdfThumbnailPolicy == PdfThumbnailPolicy::Never) {
            ss << L"  thumbnail: disabled\r\n";
        } else {
            ss << L"  " << thumbnailName;
            if (artifacts.pdfThumbnailPolicy == PdfThumbnailPolicy::Auto) {
                ss << L" (Auto; reuses Page 1 when possible)";
            } else {
                ss << L" (Always)";
            }
            ss << L"\r\n";
        }
        if (artifacts.writeLayoutPreview) {
            ss << L"  pages\\" << layoutName << L"\r\n";
        } else {
            ss << L"  layout preview: disabled\r\n";
        }
        ss << L"  OCR embedded assets: ";
        if (artifacts.embeddedAssetFormat == PdfRenderImageFormat::Auto) {
            ss << L"Auto (preserve OCR semantic format)";
        } else {
            ss << PdfRenderImageFormatToString(artifacts.embeddedAssetFormat);
            if (artifacts.embeddedAssetFormat == PdfRenderImageFormat::Jpeg ||
                artifacts.embeddedAssetFormat == PdfRenderImageFormat::WebP) {
                ss << L" quality " << artifacts.embeddedAssetQuality;
            }
        }
        ss << L"\r\n";
    };

    ss << L"Output tree:\r\n";
    if (folderNames.size() == 1) {
        const std::wstring& folder = folderNames.front();
        std::wstring pageImageName = imageFormat == PdfRenderImageFormat::Auto
            ? L"page_0001.<auto>"
            : (std::wstring(L"page_0001") + PdfRenderImageFormatExtension(imageFormat));
        ss << outputRoot << L"\\" << folder << L"\\\r\n";
        ss << L"  " << folder << L".md / " << folder << L".txt / " << folder << L".content.json\r\n";
        ss << L"  manifest.json\r\n";
        ss << L"  pages\\page_0001.md / .txt / .json\r\n";
        ss << L"  page_images\\" << pageImageName << L"\r\n";
        appendDerivedArtifacts();
        ss << L"  assets\\";
        return ss.str();
    }

    ss << outputRoot << L"\\\r\n";
    size_t shown = (std::min<size_t>)(folderNames.size(), 3);
    for (size_t i = 0; i < shown; i++) {
        ss << L"  " << folderNames[i] << L"\\\r\n";
    }
    if (folderNames.size() > shown) {
        ss << L"  ... (" << folderNames.size() << L" PDF folders)\r\n";
    }
    ss << L"Each PDF folder contains:\r\n";
    ss << L"  <pdf>.md / <pdf>.txt / <pdf>.content.json, manifest.json\r\n";
    ss << L"  pages\\page_0001.*, page_images\\page_0001";
    ss << (imageFormat == PdfRenderImageFormat::Auto ? L".<auto>" : PdfRenderImageFormatExtension(imageFormat));
    ss << L"\r\n";
    appendDerivedArtifacts();
    ss << L"  assets\\";
    return ss.str();
}

bool DashboardPdfCountSelectedPages(
    const std::wstring& pageRange,
    const std::vector<PdfImportPreflightInfo>* preflight,
    int& selectedPageCount,
    std::wstring& error)
{
    selectedPageCount = 0;
    error.clear();

    std::wstring effectiveRange = DashboardTrimWide(pageRange);
    if (effectiveRange.empty()) effectiveRange = L"all";

    if (preflight && !preflight->empty()) {
        long long selectedTotal = 0;
        if (DashboardIsAllPageRangeText(effectiveRange)) {
            for (const auto& info : *preflight) {
                selectedTotal += max(0, info.pageCount);
            }
        } else {
            for (const auto& info : *preflight) {
                std::vector<int> pages;
                std::wstring parseError;
                if (!PageRange::Parse(effectiveRange, info.pageCount, pages, parseError)) {
                    error = L"Invalid page range for " + DashboardFileNameFromPath(info.path) + L".";
                    if (info.pageCount > 0) {
                        error += L"\nPDF pages: ";
                        // OWN-123: pure int labels (WideStringUtils).
                        error += WideFormatIntLabel(info.pageCount);
                    }
                    if (!parseError.empty()) {
                        error += L"\n";
                        error += parseError;
                    }
                    return false;
                }
                selectedTotal += static_cast<long long>(pages.size());
            }
        }

        if (selectedTotal <= 0) {
            error = L"Page range selected no pages.";
            return false;
        }

        selectedPageCount = static_cast<int>((std::min<long long>)(
            selectedTotal,
            static_cast<long long>((std::numeric_limits<int>::max)())));
        return true;
    }

    if (DashboardIsAllPageRangeText(effectiveRange)) {
        selectedPageCount = 0;
        return true;
    }

    std::vector<int> pages;
    std::wstring parseError;
    if (!PageRange::Parse(effectiveRange, 1000000, pages, parseError)) {
        error = L"Invalid page range.";
        if (!parseError.empty()) {
            error += L"\n";
            error += parseError;
        }
        return false;
    }
    selectedPageCount = (int)pages.size();
    return true;
}

struct PdfImportEstimate {
    int selectedPages = 0;
    double totalMegapixels = 0.0;
    double maxPageMegapixels = 0.0;
    uint64_t rawBytes = 0;
    bool scaledDown = false;
};

static bool SelectPdfPagesForInfo(
    const PdfImportPreflightInfo& info,
    const std::wstring& pageRange,
    std::vector<int>& selectedPages,
    std::wstring& error)
{
    selectedPages.clear();
    error.clear();
    if (info.pageCount <= 0) return false;

    if (DashboardIsAllPageRangeText(pageRange)) {
        selectedPages.reserve(static_cast<size_t>(info.pageCount));
        for (int i = 1; i <= info.pageCount; i++) selectedPages.push_back(i);
        return true;
    }
    return PageRange::Parse(pageRange, info.pageCount, selectedPages, error);
}

static const PdfPreflightPageInfo* FindPreflightPageInfo(const PdfImportPreflightInfo& info, int pageIndex) {
    if (pageIndex > 0 && pageIndex <= (int)info.pages.size()) {
        const PdfPreflightPageInfo& direct = info.pages[(size_t)pageIndex - 1];
        if (direct.pageIndex == pageIndex) return &direct;
    }
    for (const auto& page : info.pages) {
        if (page.pageIndex == pageIndex) return &page;
    }
    return nullptr;
}

static double EstimateRenderedPageMegapixels(
    const PdfPreflightPageInfo& page,
    int dpi,
    const PdfRenderSettings& settings,
    bool& scaledDown)
{
    scaledDown = false;
    int effectiveDpi = dpi > 0 ? dpi : settings.dpi;
    double rawWidth = std::ceil(page.widthDip * effectiveDpi / 96.0);
    double rawHeight = std::ceil(page.heightDip * effectiveDpi / 96.0);
    if (!std::isfinite(rawWidth) || !std::isfinite(rawHeight) || rawWidth <= 0.0 || rawHeight <= 0.0) {
        return 0.0;
    }

    double scale = 1.0;
    if (settings.maxPixelEdge > 0) {
        scale = (std::min)(scale, static_cast<double>(settings.maxPixelEdge) / rawWidth);
        scale = (std::min)(scale, static_cast<double>(settings.maxPixelEdge) / rawHeight);
    }
    if (settings.maxMegapixels > 0) {
        double rawPixels = rawWidth * rawHeight;
        double maxPixels = static_cast<double>(settings.maxMegapixels) * 1000000.0;
        if (rawPixels > maxPixels && rawPixels > 0.0) {
            scale = (std::min)(scale, std::sqrt(maxPixels / rawPixels));
        }
    }
    scale = (std::max)(scale, 0.0001);
    scaledDown = scale < 0.999;
    double width = (std::max)(1.0, std::floor(rawWidth * scale));
    double height = (std::max)(1.0, std::floor(rawHeight * scale));
    return (width * height) / 1000000.0;
}

static bool EstimatePdfImport(
    const std::wstring& pageRange,
    int dpi,
    uint32_t maxPixelEdge,
    uint32_t maxMegapixels,
    const std::vector<PdfImportPreflightInfo>* preflight,
    PdfImportEstimate& estimate,
    std::wstring& error)
{
    estimate = PdfImportEstimate{};
    error.clear();
    if (!preflight || preflight->empty()) return false;

    PdfRenderSettings renderSettings;
    renderSettings.dpi = dpi > 0 ? dpi : renderSettings.dpi;
    renderSettings.maxPixelEdge = maxPixelEdge;
    renderSettings.maxMegapixels = maxMegapixels;

    std::wstring effectiveRange = DashboardTrimWide(pageRange);
    if (effectiveRange.empty()) effectiveRange = L"all";

    for (const auto& info : *preflight) {
        std::vector<int> selectedPages;
        if (!SelectPdfPagesForInfo(info, effectiveRange, selectedPages, error)) {
            return false;
        }
        for (int pageIndex : selectedPages) {
            const PdfPreflightPageInfo* page = FindPreflightPageInfo(info, pageIndex);
            if (!page) continue;
            bool pageScaledDown = false;
            double mp = EstimateRenderedPageMegapixels(*page, renderSettings.dpi, renderSettings, pageScaledDown);
            estimate.selectedPages++;
            estimate.totalMegapixels += mp;
            estimate.maxPageMegapixels = (std::max)(estimate.maxPageMegapixels, mp);
            estimate.scaledDown = estimate.scaledDown || pageScaledDown;
        }
    }

    double rawBytes = estimate.totalMegapixels * 1000000.0 * 4.0;
    if (rawBytes > static_cast<double>((std::numeric_limits<uint64_t>::max)())) {
        estimate.rawBytes = (std::numeric_limits<uint64_t>::max)();
    } else {
        estimate.rawBytes = static_cast<uint64_t>(rawBytes);
    }
    return estimate.selectedPages > 0;
}

static std::wstring FormatDurationRange(int minSeconds, int maxSeconds) {
    // OWN-123: pure int labels (WideStringUtils).
    auto formatOne = [](int seconds) {
        if (seconds < 60) return WideFormatIntLabel(seconds) + L"s";
        int minutes = seconds / 60;
        int rest = seconds % 60;
        if (rest == 0) return WideFormatIntLabel(minutes) + L"m";
        return WideFormatIntLabel(minutes) + L"m " + WideFormatIntLabel(rest) + L"s";
    };
    return formatOne(minSeconds) + L"-" + formatOne(maxSeconds);
}

// OWN-73: thin wrapper over pure DashboardFormatMegabytes.
static std::wstring FormatMegabytes(uint64_t bytes) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return DashboardFormatMegabytes(mb, mb < 10.0);
}

std::wstring DashboardPdfFormatEstimateText(
    const std::wstring& pageRange,
    int dpi,
    uint32_t maxPixelEdge,
    uint32_t maxMegapixels,
    PdfRenderImageFormat imageFormat,
    const std::vector<PdfImportPreflightInfo>* preflight)
{
    PdfImportEstimate estimate;
    std::wstring error;
    if (!EstimatePdfImport(pageRange, dpi, maxPixelEdge, maxMegapixels, preflight, estimate, error)) {
        return L"Invalid page range";
    }

    // OWN-114: pure megapixel estimate label (WideStringUtils).
    const std::wstring mpText = WideFormatMpEstimate(
        estimate.totalMegapixels,
        estimate.maxPageMegapixels);

    int minSeconds = estimate.selectedPages * 4;
    int maxSeconds = estimate.selectedPages * 15;

    // Keep this a predictable three-line status block. The output section is
    // intentionally below Estimate, so allowing an incidental long wrap here
    // would otherwise consume the output-tree/button area on normal DPI.
    // OWN-123: pure int labels (WideStringUtils).
    std::wstring text = WideFormatIntLabel(estimate.selectedPages);
    text += L" OCR request(s) · ";
    text += mpText;
    text += L"\r\nMemory: ~";
    text += FormatMegabytes(estimate.rawBytes);
    text += L" · Page: ";
    text += PdfRenderImageFormatToString(imageFormat);
    text += L"\r\nOCR: ";
    text += FormatDurationRange(minSeconds, maxSeconds);
    if (estimate.scaledDown) {
        text += L" · some pages downsampled";
    }
    return text;
}

bool DashboardValidatePdfOptions(
    HWND owner,
    const std::wstring& pageRange,
    int dpi,
    const std::vector<PdfImportPreflightInfo>* preflight,
    int* selectedPageCount)
{
    if (selectedPageCount) *selectedPageCount = 0;

    auto warn = [owner](const wchar_t* text) {
        // D-B-CLOSE-3: hermetic/null-owner callers get silent fail; product always passes HWND.
        if (owner) MessageBoxW(owner, text, L"ZenCrop", MB_OK | MB_ICONWARNING);
    };

    if (dpi < 72 || dpi > 600) {
        warn(L"DPI must be between 72 and 600.");
        return false;
    }

    std::wstring effectiveRange = DashboardTrimWide(pageRange);
    if (effectiveRange.empty()) effectiveRange = L"all";

    std::wstring error;
    int countedPages = 0;
    if (!DashboardPdfCountSelectedPages(effectiveRange, preflight, countedPages, error)) {
        warn(error.empty() ? L"Invalid page range." : error.c_str());
        return false;
    }
    if (selectedPageCount) *selectedPageCount = countedPages;

    return true;
}
