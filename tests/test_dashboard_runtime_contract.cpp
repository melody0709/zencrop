#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <iostream>
#include <string>

#include "LlamaServerManager.h"
#include "ocr/engine/OcrEngine_PaddleOCR_Doc.h"
#include "ocr/ui/OcrDashboardWindow.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcerr << L"Usage: test_dashboard_runtime_contract output-root\n";
        return 2;
    }

    WSADATA wsaData = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::wcerr << L"Failed to initialize Winsock.\n";
        return 1;
    }

    HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coInit)) {
        std::wcerr << L"Failed to initialize COM: 0x" << std::hex << coInit << L"\n";
        WSACleanup();
        return 1;
    }

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::Status gdiplusStatus = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    if (gdiplusStatus != Gdiplus::Ok) {
        std::wcerr << L"Failed to initialize GDI+.\n";
        CoUninitialize();
        WSACleanup();
        return 1;
    }

    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--disable-gpu");

    std::wstring error;
    bool ok = OcrDashboardWindow::RunRuntimeContractForTests(argv[1], error);

    LlamaServerManager::Instance().GlobalShutdown();
    OcrEnginePaddleDoc::GlobalCleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    WSACleanup();

    int exitCode = 0;
    if (!ok) {
        std::wcerr << L"Dashboard runtime contract failed: " << error << L"\n";
        exitCode = 1;
    } else if (!error.empty()) {
        std::wcout << error << L"\n";
    } else {
        std::wcout << L"Dashboard runtime contract passed.\n";
    }
    std::wcout.flush();
    std::wcerr.flush();

    // Some AMD driver versions raise BEX64 during process detach after this
    // windowed runtime smoke has already finished and cleaned up explicitly.
    // End the test process with the verified contract result instead of
    // letting unrelated DLL/static teardown mask the OCR runtime result.
    TerminateProcess(GetCurrentProcess(), (UINT)exitCode);
    return exitCode;
}
