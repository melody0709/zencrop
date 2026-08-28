#include "ocr/ui/dashboard/DashboardPdfOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardPdfOptionsDialogInternals.h"
#include "ocr/ui/dashboard/DashboardState.h"

#include <iostream>
#include <windows.h>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

// D-B-CLOSE-3: hermetic contracts for PDF options domain APIs (preflight TU).
// Does not host full modal HWND loop; covers pure/public validate/count/format paths.

int main() {
    // Count selected pages: all range without preflight → success, count 0 (unknown total).
    {
        int selected = -1;
        std::wstring err;
        Expect(DashboardPdfCountSelectedPages(L"all", nullptr, selected, err), "count all no-preflight");
        Expect(selected == 0, "count all zero without preflight");
        Expect(err.empty(), "count all no err");
    }

    // Explicit range without preflight.
    {
        int selected = 0;
        std::wstring err;
        Expect(DashboardPdfCountSelectedPages(L"1-3", nullptr, selected, err), "count 1-3");
        Expect(selected == 3, "count 1-3 size");
    }

    // Invalid range.
    {
        int selected = 0;
        std::wstring err;
        Expect(!DashboardPdfCountSelectedPages(L"nope", nullptr, selected, err), "count invalid");
        Expect(!err.empty(), "count invalid err");
    }

    // Validate DPI bounds (success path uses null owner — no MessageBox).
    {
        Expect(DashboardValidatePdfOptions(nullptr, L"all", 150, nullptr, nullptr), "validate ok dpi");
        Expect(!DashboardValidatePdfOptions(nullptr, L"all", 10, nullptr, nullptr), "validate bad dpi low");
        Expect(!DashboardValidatePdfOptions(nullptr, L"all", 999, nullptr, nullptr), "validate bad dpi high");
    }

    // Validate with preflight page counts.
    {
        std::vector<PdfImportPreflightInfo> preflight(1);
        preflight[0].path = L"C:\\docs\\a.pdf";
        preflight[0].pageCount = 10;
        int selected = 0;
        Expect(DashboardValidatePdfOptions(nullptr, L"1-2", 144, &preflight, &selected), "validate preflight range");
        Expect(selected == 2, "validate selected 2");
        Expect(!DashboardValidatePdfOptions(nullptr, L"99-100", 144, &preflight, &selected), "validate oob range");
    }

    // Format helpers non-empty.
    {
        Expect(DashboardPdfFormatSelectedPageText(3, 10).find(L"3") != std::wstring::npos, "fmt selected");
        Expect(DashboardPdfFormatOutputText(L"", nullptr).find(L"not selected") != std::wstring::npos, "fmt output empty");
        Expect(DashboardPdfFormatOutputText(L"C:\\out", nullptr).find(L"C:\\out") != std::wstring::npos, "fmt output root");
        auto tree = DashboardPdfFormatOutputTreeText(L"", nullptr);
        Expect(tree.find(L"Output tree") != std::wstring::npos, "fmt tree empty");
        auto est = DashboardPdfFormatEstimateText(L"all", 150, 0, 0, PdfRenderImageFormat::Auto, nullptr);
        Expect(!est.empty(), "fmt estimate");
    }

    // Cloud confirm prompt bilingual non-empty.
    {
        DashboardPdfCloudRiskPolicy risk{};
        auto en = DashboardFormatPdfCloudConfirmPrompt(5, 10, 1, risk);
        Expect(!en.empty(), "cloud prompt en");
        // Prompt always mentions Cloud / PaddleOCR Cloud.
        Expect(en.find(L"Cloud") != std::wstring::npos || en.find(L"Paddle") != std::wstring::npos
            || en.find(L"PDF") != std::wstring::npos, "cloud prompt content");
    }

    if (g_fail) {
        std::cerr << g_fail << " failures\n";
        return 1;
    }
    std::cout << "ALL PASSED\n";
    return 0;
}
