#include "ocr/ui/dashboard/DashboardPdfOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "ocr/ui/dashboard/DashboardDialogLayout.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"
#include "PageRange.h"
#include "PdfPageRenderer.h"
#include "PdfRenderOptions.h"

#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>

// D-B-CLOSE-2: PDF options preview prepare/delete (from god dialog TU).

static std::wstring JoinPreviewPath(const std::wstring& left, const std::wstring& right) {
    return DashboardJoinPathWide(left, right);
}

void DashboardDeletePdfPreviewTemp(const std::wstring& previewImagePath, const std::wstring& previewTempDir) {
    if (!previewImagePath.empty()) {
        DeleteFileW(previewImagePath.c_str());
    }
    if (!previewTempDir.empty()) {
        RemoveDirectoryW(previewTempDir.c_str());
    }
}

bool DashboardPreparePdfOptionsPreview(
    const std::vector<PdfImportPreflightInfo>& preflight,
    const std::wstring& pageRange,
    size_t pdfIndex,
    std::wstring& previewTempDir,
    std::wstring& previewImagePath,
    std::wstring& previewCaption)
{
    previewTempDir.clear();
    previewImagePath.clear();
    previewCaption.clear();

    if (preflight.empty() || preflight.front().path.empty() || preflight.front().pageCount <= 0) {
        previewCaption = L"Preview unavailable";
        return false;
    }

    if (pdfIndex >= preflight.size()) pdfIndex = preflight.size() - 1;
    const PdfImportPreflightInfo& firstPdf = preflight[pdfIndex];
    if (firstPdf.path.empty() || firstPdf.pageCount <= 0) {
        previewCaption = L"Preview unavailable";
        return false;
    }

    int previewPage = 1;
    std::wstring effectiveRange = DashboardTrimWide(pageRange);
    if (effectiveRange.empty()) effectiveRange = L"all";
    if (!DashboardIsAllPageRangeText(effectiveRange)) {
        std::vector<int> pages;
        std::wstring error;
        if (!PageRange::Parse(effectiveRange, firstPdf.pageCount, pages, error) || pages.empty()) {
            previewCaption = L"Preview unavailable";
            return false;
        }
        previewPage = pages.front();
    }

    wchar_t tempRoot[MAX_PATH] = {};
    DWORD tempLen = GetTempPathW(MAX_PATH, tempRoot);
    if (tempLen == 0 || tempLen >= MAX_PATH) {
        previewCaption = L"Preview unavailable";
        return false;
    }

    // OWN-114: pure pdf preview dir name (WideStringUtils).
    const std::wstring dirName = WideFormatPdfPreviewDirName(
        GetCurrentProcessId(), GetTickCount64());
    previewTempDir = JoinPreviewPath(tempRoot, dirName);
    if (!CreateDirectoryW(previewTempDir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        previewCaption = L"Preview unavailable";
        previewTempDir.clear();
        return false;
    }

    PdfRenderSettings settings;
    settings.dpi = 96;
    settings.maxPixelEdge = 900;
    settings.maxMegapixels = 2;
    // OWN-123: pure int labels (WideStringUtils).
    settings.pageRange = WideFormatIntLabel(previewPage);
    settings.password = firstPdf.password;
    settings.savePageImages = true;

    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(firstPdf.path, previewTempDir, settings);
    if (!render.pages.empty() && !render.pages.front().imagePath.empty() &&
        PathFileExistsW(render.pages.front().imagePath.c_str())) {
        previewImagePath = render.pages.front().imagePath;
        // OWN-123: pure slash count + page label (WideStringUtils).
        previewCaption = L"Preview " + WideFormatSlashCount(pdfIndex + 1, (int)preflight.size()) + L": " +
            DashboardFileNameFromPath(firstPdf.path) + WideFormatPageSlashLabel(previewPage);
        return true;
    }

    previewCaption = L"Preview unavailable";
    DashboardDeletePdfPreviewTemp(previewImagePath, previewTempDir);
    previewImagePath.clear();
    previewTempDir.clear();
    return false;
}
