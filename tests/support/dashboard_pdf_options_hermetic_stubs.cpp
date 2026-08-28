// Hermetic stubs for heavy PDF/password/i18n deps used only by Collect path
// or format summaries. D-B-CLOSE-3: keep test free of WinRT PdfPageRenderer.

#include "ocr/batch/PdfPageRenderer.h"
#include "ocr/batch/BatchOcrTypes.h"
#include "ocr/ui/dashboard/DashboardPdfPasswordDialog.h"
#include "Strings.h"

#include <string>

namespace S {
bool IsChinese() { return false; }
}

bool DashboardPromptForPdfPassword(
    HWND,
    UINT,
    HFONT,
    const std::wstring&,
    int,
    int,
    const std::wstring&,
    std::wstring& password)
{
    password.clear();
    return false;
}

PdfPreflightResult PdfPageRenderer::Inspect(
    const std::wstring&,
    const std::wstring&)
{
    PdfPreflightResult r;
    r.success = false;
    r.error = L"stub";
    return r;
}

PdfRenderResult PdfPageRenderer::RenderToPageImages(
    const std::wstring&,
    const std::wstring&,
    const PdfRenderSettings&)
{
    PdfRenderResult r;
    r.success = false;
    r.error = L"stub";
    return r;
}

PdfCoverRenderResult PdfPageRenderer::RenderFirstPageCover(
    const std::wstring&,
    const std::wstring&,
    const std::wstring&,
    uint32_t,
    uint32_t,
    PdfRenderImageFormat,
    int)
{
    PdfCoverRenderResult r;
    r.success = false;
    r.error = L"stub";
    return r;
}

std::wstring DashboardFormatOutputArtifactSummary(const OcrOutputArtifactOptions&)
{
    return L"artifacts";
}
