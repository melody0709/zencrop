#include "ocr/ui/dashboard/DashboardPreviewSecurity.h"

#include <iostream>
#include <string>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    std::wstring norm;
    Expect(DashboardPreviewIsSafeRelativeAssetPath(L"assets\\a.png", norm), "png ok");
    Expect(norm == L"assets\\a.png", "norm png");
    Expect(DashboardPreviewIsSafeRelativeAssetPath(L"./assets/b.JPG", norm), "dot slash");
    Expect(norm == L"assets\\b.JPG" || norm.find(L"assets\\") == 0, "norm jpg");

    Expect(!DashboardPreviewIsSafeRelativeAssetPath(L"C:\\secrets\\x.png", norm), "abs drive");
    Expect(!DashboardPreviewIsSafeRelativeAssetPath(L"//server/x.png", norm), "unc");
    Expect(!DashboardPreviewIsSafeRelativeAssetPath(L"assets\\..\\x.png", norm), "dotdot");
    Expect(!DashboardPreviewIsSafeRelativeAssetPath(L"assets\\x.txt", norm), "bad ext");
    Expect(!DashboardPreviewIsSafeRelativeAssetPath(L"", norm), "empty");

    Expect(DashboardPreviewRenderTokenMatches(L"tok1", L"tok1"), "token match");
    Expect(!DashboardPreviewRenderTokenMatches(L"tok1", L"tok2"), "token mismatch");
    Expect(!DashboardPreviewRenderTokenMatches(L"", L"tok1"), "empty pending");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
