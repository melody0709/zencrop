#include "ocr/ui/OcrDashboardWindow.h"

#include <gdiplus.h>
#include <windows.h>

#include <string>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::wcerr << L"Usage: test_dashboard_window_contract input.pdf output-root\n";
        return 2;
    }

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        std::wcerr << L"Failed to start GDI+.\n";
        return 1;
    }

    std::wstring error;
    bool ok = OcrDashboardWindow::RunWindowContractForTests(argv[1], argv[2], error);

    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (!ok) {
        std::wcerr << L"Dashboard window contract failed: " << error << L"\n";
        return 1;
    }

    std::wcout << L"Dashboard window contract passed.\n";
    return 0;
}
