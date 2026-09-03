#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>

#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "ocr/ui/OcrMarkdownPreviewHost.h"
#include "ocr/ui/WebAssetGuard.h"

static std::wstring g_ocrImageDir;

static std::wstring GetExeDirForTest() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    return exePath;
}

std::wstring GetOcrImageDir() {
    if (g_ocrImageDir.empty()) {
        g_ocrImageDir = GetExeDirForTest() + L"\\ocr_images\\";
    }
    CreateDirectoryW(g_ocrImageDir.c_str(), nullptr);
    return g_ocrImageDir;
}

static bool PumpUntil(const std::function<bool()>& done, DWORD timeoutMs) {
    DWORD start = GetTickCount();
    MSG msg = {};
    while (!done()) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetTickCount() - start > timeoutMs) return false;
        Sleep(10);
    }
    return true;
}

static void PumpFor(DWORD durationMs) {
    DWORD start = GetTickCount();
    PumpUntil([&]() {
        return GetTickCount() - start >= durationMs;
    }, durationMs + 100);
}

static bool ExecuteScriptSync(
    OcrMarkdownPreviewHost& host,
    const std::wstring& script,
    std::wstring& result,
    DWORD timeoutMs = 5000)
{
    bool done = false;
    bool succeeded = false;
    result.clear();
    if (!host.ExecuteScriptForTests(script, [&](bool ok, const std::wstring& json) {
            succeeded = ok;
            result = json;
            done = true;
        })) {
        return false;
    }
    return PumpUntil([&]() { return done; }, timeoutMs) && succeeded;
}

static bool WaitForScriptInt(
    OcrMarkdownPreviewHost& host,
    const std::wstring& script,
    int expected,
    DWORD timeoutMs = 5000)
{
    DWORD start = GetTickCount();
    while (GetTickCount() - start <= timeoutMs) {
        std::wstring result;
        if (ExecuteScriptSync(host, script, result, 1500) && _wtoi(result.c_str()) == expected) {
            return true;
        }
        PumpFor(30);
    }
    return false;
}

static bool WriteTestBmp(
    const std::wstring& path,
    int width,
    int height,
    BYTE red,
    BYTE green,
    BYTE blue)
{
    if (width <= 0 || height <= 0) return false;
    const DWORD stride = static_cast<DWORD>((width * 3 + 3) & ~3);
    std::vector<BYTE> pixels(static_cast<size_t>(stride) * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            BYTE* pixel = pixels.data() + static_cast<size_t>(y) * stride + x * 3;
            pixel[0] = blue;
            pixel[1] = green;
            pixel[2] = red;
        }
    }

    BITMAPFILEHEADER fileHeader = {};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER infoHeader = {};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = static_cast<DWORD>(pixels.size());

    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) &&
        written == sizeof(fileHeader) &&
        WriteFile(file, &infoHeader, sizeof(infoHeader), &written, nullptr) &&
        written == sizeof(infoHeader) &&
        WriteFile(file, pixels.data(), static_cast<DWORD>(pixels.size()), &written, nullptr) &&
        written == pixels.size();
    CloseHandle(file);
    return ok;
}

static void RemoveMappingFixture(const std::wstring& root) {
    DeleteFileW((root + L"\\assets\\page_0001_img_001.bmp").c_str());
    RemoveDirectoryW((root + L"\\assets").c_str());
    RemoveDirectoryW(root.c_str());
}

static bool CopyGuardFixture(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::wstring& error)
{
    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
    ec.clear();
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        error = L"could not create guard fixture directory";
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(source, ec)) {
        if (ec) {
            error = L"could not enumerate guard fixture source";
            return false;
        }
        std::filesystem::copy(
            entry.path(), destination / entry.path().filename(),
            std::filesystem::copy_options::recursive,
            ec);
        if (ec) {
            error = L"could not copy guard fixture payload";
            return false;
        }
    }
    return true;
}

static bool ExpectGuardFailure(
    const std::filesystem::path& root,
    ZenCrop::WebAssets::GuardFailure expected,
    const wchar_t* scenario,
    std::wstring& error)
{
    const ZenCrop::WebAssets::GuardResult result =
        ZenCrop::WebAssets::VerifyWebAssetDirectory(root.wstring());
    if (result.failure == expected) return true;
    error = std::wstring(L"WebAssetGuard ") + scenario + L" expected " +
        ZenCrop::WebAssets::GuardFailureName(expected) + L" but got " +
        ZenCrop::WebAssets::GuardFailureName(result.failure) + L": " + result.message;
    return false;
}

static bool IsSymlinkFixtureUnavailable(DWORD errorCode) {
    switch (errorCode) {
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_ACCESS_DENIED:
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_PARAMETER:
        return true;
    default:
        return false;
    }
}

static bool RunWebAssetGuardFixtures(std::wstring& error, bool& skipped) {
    skipped = false;
    namespace fs = std::filesystem;
    const fs::path source = fs::path(GetExeDirForTest()) / L"webview_assets";
    const fs::path base = fs::path(GetExeDirForTest()) / L"web_asset_guard_fixtures";
    std::error_code ec;
    fs::remove_all(base, ec);
    if (!fs::is_directory(source, ec)) {
        error = L"trusted WebView2 test asset fixture is missing";
        return false;
    }

    auto finish = [&](bool success) {
        std::error_code cleanupError;
        fs::remove_all(base, cleanupError);
        return success;
    };

    const fs::path valid = base / L"valid";
    if (!CopyGuardFixture(source, valid, error)) return finish(false);
    if (!ZenCrop::WebAssets::VerifyWebAssetDirectory(valid.wstring()).ok()) {
        error = L"WebAssetGuard rejected a complete trusted fixture";
        return finish(false);
    }

    const fs::path missing = base / L"missing";
    if (!CopyGuardFixture(source, missing, error)) return finish(false);
    fs::remove(missing / L"ocr-preview" / L"index.html", ec);
    if (ec || !ExpectGuardFailure(missing, ZenCrop::WebAssets::GuardFailure::MissingFile, L"missing", error)) {
        return finish(false);
    }

    const fs::path modified = base / L"modified";
    if (!CopyGuardFixture(source, modified, error)) return finish(false);
    const fs::path modifiedFile = modified / L"ocr-preview" / L"index.html";
    std::fstream modifiedStream(modifiedFile, std::ios::in | std::ios::out | std::ios::binary);
    char firstByte = 0;
    modifiedStream.read(&firstByte, 1);
    modifiedStream.seekp(0);
    firstByte ^= 0x01;
    modifiedStream.write(&firstByte, 1);
    modifiedStream.close();
    if (!modifiedStream || !ExpectGuardFailure(modified, ZenCrop::WebAssets::GuardFailure::HashMismatch, L"single-byte modification", error)) {
        return finish(false);
    }

    const fs::path unknown = base / L"unknown";
    if (!CopyGuardFixture(source, unknown, error)) return finish(false);
    std::ofstream unknownFile(unknown / L"unexpected-extra.js", std::ios::binary);
    unknownFile << "unexpected";
    unknownFile.close();
    if (!unknownFile || !ExpectGuardFailure(unknown, ZenCrop::WebAssets::GuardFailure::UnknownFile, L"unknown extra", error)) {
        return finish(false);
    }

    const fs::path caseVariant = base / L"case-variant";
    if (!CopyGuardFixture(source, caseVariant, error)) return finish(false);
    // NTFS does not reliably apply a case-only rename in one operation.
    // Route through a unique sibling so this fixture works on ordinary
    // case-insensitive Windows volumes as well as case-sensitive test roots.
    const fs::path caseRenameStaging = caseVariant / L"vendor-case-rename-staging";
    fs::rename(caseVariant / L"vendor", caseRenameStaging, ec);
    if (!ec) fs::rename(caseRenameStaging, caseVariant / L"VENDOR", ec);
    if (ec || !ExpectGuardFailure(caseVariant, ZenCrop::WebAssets::GuardFailure::CaseCollision, L"case collision", error)) {
        if (ec) error = L"could not create case-variant guard fixture";
        return finish(false);
    }

    const fs::path reparse = base / L"reparse";
    if (!CopyGuardFixture(source, reparse, error)) return finish(false);
    const fs::path reparsePath = reparse / L"unexpected-reparse";
    DWORD symlinkFlags = SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2; // ALLOW_UNPRIVILEGED_CREATE
    if (!CreateSymbolicLinkW(reparsePath.c_str(), source.c_str(), symlinkFlags)) {
        const DWORD symlinkError = GetLastError();
        if (IsSymlinkFixtureUnavailable(symlinkError)) {
            skipped = true;
            error = L"symbolic-link fixture is unavailable on this Windows host (Win32 error " +
                std::to_wstring(symlinkError) + L")";
            return finish(true);
        }
        error = L"could not create reparse guard fixture (Win32 error " +
            std::to_wstring(symlinkError) + L")";
        return finish(false);
    }
    if (!ExpectGuardFailure(reparse, ZenCrop::WebAssets::GuardFailure::ReparsePoint, L"reparse", error)) {
        return finish(false);
    }

    return finish(true);
}

int wmain() {
    std::wstring error;
    bool webAssetGuardSkipped = false;
    if (!RunWebAssetGuardFixtures(error, webAssetGuardSkipped)) {
        std::wcerr << L"WebAssetGuard fixture contract failed: " << error << L"\n";
        return 1;
    }
    if (webAssetGuardSkipped) {
        std::wcerr << L"WebAssetGuard fixture contract skipped: " << error << L"\n";
        return 77;
    }
    if (!OcrMarkdownPreviewHost::RunStaticContractForTests(error)) {
        std::wcerr << L"Static preview contract failed: " << error << L"\n";
        return 1;
    }

    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co)) {
        std::wcerr << L"CoInitializeEx failed: 0x" << std::hex << co << L"\n";
        return 1;
    }

    const wchar_t* className = L"ZenCropPreviewContractWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"ZenCrop Preview Contract",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        480,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!hwnd) {
        std::wcerr << L"CreateWindowExW failed\n";
        CoUninitialize();
        return 1;
    }

    bool ready = false;
    int readyCount = 0;
    bool unavailable = false;
    bool renderError = false;
    bool processFailed = false;
    bool contentMetricsReceived = false;
    OcrMarkdownPreviewHost::PreviewContentMetrics contentMetrics;
    std::wstring unavailableMessage;
    std::wstring renderErrorMessage;
    int imageLoadErrorCount = 0;
    std::wstring imageLoadErrorSrc;
    int previewSaveCount = 0;
    int previewRestoreCount = 0;
    std::wstring restoredBlockId;
    DashboardSourceEditRequest restoredSourceEdit;
    std::wstring savedBlockId;
    std::wstring savedContent;
    std::wstring savedOriginalContent;
    DashboardSourceEditRequest savedSourceEdit;
    bool rejectNextPreviewSave = false;
    int saveCountBeforeInvalidFormula = 0;
    int previewHoverCount = 0;
    std::wstring hoveredPreviewBlockId;
    int previewSelectCount = 0;
    std::wstring selectedPreviewBlockId;

    OcrMarkdownPreviewHost host;
    OcrMarkdownPreviewHost::Callbacks callbacks;
    callbacks.onReady = [&]() {
        ready = true;
        readyCount++;
    };
    callbacks.onUnavailable = [&](const std::wstring& message) {
        unavailable = true;
        unavailableMessage = message;
    };
    callbacks.onRenderError = [&](int, const std::wstring& message) {
        renderError = true;
        renderErrorMessage = message;
    };
    callbacks.onContentMetrics = [&](const OcrMarkdownPreviewHost::PreviewContentMetrics& metrics) {
        contentMetricsReceived = true;
        contentMetrics = metrics;
    };
    callbacks.onImageLoadError = [&](int, const std::wstring& src) {
        imageLoadErrorCount++;
        imageLoadErrorSrc = src;
    };
    callbacks.onProcessFailed = [&]() {
        processFailed = true;
    };
    callbacks.onOpenExternal = [](const std::wstring&) {};
    callbacks.onAcceleratorKey = [](UINT, bool) {
        return true;
    };
    callbacks.onPreviewBlockHover = [&](const std::wstring& id) {
        previewHoverCount++;
        hoveredPreviewBlockId = id;
    };
    callbacks.onPreviewBlockSelect = [&](const std::wstring& id) {
        previewSelectCount++;
        selectedPreviewBlockId = id;
    };
    callbacks.onPreviewBlockSave = [&](
        const std::wstring& id,
        const std::wstring& content,
        const DashboardSourceEditRequest& sourceEdit,
        const std::wstring& renderToken) {
        previewSaveCount++;
        savedBlockId = id;
        savedContent = content;
        savedOriginalContent = sourceEdit.expectedSource;
        savedSourceEdit = sourceEdit;
        if (rejectNextPreviewSave) {
            rejectNextPreviewSave = false;
            host.PostPreviewBlockSaveResult(id, renderToken, false, L"persist_failed");
        } else {
            host.PostPreviewBlockSaveResult(id, renderToken, true);
            host.SetEditingBlock(L"");
        }
    };
    callbacks.onPreviewBlockRestore = [&](
        const std::wstring& id,
        const DashboardSourceEditRequest& sourceEdit,
        const std::wstring& renderToken) {
        previewRestoreCount++;
        restoredBlockId = id;
        restoredSourceEdit = sourceEdit;
        host.PostPreviewBlockRestoreResult(id, renderToken, true);
        host.SetEditingBlock(L"");
    };

    RECT bounds = {0, 0, 640, 480};
    bool created = host.Create(hwnd, bounds, callbacks);
    if (!created) {
        DestroyWindow(hwnd);
        CoUninitialize();
        if (unavailableMessage.find(L"WebView2 Runtime is missing") != std::wstring::npos) {
            std::wcout << L"WebView2 runtime is missing; runtime smoke skipped after static contract passed.\n";
            return 0;
        }
        std::wcerr << L"Preview host create failed: " << unavailableMessage << L"\n";
        return 1;
    }

    bool observed = PumpUntil([&]() {
        return ready || unavailable || processFailed;
    }, 15000);
    if (!observed) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Timed out waiting for WebView2 preview readiness\n";
        return 1;
    }
    if (unavailable || processFailed || !ready) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview host did not become ready: " << unavailableMessage << L"\n";
        return 1;
    }

    std::wstring smokeResult;
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){return /^zencrop-assets-[0-9a-f]{32}\.invalid$/.test(location.hostname)&&location.pathname==="/ocr-preview/index.html"&&typeof window.ZenCropPreviewSecurity==="object"?1:0;})())JS",
            smokeResult) ||
        _wtoi(smokeResult.c_str()) != 1) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview runtime did not load the trusted versioned asset origin\n";
        return 1;
    }
    host.RenderMarkdown(1, L"# Runtime smoke\n\ntrusted preview payload");
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){var preview=document.querySelector("#preview");return preview&&preview.textContent.indexOf("trusted preview payload")>=0?1:0;})())JS",
            1)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview runtime did not render a trusted payload\n";
        return 1;
    }
    host.SetTextFontSize(22);
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){
              var preview=document.querySelector("#preview");
              if(!preview)return 0;
              var style=window.getComputedStyle(preview);
              return preview.style.getPropertyValue("--preview-font-size").trim()==="22px"&&
                style.fontSize==="22px"?1:0;
            })())JS",
            1)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview runtime did not apply the configured source font size\n";
        return 1;
    }
    host.SetTextFontSize(14);
    if (!PumpUntil([&]() { return contentMetricsReceived; }, 2000) ||
        contentMetrics.scrollHeight <= 0 || contentMetrics.scrollWidth <= 0 ||
        contentMetrics.clientWidth <= 0 || contentMetrics.renderToken != L"1") {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview content metrics callback contract failed\n";
        return 1;
    }

    // The main Preview rail is intentionally zero-width in the document and
    // replaced by a non-layout overlay thumb. Verify that a long document
    // still exposes a scrollable viewport, starts visually hidden, appears
    // only after intentional right-edge dwell, and does not reflow when shown.
    std::wstring scrollbarMarkdown = L"# Scrollbar contract\n\n";
    for (int i = 0; i < 240; ++i) {
        scrollbarMarkdown += L"Preview scroll line " + std::to_wstring(i) + L"\n\n";
    }
    scrollbarMarkdown += L"\n```text\n";
    scrollbarMarkdown += std::wstring(480, L'x');
    scrollbarMarkdown += L"\n```\n\n";
    scrollbarMarkdown += L"| Wide column | Another wide column |\n| --- | --- |\n| ";
    scrollbarMarkdown += std::wstring(220, L't');
    scrollbarMarkdown += L" | ";
    scrollbarMarkdown += std::wstring(220, L'u');
    scrollbarMarkdown += L" |\n\n";
    host.RenderMarkdown(4, scrollbarMarkdown);
    const std::wstring scrollbarReadyScript = LR"JS((function(){
      var preview=document.querySelector("#preview");
      var bar=document.querySelector("#preview-scrollbar");
      var style=bar&&window.getComputedStyle(bar);
      return preview&&bar&&preview.scrollHeight>preview.clientHeight+1&&
        preview.clientWidth>=preview.offsetWidth-1&&
        style&&style.opacity==="0"&&style.pointerEvents==="none"&&
        !bar.classList.contains("is-visible")?1:0;
    })())JS";
    if (!WaitForScriptInt(host, scrollbarReadyScript, 1, 5000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar did not start hidden without a layout gutter\n";
        return 1;
    }
    std::wstring scrollbarWidthResult;
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){return document.querySelector("#preview").clientWidth;})())JS",
            scrollbarWidthResult)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Could not measure Preview client width before scrollbar reveal\n";
        return 1;
    }
    const int clientWidthBeforeReveal = _wtoi(scrollbarWidthResult.c_str());
    std::wstring scrollbarHeightResult;
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){return document.querySelector("#preview").scrollHeight;})())JS",
            scrollbarHeightResult)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Could not measure Preview scroll height before scrollbar reveal\n";
        return 1;
    }
    const int scrollHeightBeforeReveal = _wtoi(scrollbarHeightResult.c_str());
    const std::wstring scrollbarVisibleScript =
        LR"JS((function(){return document.querySelector("#preview-scrollbar").classList.contains("is-visible")?1:0;})())JS";

    // A fast pass across the native Preview/Translation divider must cancel
    // the dwell timer before the thumb becomes visible.
    host.SetVerticalScrollbarBoundaryHover(true);
    PumpFor(90);
    if (!WaitForScriptInt(host, scrollbarVisibleScript, 0, 1000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar appeared before the boundary dwell delay\n";
        return 1;
    }
    host.SetVerticalScrollbarBoundaryHover(false);
    PumpFor(260);
    if (!WaitForScriptInt(host, scrollbarVisibleScript, 0, 1000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar survived a fast native boundary pass\n";
        return 1;
    }

    // Staying at the native divider long enough reveals the Result Preview
    // thumb, while preserving the Markdown viewport geometry.
    host.SetVerticalScrollbarBoundaryHover(true);
    if (!WaitForScriptInt(
            host,
            scrollbarVisibleScript,
            1,
            3000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Native Preview boundary dwell bridge did not show the overlay thumb\n";
        return 1;
    }
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){return document.querySelector("#preview").clientWidth;})())JS",
            scrollbarWidthResult) ||
        _wtoi(scrollbarWidthResult.c_str()) != clientWidthBeforeReveal) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview scrollbar boundary reveal changed the Markdown client width\n";
        return 1;
    }
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){return document.querySelector("#preview").scrollHeight;})())JS",
            scrollbarHeightResult) ||
        _wtoi(scrollbarHeightResult.c_str()) != scrollHeightBeforeReveal) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview scrollbar boundary reveal changed the Markdown scroll height\n";
        return 1;
    }
    host.SetVerticalScrollbarBoundaryHover(false);
    PumpFor(850);
    if (!WaitForScriptInt(host, scrollbarVisibleScript, 0, 1000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar did not fade after boundary exit\n";
        return 1;
    }

    // Programmatic/main-container scrolling and wheel input must not reveal
    // the hidden thumb. They retain normal scrolling behavior only.
    std::wstring scrollbarInteractionResult;
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){var preview=document.querySelector("#preview");if(!preview)return 0;preview.scrollTop=Math.min(80,preview.scrollHeight-preview.clientHeight);preview.dispatchEvent(new WheelEvent("wheel",{bubbles:true,deltaY:120}));return 1;})())JS",
            scrollbarInteractionResult) ||
        _wtoi(scrollbarInteractionResult.c_str()) != 1) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Could not exercise Preview scroll/wheel visibility contract\n";
        return 1;
    }
    PumpFor(300);
    if (!WaitForScriptInt(host, scrollbarVisibleScript, 0, 1000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview scroll or wheel unexpectedly revealed the overlay thumb\n";
        return 1;
    }

    // The in-WebView right boundary follows the same dwell rule: passing over
    // it does not reveal the thumb, but deliberate dwell does.
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){var preview=document.querySelector("#preview");if(!preview)return 0;var rect=preview.getBoundingClientRect();preview.dispatchEvent(new MouseEvent("mousemove",{bubbles:true,clientX:Math.floor(rect.right)-1,clientY:Math.floor(rect.top)+8}));preview.dispatchEvent(new MouseEvent("mousemove",{bubbles:true,clientX:Math.floor(rect.left)+1,clientY:Math.floor(rect.top)+8}));return 1;})())JS",
            scrollbarInteractionResult) ||
        _wtoi(scrollbarInteractionResult.c_str()) != 1) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Could not exercise Preview right-boundary pass contract\n";
        return 1;
    }
    PumpFor(260);
    if (!WaitForScriptInt(host, scrollbarVisibleScript, 0, 1000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar survived a fast right-boundary pass\n";
        return 1;
    }
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){var preview=document.querySelector("#preview");if(!preview)return 0;var rect=preview.getBoundingClientRect();preview.dispatchEvent(new MouseEvent("mousemove",{bubbles:true,clientX:Math.floor(rect.right)-1,clientY:Math.floor(rect.top)+8}));return 1;})())JS",
            scrollbarInteractionResult) ||
        _wtoi(scrollbarInteractionResult.c_str()) != 1 ||
        !WaitForScriptInt(host, scrollbarVisibleScript, 1, 3000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview right-boundary dwell did not reveal the overlay thumb\n";
        return 1;
    }
    // Move back into the document before switching to the native divider
    // contract, so the two boundary sources are tested independently.
    if (!ExecuteScriptSync(
            host,
            LR"JS((function(){var preview=document.querySelector("#preview");if(!preview)return 0;var rect=preview.getBoundingClientRect();preview.dispatchEvent(new MouseEvent("mousemove",{bubbles:true,clientX:Math.floor(rect.left)+1,clientY:Math.floor(rect.top)+8}));return 1;})())JS",
            scrollbarInteractionResult) ||
        _wtoi(scrollbarInteractionResult.c_str()) != 1) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Could not clear the in-WebView boundary before the native render-reset contract\n";
        return 1;
    }

    // Keep the native boundary hover active across a render. The host does
    // not emit another enter message if the pointer never left the divider;
    // the page must therefore retain that state and restart the dwell timer
    // after the render reset itself hides the old thumb.
    host.SetVerticalScrollbarBoundaryHover(true);
    if (!WaitForScriptInt(host, scrollbarVisibleScript, 1, 3000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar could not be shown for the native render-reset contract\n";
        return 1;
    }
    host.RenderMarkdown(5, scrollbarMarkdown);
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){
              var preview=document.querySelector("#preview");
              var bar=document.querySelector("#preview-scrollbar");
              var style=bar&&window.getComputedStyle(bar);
              return preview&&bar&&preview.textContent.indexOf("Preview scroll line 0")>=0&&
                style&&style.opacity==="0"&&style.pointerEvents==="none"&&
                !bar.classList.contains("is-visible")?1:0;
            })())JS",
            1,
            5000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview render did not reset the previous overlay scrollbar visibility\n";
        return 1;
    }
    if (!WaitForScriptInt(
            host,
            scrollbarVisibleScript,
            1,
            3000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar did not re-reveal after a render reset with native boundary hover retained\n";
        return 1;
    }
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){
              var preview=document.querySelector("#preview");
              var code=preview&&preview.querySelector("pre");
              var table=preview&&preview.querySelector(".table-scroll");
              if (!code||!table||code.scrollWidth<=code.clientWidth+1||table.scrollWidth<=table.clientWidth+1) return 0;
              code.scrollLeft=code.scrollWidth;
              table.scrollLeft=table.scrollWidth;
              return code.scrollLeft>0&&table.scrollLeft>0?1:0;
            })())JS",
            1,
            3000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Nested code/table horizontal scrolling contract failed\n";
        return 1;
    }
    host.SetVerticalScrollbarBoundaryHover(false);
    PumpFor(850);
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){return !document.querySelector("#preview-scrollbar").classList.contains("is-visible")?1:0;})())JS",
            1,
            1000)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview overlay scrollbar did not fade after boundary exit\n";
        return 1;
    }
    for (double zoom : { 0.75, 1.0, 1.25, 1.5 }) {
        host.SetZoomFactor(zoom);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var preview=document.querySelector("#preview");
                  return preview&&preview.scrollHeight>preview.clientHeight+1&&
                    preview.clientWidth>=preview.offsetWidth-1?1:0;
                })())JS",
                1,
                3000)) {
            host.Destroy();
            DestroyWindow(hwnd);
            CoUninitialize();
            std::wcerr << L"Preview overlay scrollbar lost its no-gutter contract after zoom change\n";
            return 1;
        }
    }
    host.SetZoomFactor(1.0);

    // Source uses single line breaks for OCR visual lines. The shared
    // Dashboard/translation Preview renderer must retain them instead of
    // folding them into spaces.
    host.RenderMarkdown(2, L"line one\nline two\nline three");
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){
              var paragraph=document.querySelector("#preview p");
              return paragraph&&paragraph.querySelectorAll("br").length===2?1:0;
            })())JS",
            1)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview did not preserve Source line breaks\n";
        return 1;
    }
    host.RenderMarkdown(3, L"compact preview", true);
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){
              var preview=document.querySelector("#preview");
              return preview&&preview.classList.contains("compact-preview")&&
                !preview.classList.contains("translation-compact")?1:0;
            })())JS",
            1)) {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcerr << L"Preview compact layout class contract failed\n";
        return 1;
    }

    // The default CTest contract is deterministic: compiled manifest guard,
    // trusted virtual origin, and real WebView2 rendering. The legacy full
    // WYSIWYG interaction matrix remains available for an explicitly opted-in
    // interactive regression run, where its long async document scenarios can
    // be isolated from normal build/package gates.
    wchar_t extendedContracts[2] = {};
    if (GetEnvironmentVariableW(
            L"ZENCROP_PREVIEW_EXTENDED_CONTRACTS", extendedContracts,
            static_cast<DWORD>(sizeof(extendedContracts) / sizeof(extendedContracts[0]))) != 1 ||
        extendedContracts[0] != L'1') {
        host.Destroy();
        DestroyWindow(hwnd);
        CoUninitialize();
        std::wcout << L"WebView2 preview contract passed.\n";
        return 0;
    }

    std::vector<OcrMarkdownPreviewHost::PreviewBlock> blocks;
    auto addBlock = [&](const wchar_t* id, int order, const wchar_t* label, const wchar_t* content) {
        OcrMarkdownPreviewHost::PreviewBlock block;
        block.id = id;
        block.order = order;
        block.label = label;
        block.displayLabel = label;
        block.content = content;
        blocks.push_back(std::move(block));
    };
    addBlock(L"page_1:title", 1, L"doc_title", L"Editable title");
    addBlock(L"page_1:formula", 2, L"display_formula", L"$$ x^2 $$");
    addBlock(L"page_1:formula_number", 3, L"formula_number", L"(2.1)");
    addBlock(L"page_1:paragraph", 4, L"text", L"A later paragraph refers to Eq. (2.1).");
    addBlock(L"page_1:table", 5, L"table", L"| A | B |\n| --- | --- |\n| 1 | 2 |");
    addBlock(L"page_1:list", 6, L"text", L"Parent Child");
    // This is the same empty-image plus adjacent-caption shape emitted for
    // local PaddleOCR-VL documents.  They must remain two different nodes.
    addBlock(L"page_1:image", 7, L"image", L"");
    addBlock(L"page_1:figure_title", 8, L"figure_title", L"FIG. 2. Preview image caption.");
    addBlock(
        L"page_1:multi_paragraph", 9, L"text",
        L"First paragraph contains \\(x+y\\).\n\nSecond paragraph remains part of the same detected block.");
    blocks[0].edited = true;
    blocks[0].canRestoreOriginal = true;

    const std::wstring markdown =
        L"# Editable title\n\n"
        L"$$ x^2 \\tag{2.1} $$\n\n"
        L"A later paragraph refers to Eq. (2.1).\n\n"
        L"| A | B |\n| --- | --- |\n| 1 | 2 |\n\n"
        L"```mermaid\nflowchart TD\n  A --> B\n```\n\n"
        L"3. Parent\n   - Child\n\n"
        L"<div style=\"text-align: center;\"><img src=\"https://zencrop-ocr-images.invalid/preview%20test.png\" alt=\"Image\" width=\"42%\" /></div>\n\n"
        L"FIG. 2. Preview image caption.\n\n"
        L"First paragraph contains \\(x+y\\).\n\n"
        L"Second paragraph remains part of the same detected block.";

    std::wstring runtimeError;

    // PaddleOCR Cloud emits inline math with horizontal padding inside the
    // dollar delimiters. Preview accepts that provider dialect without
    // broadening inline math into currency, escaped dollars, code, or lines.
    const std::wstring cloudInlineMathMarkdown =
        LR"MD(Cloud spaced formula: $ \Psi_I(\mathbf{r};\mathbf{R}) $.

Tight formula: $N^e$.

Currency stays text: $ 5 $.

Escaped dollars stay text: \$x\$.

Code stays code: `$ \beta $`.

An inline delimiter cannot span lines: $ x
still text $.)MD";
    host.RenderMarkdown(40, cloudInlineMathMarkdown);
    if (!WaitForScriptInt(
            host,
            LR"JS((function(){
              var preview=document.querySelector("#preview");
              if(!preview)return 0;
              var math=Array.from(preview.querySelectorAll(".math-node.math-inline"));
              var code=preview.querySelector("code");
              var text=preview.textContent||"";
              return math.length===2&&math.every(function(node){return !!node.querySelector(".katex");})&&
                text.indexOf("$ 5 $")>=0&&text.indexOf("$x$")>=0&&
                code&&code.textContent==="$ \\beta $"?1:0;
            })())JS",
            1)) {
        runtimeError = L"Cloud spaced inline math was not rendered safely by the shared Preview parser.";
    }

    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    wchar_t mappingFixtureName[96] = {};
    swprintf_s(
        mappingFixtureName,
        L"ZenCropPreviewMapping_%lu_%llu",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()));
    const std::wstring mappingFixtureBase = std::wstring(tempPath) + mappingFixtureName;
    const std::wstring mappingRootA = mappingFixtureBase + L"\\root_a";
    const std::wstring mappingRootB = mappingFixtureBase + L"\\root_b";
    const std::wstring mappingAssetA = mappingRootA + L"\\assets\\page_0001_img_001.bmp";
    const std::wstring mappingAssetB = mappingRootB + L"\\assets\\page_0001_img_001.bmp";

    bool mappingDirectoriesCreated =
        CreateDirectoryW(mappingFixtureBase.c_str(), nullptr) &&
        CreateDirectoryW(mappingRootA.c_str(), nullptr) &&
        CreateDirectoryW((mappingRootA + L"\\assets").c_str(), nullptr) &&
        CreateDirectoryW(mappingRootB.c_str(), nullptr) &&
        CreateDirectoryW((mappingRootB + L"\\assets").c_str(), nullptr);
    if (!mappingDirectoriesCreated ||
        !WriteTestBmp(mappingAssetA, 2, 1, 255, 0, 0) ||
        !WriteTestBmp(mappingAssetB, 1, 2, 0, 0, 255)) {
        runtimeError = L"Failed to create virtual-host mapping fixtures.";
    }

    const std::wstring mappingMarkdown =
        L"![mapping](https://zencrop-preview-output.invalid/assets/page_0001_img_001.bmp?v=same-url)";
    auto verifyMappedRoot = [&](const std::wstring& root, int expectedDimensions) {
        int previousReadyCount = readyCount;
        host.SetLocalAssetRoot(root);
        host.RenderMarkdown(41, mappingMarkdown);
        if (!PumpUntil([&]() { return readyCount > previousReadyCount; }, 8000)) {
            runtimeError = L"Preview did not become ready after changing its local asset root.";
            return;
        }
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var image=document.querySelector("#preview img");
                  return image&&image.complete&&image.naturalWidth>0
                    ? image.naturalWidth*1000+image.naturalHeight : 0;
                })())JS",
                expectedDimensions,
                5000)) {
            runtimeError = L"Preview loaded an image from the wrong local asset root.";
        }
    };

    if (runtimeError.empty()) verifyMappedRoot(mappingRootA, 2001);
    if (runtimeError.empty()) verifyMappedRoot(mappingRootB, 1002);
    if (runtimeError.empty()) verifyMappedRoot(mappingRootA, 2001);

    if (runtimeError.empty()) {
        const int previousImageErrorCount = imageLoadErrorCount;
        host.RenderMarkdown(
            42,
            L"![missing](https://zencrop-preview-output.invalid/assets/missing.bmp?v=missing)");
        if (!PumpUntil(
                [&]() { return imageLoadErrorCount > previousImageErrorCount; },
                5000) ||
            imageLoadErrorSrc.find(L"zencrop-preview-output.invalid/assets/missing.bmp") ==
                std::wstring::npos) {
            runtimeError = L"Managed Preview image failures were not reported to the host.";
        }
    }

    auto renderFixture = [&]() {
        host.RenderMarkdownBlocks(7, markdown, blocks, markdown);
        return WaitForScriptInt(
            host,
            LR"JS((function(){return document.querySelectorAll(".ocr-preview-linked-block[data-block-id]").length;})())JS",
            9);
    };

    if (!renderFixture()) {
        std::wstring mappedIds;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var linked=Array.from(document.querySelectorAll(".ocr-preview-linked-block[data-block-id]")).map(function(node){return node.getAttribute("data-block-id")+":"+node.nodeName;}).join(",");
              var paragraphs=Array.from(document.querySelectorAll("#preview p")).map(function(node){return node.textContent;}).join("|");
              return linked+"; paragraphs="+paragraphs;
            })())JS",
            mappedIds);
        runtimeError = L"Preview blocks were not mapped to all visible Markdown nodes: " + mappedIds;
    }

    if (runtimeError.empty()) {
        std::wstring result;
        if (!ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var image=document.querySelector('[data-block-id="page_1:image"]');
                  var caption=document.querySelector('[data-block-id="page_1:figure_title"]');
                  var number=document.querySelector('[data-block-id="page_1:formula_number"]');
                  var paragraph=document.querySelector('[data-block-id="page_1:paragraph"]');
                  var formula=document.querySelector('[data-block-id="page_1:formula"]');
                  var multi=document.querySelectorAll('[data-block-id="page_1:multi_paragraph"]');
                  return image&&image.nodeName==="IMG"&&caption&&caption.nodeName==="P"&&
                    paragraph&&paragraph.nodeName==="P"&&!number&&formula&&
                    (formula.getAttribute("data-linked-block-ids")||"").indexOf("page_1:formula_number")>=0&&
                    multi.length===2&&multi[0].nodeName==="P"&&multi[1].nodeName==="P"?1:0;
                })())JS",
                result) ||
            _wtoi(result.c_str()) != 1) {
            std::wstring mappingDetails;
            ExecuteScriptSync(
                host,
                LR"JS((function(){return Array.from(document.querySelectorAll('.ocr-preview-linked-block[data-block-id]')).map(function(node){return node.getAttribute('data-block-id')+':'+node.nodeName+':'+(node.getAttribute('data-linked-block-ids')||'');}).join('|');})())JS",
                mappingDetails);
            runtimeError = L"Empty image, figure caption, and following paragraph were not bound correctly, or formula number claimed a Preview node: " + mappingDetails;
        }
    }

    // A PDF root renders document-level Markdown while its Canvas shows Page 1.
    // Verify that the Page 1 overlay block can skip the synthetic document page
    // heading, bind to its body node, and preserve both selection directions
    // without changing the Source Rail selection to a Page child.
    if (runtimeError.empty()) {
        OcrMarkdownPreviewHost::PreviewBlock rootPageOneBlock;
        rootPageOneBlock.id = L"page_1:root_cover_block";
        rootPageOneBlock.pageIndex = 0;
        rootPageOneBlock.order = 1;
        rootPageOneBlock.label = L"text";
        rootPageOneBlock.displayLabel = L"text";
        rootPageOneBlock.content = L"Root cover paragraph with Page 1 blocks.";
        rootPageOneBlock.editable = false;
        const std::wstring rootDocumentMarkdown =
            L"## Page 1\n\n"
            L"Root cover paragraph with Page 1 blocks.\n\n"
            L"## Page 2\n\n"
            L"A later page must not claim the Page 1 block.";
        host.RenderMarkdownBlocks(
            7,
            rootDocumentMarkdown,
            { rootPageOneBlock },
            rootDocumentMarkdown);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var node=document.querySelector('[data-block-id="page_1:root_cover_block"]');
                  return node&&node.nodeName==="P"&&
                    node.textContent==="Root cover paragraph with Page 1 blocks."?1:0;
                })())JS",
                1)) {
            runtimeError = L"PDF root Page 1 block did not bind past the document-level Page 1 heading.";
        }

        if (runtimeError.empty()) {
            host.SetSelectedBlock(L"page_1:root_cover_block", true);
            if (!WaitForScriptInt(
                    host,
                    LR"JS((function(){
                      var node=document.querySelector('.ocr-preview-linked-block.is-selected');
                      return node&&node.getAttribute('data-block-id')==="page_1:root_cover_block"?1:0;
                    })())JS",
                    1)) {
                runtimeError = L"PDF root Canvas selection did not highlight the Page 1 Preview block.";
            }
        }

        if (runtimeError.empty() && !WaitForScriptInt(
                host,
                LR"JS((function(){
                  var node=document.querySelector('[data-block-id="page_1:root_cover_block"]');
                  if(!node)return 0;
                  node.dispatchEvent(new MouseEvent('mouseenter',{bubbles:true}));
                  node.dispatchEvent(new MouseEvent('dblclick',{bubbles:true,cancelable:true}));
                  var edit=document.querySelector('.ocr-preview-floating-toolbar .ocr-preview-tool-button.is-primary');
                  return edit&&getComputedStyle(edit).display==="none"&&
                    !document.querySelector('.ocr-preview-inline-editor')?1:0;
                })())JS",
                1)) {
            runtimeError = L"Document-level PDF root block exposed a page-artifact editor instead of read-only selection/copy.";
        }

        if (runtimeError.empty()) {
            const int selectCount = previewSelectCount;
            std::wstring result;
            if (!ExecuteScriptSync(
                    host,
                    LR"JS((function(){
                      var node=document.querySelector('[data-block-id="page_1:root_cover_block"]');
                      if(node)node.click();
                      return node?1:0;
                    })())JS",
                    result) ||
                _wtoi(result.c_str()) != 1 ||
                !PumpUntil([&]() {
                    return previewSelectCount == selectCount + 1 &&
                        selectedPreviewBlockId == L"page_1:root_cover_block";
                }, 3000)) {
                runtimeError = L"Clicking the PDF root Page 1 Preview block returned the wrong block ID.";
            }
        }
    }

    // Page decorations remain selectable Canvas rectangles, but PaddleOCR's
    // Markdown omits them. Reproduce the real Page 2 ordering: two headers,
    // body paragraphs, and later section headings. The omitted headers must
    // never advance the forward-only Preview source cursor.
    if (runtimeError.empty()) {
        std::vector<OcrMarkdownPreviewHost::PreviewBlock> page2Blocks;
        auto addPage2Block = [&](const wchar_t* id, int order, const wchar_t* label, const wchar_t* content) {
            OcrMarkdownPreviewHost::PreviewBlock block;
            block.id = id;
            block.pageIndex = 1;
            block.order = order;
            block.label = label;
            block.displayLabel = label;
            block.content = content;
            page2Blocks.push_back(std::move(block));
        };
        addPage2Block(L"page_2:layout_1", 1, L"header", L"中国科技期刊研究");
        addPage2Block(L"page_2:layout_2", 2, L"header", L"2016年7月 第27卷 第7期");
        addPage2Block(
            L"page_2:layout_3", 3, L"text",
            L"片、表格、视频等附加信息，但添加入附加图片的PDF格式全文却未包含视频资料。");
        addPage2Block(
            L"page_2:layout_4", 4, L"text",
            L"利用最新的 Adobe Acrobat XI 可以很方便地在现有形态的 PDF 文档中插入视频（包括动画）以及音频文件，并可采用 Adobe Acrobat 和 Adobe Reader 流畅和清晰地播放。");
        addPage2Block(L"page_2:layout_5", 5, L"paragraph_title", L"1 视频转换、编辑与合并");
        addPage2Block(L"page_2:layout_10", 10, L"paragraph_title", L"1.1 利用格式工厂转换、编辑、合并视频");
        addPage2Block(L"page_2:layout_11", 11, L"paragraph_title", L"1.1.1 利用格式工厂转换视频");
        addPage2Block(L"page_2:layout_16", 16, L"footer", L"中国科技期刊研究，2016，27（7）");

        const std::wstring page2Markdown =
            L"片、表格、视频等附加信息，但添加入附加图片的PDF格式全文却未包含视频资料。\n\n"
            L"利用最新的 Adobe Acrobat XI 可以很方便地在现有形态的 PDF 文档中插入视频（包括动画）以及音频文件，并可采用 Adobe Acrobat 和 Adobe Reader 流畅和清晰地播放。\n\n"
            L"### 1 视频转换、编辑与合并\n\n"
            L"### 1.1 利用格式工厂转换、编辑、合并视频\n\n"
            L"### 1.1.1 利用格式工厂转换视频";

        host.RenderMarkdownBlocks(8, page2Markdown, page2Blocks, page2Markdown);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var first=document.querySelector('[data-block-id="page_2:layout_3"]');
                  var second=document.querySelector('[data-block-id="page_2:layout_4"]');
                  var h1=document.querySelector('[data-block-id="page_2:layout_5"]');
                  var h2=document.querySelector('[data-block-id="page_2:layout_10"]');
                  var h3=document.querySelector('[data-block-id="page_2:layout_11"]');
                  var omitted=document.querySelector(
                    '[data-block-id="page_2:layout_1"],[data-block-id="page_2:layout_2"],[data-block-id="page_2:layout_16"]');
                  return !omitted&&first&&first.nodeName==="P"&&
                    first.textContent.indexOf("片、表格、视频")===0&&
                    second&&second.nodeName==="P"&&
                    second.textContent.indexOf("利用最新的 Adobe Acrobat XI")===0&&
                    h1&&h1.nodeName==="H3"&&h1.textContent.indexOf("1 视频转换")===0&&
                    h2&&h2.nodeName==="H3"&&h2.textContent.indexOf("1.1 利用格式工厂")===0&&
                    h3&&h3.nodeName==="H3"&&h3.textContent.indexOf("1.1.1 利用格式工厂")===0?1:0;
                })())JS",
                1)) {
            std::wstring mappingDetails;
            ExecuteScriptSync(
                host,
                LR"JS((function(){return Array.from(document.querySelectorAll('#preview>*,#preview .ocr-preview-linked-block')).map(function(node){return node.nodeName+':'+(node.getAttribute('data-block-id')||'')+':'+node.textContent.slice(0,24);}).join('|');})())JS",
                mappingDetails);
            runtimeError = L"Page 2 Preview mapping allowed omitted page decorations to shift body blocks: " + mappingDetails;
        }

        if (runtimeError.empty()) {
            host.SetSelectedBlock(L"page_2:layout_4", true);
            if (!WaitForScriptInt(
                    host,
                    LR"JS((function(){
                      var selected=document.querySelector('.ocr-preview-linked-block.is-selected');
                      return selected&&selected.getAttribute('data-block-id')==="page_2:layout_4"&&
                        selected.nodeName==="P"&&
                        selected.textContent.indexOf("利用最新的 Adobe Acrobat XI")===0?1:0;
                    })())JS",
                    1)) {
                runtimeError = L"Selecting Page 2 block 4 highlighted the wrong Preview paragraph.";
            }
        }

        if (runtimeError.empty()) {
            const int selectCount = previewSelectCount;
            std::wstring result;
            if (!ExecuteScriptSync(
                    host,
                    LR"JS((function(){
                      var first=document.querySelector('[data-block-id="page_2:layout_3"]');
                      if(first)first.click();
                      return first?1:0;
                    })())JS",
                    result) ||
                _wtoi(result.c_str()) != 1 ||
                !PumpUntil([&]() {
                    return previewSelectCount == selectCount + 1 &&
                        selectedPreviewBlockId == L"page_2:layout_3";
                }, 3000)) {
                runtimeError = L"Clicking the first Page 2 Preview paragraph returned the wrong block ID.";
            }
        }

        // Reproduce the reported Page 2 failure with its real block labels and
        // ordering. `paragraph_title` used to be treated as ordinary text, so
        // the range scorer preferred one heading plus the next five Markdown
        // nodes over the exact single heading. Selecting block 8 then painted
        // several Preview rows and left blocks 9/10 with no DOM node.
        if (runtimeError.empty()) {
            std::vector<OcrMarkdownPreviewHost::PreviewBlock> reportedPage2Blocks;
            auto addReportedPage2Block = [&](const wchar_t* id, int order, const wchar_t* label, const wchar_t* content) {
                OcrMarkdownPreviewHost::PreviewBlock block;
                block.id = id;
                block.pageIndex = 1;
                block.order = order;
                block.label = label;
                block.displayLabel = label;
                block.content = content;
                reportedPage2Blocks.push_back(std::move(block));
            };
            addReportedPage2Block(
                L"page_2:layout_8", 8, L"paragraph_title",
                L"(2) 对作者论文 PDF 文件中不必要的内容进行裁剪");
            addReportedPage2Block(
                L"page_2:layout_9", 9, L"text",
                L"上述生成的 PDF 文件中往往含有多余信息内容，如论文最后一页和第一页常插有与作者论文无关的补白或其他作者论文的转页等内容，需要对这些内容作裁剪。打开一个作者论文的 PDF 文件，把含有多余内容的页次定为当前页，选择文档到裁剪页面，弹出图3所示对话框。若多余内容在下部，则选择页边距控制中的下方按钮，按增大剪切量按钮，在右侧预览图中见剪切黑线上移，到合适位置时即可确定。");
            addReportedPage2Block(
                L"page_2:layout_10", 10, L"paragraph_title",
                L"(3) 对作者论文 PDF 文件中的所缺内容进行增补");
            addReportedPage2Block(
                L"page_2:layout_11", 11, L"text",
                L"初步生成的论文 PDF 文件中常会缺失横排的图表或论文最后内容发生转页造成论文内容不全。对于前者，可将缺失的图表转换成单独的 PDF 文件备用；对于后者，可先将转页内容制成单独的 PDF 文件，再对其进行裁剪加工以制成备用文件。打开作者论文 PDF 文件，选择文档到插入页面按钮，选取所要插入的备用 PDF 文件，选择正确的插入位置，确认后即完成增补。");
            addReportedPage2Block(
                L"page_2:layout_12", 12, L"text",
                L"经过上述操作，即获得了完整的作者论文 PDF 文件，关");
            addReportedPage2Block(
                L"page_2:layout_13", 13, L"figure_title",
                L"图 3 Adobe Acrobat 软件中文档到裁剪页面窗口");

            const std::wstring reportedPage2Markdown =
                L"### (2) 对作者论文 PDF 文件中不必要的内容进行裁剪\n\n"
                L"上述生成的 PDF 文件中往往含有多余信息内容，如论文最后一页和第一页常插有与作者论文无关的补白或其他作者论文的转页等内容，需要对这些内容作裁剪。打开一个作者论文的 PDF 文件，把含有多余内容的页次定为当前页，选择文档到裁剪页面，弹出图3所示对话框。若多余内容在下部，则选择页边距控制中的下方按钮，按增大剪切量按钮，在右侧预览图中见剪切黑线上移，到合适位置时即可确定。\n\n"
                L"### (3) 对作者论文 PDF 文件中的所缺内容进行增补\n\n"
                L"初步生成的论文 PDF 文件中常会缺失横排的图表或论文最后内容发生转页造成论文内容不全。对于前者，可将缺失的图表转换成单独的 PDF 文件备用；对于后者，可先将转页内容制成单独的 PDF 文件，再对其进行裁剪加工以制成备用文件。打开作者论文 PDF 文件，选择文档到插入页面按钮，选取所要插入的备用 PDF 文件，选择正确的插入位置，确认后即完成增补。\n\n"
                L"经过上述操作，即获得了完整的作者论文 PDF 文件，关\n\n"
                L"图 3 Adobe Acrobat 软件中文档到裁剪页面窗口";

            host.RenderMarkdownBlocks(
                9,
                reportedPage2Markdown,
                reportedPage2Blocks,
                reportedPage2Markdown);
            host.SetSelectedBlock(L"page_2:layout_8", true);
            if (!WaitForScriptInt(
                    host,
                    LR"JS((function(){
                      var eight=document.querySelectorAll('[data-block-id="page_2:layout_8"]');
                      var nine=document.querySelectorAll('[data-block-id="page_2:layout_9"]');
                      var ten=document.querySelectorAll('[data-block-id="page_2:layout_10"]');
                      var selected=document.querySelectorAll('.ocr-preview-linked-block.is-selected');
                      return eight.length===1&&eight[0].nodeName==="H3"&&
                        nine.length===1&&nine[0].nodeName==="P"&&
                        ten.length===1&&ten[0].nodeName==="H3"&&
                        selected.length===1&&selected[0]===eight[0]?1:0;
                    })())JS",
                    1)) {
                std::wstring mappingDetails;
                ExecuteScriptSync(
                    host,
                    LR"JS((function(){return Array.from(document.querySelectorAll('#preview>*')).map(function(node){return node.nodeName+':'+(node.getAttribute('data-block-id')||'')+':'+node.textContent.slice(0,20);}).join('|');})())JS",
                    mappingDetails);
                runtimeError = L"Reported Page 2 block 8 consumed rows belonging to blocks 9/10: " + mappingDetails;
            }
        }

        // Page 3 has the same title/body alternation and therefore exercises
        // the same invariant independently of any Page 2-specific text.
        if (runtimeError.empty()) {
            std::vector<OcrMarkdownPreviewHost::PreviewBlock> reportedPage3Blocks;
            auto addReportedPage3Block = [&](const wchar_t* id, int order, const wchar_t* label, const wchar_t* content) {
                OcrMarkdownPreviewHost::PreviewBlock block;
                block.id = id;
                block.pageIndex = 2;
                block.order = order;
                block.label = label;
                block.displayLabel = label;
                block.content = content;
                reportedPage3Blocks.push_back(std::move(block));
            };
            addReportedPage3Block(L"page_3:layout_4", 4, L"paragraph_title", L"(2) 用清晰图像替换论文中的不清晰图像");
            addReportedPage3Block(L"page_3:layout_5", 5, L"text", L"这里所说的替换，实质上是用清晰图像去覆盖原来的不清晰图像。为了尽量减小作者论文 PDF 文件所占的空间，建议以不含图件的作者论文 PDF 文件为底本，在此基础上进行覆盖工作。首先打开文件，将要贴图的页次定为当前页，选择添加水印和背景，调节垂直对齐及水平对齐按钮，将所贴图像准确放入空白位置。");
            addReportedPage3Block(L"page_3:layout_7", 7, L"paragraph_title", L"4 结语");
            addReportedPage3Block(L"page_3:layout_8", 8, L"text", L"有了方正软件排版的原始完整文件和图件文件，编辑部制作期刊作者论文的 PDF 文件不是一件困难复杂的事情，也不费太多时间。有条件的编辑部可以尝试着做这一工作。此外还可以利用添加水印、签名及图章等功能标明版权信息。");
            addReportedPage3Block(L"page_3:layout_9", 9, L"paragraph_title", L"参考文献");
            addReportedPage3Block(L"page_3:layout_10", 10, L"reference_content", L"1 郑楼先. 采用方正文易制作期刊电子版的方法及要领.");

            const std::wstring reportedPage3Markdown =
                L"### (2) 用清晰图像替换论文中的不清晰图像\n\n"
                L"这里所说的替换，实质上是用清晰图像去覆盖原来的不清晰图像。为了尽量减小作者论文 PDF 文件所占的空间，建议以不含图件的作者论文 PDF 文件为底本，在此基础上进行覆盖工作。首先打开文件，将要贴图的页次定为当前页，选择添加水印和背景，调节垂直对齐及水平对齐按钮，将所贴图像准确放入空白位置。\n\n"
                L"### 4 结语\n\n"
                L"有了方正软件排版的原始完整文件和图件文件，编辑部制作期刊作者论文的 PDF 文件不是一件困难复杂的事情，也不费太多时间。有条件的编辑部可以尝试着做这一工作。此外还可以利用添加水印、签名及图章等功能标明版权信息。\n\n"
                L"### 参考文献\n\n"
                L"1 郑楼先. 采用方正文易制作期刊电子版的方法及要领.";

            host.RenderMarkdownBlocks(
                10,
                reportedPage3Markdown,
                reportedPage3Blocks,
                reportedPage3Markdown);
            if (!WaitForScriptInt(
                    host,
                    LR"JS((function(){
                      var four=document.querySelectorAll('[data-block-id="page_3:layout_4"]');
                      var five=document.querySelectorAll('[data-block-id="page_3:layout_5"]');
                      var seven=document.querySelectorAll('[data-block-id="page_3:layout_7"]');
                      var nine=document.querySelectorAll('[data-block-id="page_3:layout_9"]');
                      return four.length===1&&four[0].nodeName==="H3"&&
                        five.length===1&&five[0].nodeName==="P"&&
                        seven.length===1&&seven[0].nodeName==="H3"&&
                        nine.length===1&&nine[0].nodeName==="H3"?1:0;
                    })())JS",
                    1)) {
                runtimeError = L"Reported Page 3 title/body blocks did not retain one-to-one Preview mapping.";
            }
        }

        if (runtimeError.empty() && !renderFixture()) {
            runtimeError = L"Preview did not rerender after the Page 2 mapping regression.";
        }
    }

    if (runtimeError.empty()) {
        std::wstring result;
        if (!ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var block=document.querySelector('[data-block-id="page_1:title"]');
                  if(!block)return 0;
                  block.dispatchEvent(new MouseEvent("mouseenter"));
                  block.click();
                  var toolbar=document.querySelector('.ocr-preview-floating-toolbar');
                  return block.classList.contains('is-hovered')&&block.classList.contains('is-selected')&&
                    toolbar&&toolbar.style.display!=="none"?1:0;
                })())JS",
                result) ||
            _wtoi(result.c_str()) != 1 ||
            !PumpUntil([&]() {
                return previewHoverCount >= 1 && hoveredPreviewBlockId == L"page_1:title" &&
                    previewSelectCount >= 1 && selectedPreviewBlockId == L"page_1:title";
            }, 3000)) {
            runtimeError = L"Preview block hover/selection did not reach both DOM and host state.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        if (!ExecuteScriptSync(
                host,
                LR"JS((function(){
                  window.dispatchEvent(new KeyboardEvent("keydown",{key:"Escape",bubbles:true}));
                  var toolbar=document.querySelector('.ocr-preview-floating-toolbar');
                  return !document.querySelector('.ocr-preview-linked-block.is-selected, .ocr-preview-linked-block.is-hovered')&&
                    (!toolbar||toolbar.style.display==="none")?1:0;
                })())JS",
                result) ||
            _wtoi(result.c_str()) != 1 ||
            !PumpUntil([&]() {
                return previewHoverCount >= 2 && hoveredPreviewBlockId.empty() &&
                    previewSelectCount >= 2 && selectedPreviewBlockId.empty();
            }, 3000)) {
            runtimeError = L"Escape did not clear Preview DOM, toolbar, and host hover/selection state.";
        }
    }

    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:title");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var restore=Array.from(document.querySelectorAll(".ocr-preview-inline-editor-toolbar button"))
                    .find(function(button){return button.textContent==="Restore OCR";});
                  if(restore)restore.click();
                  return restore?1:0;
                })())JS",
                1) ||
            !PumpUntil([&]() { return previewRestoreCount == 1; }, 3000) ||
            restoredBlockId != L"page_1:title" ||
            restoredSourceEdit.expectedSource != L"# Editable title" ||
            restoredSourceEdit.revisionSha256.size() != 64) {
            runtimeError = L"Restore OCR control did not send the strict current source range to Host.";
        }
    }
    if (runtimeError.empty() && !renderFixture()) {
        runtimeError = L"Preview did not rerender after the Restore OCR contract.";
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:title");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-rich-editor-body")?1:0;})())JS",
                1)) {
            runtimeError = L"Heading rich-text editor did not open.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        if (!ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var select=document.querySelector(".ocr-preview-editor-select");
                  var body=document.querySelector(".ocr-preview-rich-editor-body");
                  select.value="3";
                  select.dispatchEvent(new Event("change",{bubbles:true}));
                  return body&&body.querySelector("h3")?1:0;
                })())JS",
                result) ||
            _wtoi(result.c_str()) != 1) {
            runtimeError = L"Heading selector did not apply H3 in the visible editor.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var buttons=Array.from(document.querySelectorAll(".ocr-preview-inline-editor-toolbar button"));
              var save=buttons.find(function(button){return button.textContent==="Save";});
              if(save)save.click();
              return save?1:0;
            })())JS",
            result);
        if (!PumpUntil([&]() { return previewSaveCount >= 1; }, 3000) ||
            savedBlockId != L"page_1:title" ||
            savedContent.find(L"### Editable title") == std::wstring::npos ||
            savedOriginalContent.find(L"# Editable title") == std::wstring::npos) {
            runtimeError = L"Heading edit did not save H3 Markdown with its original source segment.";
        }
    }

    if (runtimeError.empty() && !renderFixture()) {
        runtimeError = L"Preview did not rerender after heading edit.";
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:formula");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-formula-editor")?1:0;})())JS",
                1)) {
            runtimeError = L"Formula editor did not open.";
        }
    }
    if (runtimeError.empty()) {
        saveCountBeforeInvalidFormula = previewSaveCount;
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var source=document.querySelector(".ocr-preview-formula-source");
              source.value="\\bad{";
              source.dispatchEvent(new Event("input",{bubbles:true}));
              source.dispatchEvent(new KeyboardEvent("keydown",{key:"s",ctrlKey:true,bubbles:true}));
              return 1;
            })())JS",
            result);
        PumpFor(100);
        if (previewSaveCount != saveCountBeforeInvalidFormula) {
            runtimeError = L"Immediate Ctrl+S bypassed synchronous LaTeX validation.";
        }
    }
    if (runtimeError.empty()) {
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var save=document.querySelector(".ocr-preview-inline-editor-toolbar .ocr-preview-editor-button.is-primary");
                  return save&&save.disabled?1:0;
                })())JS",
                1)) {
            runtimeError = L"Invalid LaTeX did not disable Save.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var source=document.querySelector(".ocr-preview-formula-source");
              source.value="y^2";
              source.dispatchEvent(new Event("input",{bubbles:true}));
              return 1;
            })())JS",
            result);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var save=document.querySelector(".ocr-preview-inline-editor-toolbar .ocr-preview-editor-button.is-primary");
                  return save&&!save.disabled?1:0;
                })())JS",
                1)) {
            runtimeError = L"Valid LaTeX did not enable Save.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var save=document.querySelector(".ocr-preview-inline-editor-toolbar .ocr-preview-editor-button.is-primary");
              if(save)save.click();
              return save?1:0;
            })())JS",
            result);
        if (!PumpUntil([&]() { return previewSaveCount >= 2; }, 3000) ||
            savedBlockId != L"page_1:formula" ||
            savedContent.find(L"y^2") == std::wstring::npos ||
            savedOriginalContent.find(L"\\tag{2.1}") == std::wstring::npos) {
            runtimeError = L"Formula editor did not save validated LaTeX source.";
        }
    }

    if (runtimeError.empty() && !renderFixture()) {
        runtimeError = L"Preview did not rerender before the table test.";
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:table");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-table-grid-scroll table")?1:0;})())JS",
                1)) {
            runtimeError = L"Table grid editor did not open.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var add=Array.from(document.querySelectorAll(".ocr-preview-table-grid-tools button"))
                .find(function(button){return button.title==="Insert row";});
              if(add)add.click();
              return document.querySelectorAll(".ocr-preview-table-grid-scroll table tr").length;
            })())JS",
            result);
        if (_wtoi(result.c_str()) != 3) {
            runtimeError = L"Table grid row insertion failed.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var cells=document.querySelectorAll(".ocr-preview-table-grid-scroll table tr:first-child th");
              cells[0].click();
              cells[1].dispatchEvent(new MouseEvent("click",{bubbles:true,shiftKey:true}));
              var merge=Array.from(document.querySelectorAll(".ocr-preview-table-grid-tools button"))
                .find(function(button){return button.title==="Merge selected cells";});
              if(merge)merge.click();
              return cells[0].colSpan;
            })())JS",
            result);
        if (_wtoi(result.c_str()) != 2) {
            runtimeError = L"Table grid cell merge failed.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var save=document.querySelector(".ocr-preview-inline-editor-toolbar .ocr-preview-editor-button.is-primary");
              if(save)save.click();
              return save?1:0;
            })())JS",
            result);
        if (!PumpUntil([&]() { return previewSaveCount >= 3; }, 3000) ||
            savedBlockId != L"page_1:table" ||
            savedContent.find(L"<table") == std::wstring::npos) {
            runtimeError = L"Table grid did not serialize merged cells as HTML.";
        }
    }

    if (runtimeError.empty() && !renderFixture()) {
        runtimeError = L"Preview did not rerender before the image test.";
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:image");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var image=document.querySelector(".ocr-preview-image-editor-preview");
                  return image&&image.src?1:0;
                })())JS",
                1)) {
            runtimeError = L"Empty image block did not map to the visible Preview image.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var fields=document.querySelectorAll(".ocr-preview-image-editor-fields input");
              var caption=document.querySelector(".ocr-preview-image-caption-input");
              fields[0].value="Updated diagram";
              fields[0].dispatchEvent(new Event("input",{bubbles:true}));
              caption.value="Edited caption";
              caption.dispatchEvent(new Event("input",{bubbles:true}));
              var save=document.querySelector(".ocr-preview-inline-editor-toolbar .ocr-preview-editor-button.is-primary");
              if(save)save.click();
              return save?1:0;
            })())JS",
            result);
        if (_wtoi(result.c_str()) != 1 ||
            !PumpUntil([&]() { return previewSaveCount >= 4; }, 3000) ||
            savedBlockId != L"page_1:image" ||
            savedContent.find(L"Updated diagram") == std::wstring::npos ||
            savedContent.find(L"Edited caption") == std::wstring::npos ||
            savedContent.find(L"width=\"42%\"") == std::wstring::npos ||
            savedContent.find(L"text-align: center") == std::wstring::npos ||
            savedContent.find(L"title=\"#7 image\"") != std::wstring::npos ||
            savedOriginalContent.find(L"<img") == std::wstring::npos ||
            savedSourceEdit.sourceEnd <= savedSourceEdit.sourceStart ||
            savedSourceEdit.revisionSha256.size() != 64) {
            runtimeError = L"Image editor did not save alt/caption against the visible source image. count=" +
                std::to_wstring(previewSaveCount) + L", id=" + savedBlockId + L", content=" +
                savedContent + L", original=" + savedOriginalContent + L", range=" +
                std::to_wstring(savedSourceEdit.sourceStart) + L".." +
                std::to_wstring(savedSourceEdit.sourceEnd) + L", script=" + result;
        }
    }

    if (runtimeError.empty() && !renderFixture()) {
        runtimeError = L"Preview did not rerender before the nested-list test.";
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:list");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-rich-editor-body ol")?1:0;})())JS",
                1)) {
            runtimeError = L"Nested ordered-list editor did not open.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var save=document.querySelector(".ocr-preview-inline-editor-toolbar .ocr-preview-editor-button.is-primary");
              if(save)save.click();
              return save?1:0;
            })())JS",
            result);
        if (!PumpUntil([&]() { return previewSaveCount >= 5; }, 3000) ||
            savedBlockId != L"page_1:list" ||
            savedContent.find(L"3. Parent") == std::wstring::npos ||
            savedContent.find(L"   - Child") == std::wstring::npos ||
            savedOriginalContent.find(L"3. Parent") == std::wstring::npos ||
            savedContent != savedOriginalContent ||
            savedOriginalContent.find(L"flowchart TD") != std::wstring::npos) {
            runtimeError = L"Nested-list round trip or diagram-adjacent source mapping failed.";
        }
    }

    if (runtimeError.empty() && !renderFixture()) {
        runtimeError = L"Preview did not rerender before the stable-ID alias test.";
    }
    if (runtimeError.empty()) {
        host.SetSelectedBlock(L"page_1:formula_number", false);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var formula=document.querySelector('[data-block-id="page_1:formula"]');
                  return formula&&formula.classList.contains("is-selected")?1:0;
                })())JS",
                1)) {
            runtimeError = L"A formula-number block ID did not resolve to its merged formula Preview node.";
        }
        host.SetSelectedBlock(L"", false);
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:multi_paragraph");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var body=document.querySelector(".ocr-preview-rich-editor-body");
                  var originals=Array.from(document.querySelectorAll('[data-block-id="page_1:multi_paragraph"]'))
                    .filter(function(node){return !node.classList.contains("ocr-preview-inline-editor");});
                  return body&&body.querySelectorAll("p").length===2&&
                    !document.querySelector(".ocr-preview-formula-source")&&
                    originals.length===2&&originals.every(function(node){return node.style.display==="none";})?1:0;
                })())JS",
                1)) {
            runtimeError = L"One layout block spanning two Preview paragraphs did not open one rich-text editor or hide both linked nodes.";
        }
        host.SetEditingBlock(L"");
    }

    // Regression fixture from the Local VL 1.6 two-column paper sample.  In
    // the real result, formula numbers are separate layout blocks while the
    // Markdown renderer folds them into the preceding display formula.  The
    // visible formula and the source opened by its editor must still resolve
    // to the same block; a following paragraph must never be used as formula
    // editor input.
    if (runtimeError.empty()) {
        std::vector<OcrMarkdownPreviewHost::PreviewBlock> paperBlocks;
        auto addPaperBlock = [&](const wchar_t* id, int order, const wchar_t* label, const wchar_t* content) {
            OcrMarkdownPreviewHost::PreviewBlock block;
            block.id = id;
            block.order = order;
            block.label = label;
            block.displayLabel = label;
            block.content = content;
            paperBlocks.push_back(std::move(block));
        };
        addPaperBlock(
            L"page_1:layout_12", 181, L"text",
            LR"MD(The \(\mathbf{c}^{I}(\mathbf{R})\) satisfy the secular problem)MD");
        addPaperBlock(
            L"page_1:layout_13", 191, L"display_formula",
            LR"MD(\[[\mathbf{H}(\mathbf{R})-\mathbf{I}E_{I}(\mathbf{R})]\mathbf{c}^{I}(\mathbf{R})=\mathbf{0}\])MD");
        addPaperBlock(L"page_1:layout_14", 205, L"formula_number", L"(2.5)");
        addPaperBlock(L"page_1:layout_15", 214, L"image", L"");
        addPaperBlock(
            L"page_1:layout_16", 216, L"figure_title",
            L"FIG. 2. Conical intersections of two-dimensional potential-energy surfaces for states (1,2) and (2,3) in a region of nuclear coordinate space indicated by the shaded oval.");
        addPaperBlock(
            L"page_1:layout_17", 224, L"text",
            LR"MD(continuity with respect to \(\mathbf{R}\). This is a key issue in the theory of conical intersections and is the essential idea behind the geometric phase effect. To include the possibility of a geometry-dependent phase factor, we define, following Mead and Truhlar (1979), a gauge transformation)MD");
        addPaperBlock(
            L"page_1:layout_18", 230, L"display_formula",
            LR"MD(\[\tilde{\Psi}_{I}(\mathbf{r};\mathbf{R})\equiv e^{i A_{I}(\mathbf{R})}\Psi_{I}(\mathbf{r};\mathbf{R}),\])MD");
        addPaperBlock(L"page_1:layout_19", 232, L"formula_number", L"(2.4b)");
        addPaperBlock(
            L"page_1:layout_20", 234, L"text",
            LR"MD(where the \(A_1(\mathbf{R})\) are chosen to make \(\tilde{\Psi}_1(\mathbf{r};\mathbf{R})\) single-valued.)MD");
        addPaperBlock(L"page_1:layout_21", 294, L"display_formula", LR"MD(\[N^{a}\])MD");

        const std::wstring paperMarkdown =
            LR"MD(The \(\mathbf{c}^{I}(\mathbf{R})\) satisfy the secular problem

$$
[\mathbf{H}(\mathbf{R})-\mathbf{I}E_{I}(\mathbf{R})]\mathbf{c}^{I}(\mathbf{R})=\mathbf{0} \tag{2.5}
$$

<div style="text-align: center;"><img src="https://zencrop-ocr-images.invalid/preview%20test.png" alt="Image" width="42%" /></div>

FIG. 2. Conical intersections of two-dimensional potential-energy surfaces for states (1,2) and (2,3) in a region of nuclear coordinate space indicated by the shaded oval.

continuity with respect to \(\mathbf{R}\). This is a key issue in the theory of conical intersections and is the essential idea behind the geometric phase effect. To include the possibility of a geometry-dependent phase factor, we define, following Mead and Truhlar (1979), a gauge transformation

$$
\tilde{\Psi}_{I}(\mathbf{r};\mathbf{R})\equiv e^{i A_{I}(\mathbf{R})}\Psi_{I}(\mathbf{r};\mathbf{R}), \tag{2.4b}
$$

where the \(A_1(\mathbf{R})\) are chosen to make \(\tilde{\Psi}_1(\mathbf{r};\mathbf{R})\) single-valued.

$$
N^{a}
$$)MD";

        host.RenderMarkdownBlocks(8, paperMarkdown, paperBlocks, paperMarkdown);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelectorAll(".ocr-preview-linked-block[data-block-id]").length;})())JS",
                8)) {
            runtimeError = L"Two-column paper blocks were not mapped to all visible Preview nodes.";
        }
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:layout_13");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){
                  var source=document.querySelector(".ocr-preview-formula-source");
                  return source&&source.value.indexOf("\\mathbf{H}")>=0&&
                    source.value.indexOf("continuity with respect")<0?1:0;
                })())JS",
                1)) {
            std::wstring mappingDetails;
            ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var editor=document.querySelector(".ocr-preview-formula-source");
                  var original=document.querySelector('[data-block-id="page_1:layout_13"]:not(.ocr-preview-inline-editor)');
                  return "source="+(editor?editor.value:"")+"; node="+
                    (original?original.nodeName+":"+original.textContent:"missing");
                })())JS",
                mappingDetails);
            runtimeError = L"Formula (2.5) editor was bound to a following paragraph source segment: " + mappingDetails;
        }
    }

    // Regression fixture from the official Cloud response: parsing_res_list
    // is in source order, but image/caption have null block_order and the next
    // text/formula use smaller numbered content-only orders. The caption is a
    // top-level HTML div. Display metadata must not reorder source matching,
    // and safe HTML containers must remain interactive.
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"");
        std::vector<OcrMarkdownPreviewHost::PreviewBlock> cloudBlocks;
        auto addCloudBlock = [&](const wchar_t* id, int order, const wchar_t* label, const wchar_t* content) {
            OcrMarkdownPreviewHost::PreviewBlock block;
            block.id = id;
            block.order = order;
            block.label = label;
            block.displayLabel = label;
            block.content = content;
            cloudBlocks.push_back(std::move(block));
        };
        addCloudBlock(L"page_1:cloud_formula_before", 13, L"display_formula", L"$$ Hx=0 $$");
        addCloudBlock(L"page_1:cloud_formula_number", 14, L"formula_number", L"(2.5)");
        addCloudBlock(L"page_1:cloud_image", 17, L"image", L"");
        addCloudBlock(L"page_1:cloud_caption", 18, L"figure_title", L"FIG. 2. Cloud HTML caption.");
        addCloudBlock(L"page_1:cloud_continuation", 15, L"text", L"Continuation after the cloud figure.");
        addCloudBlock(L"page_1:cloud_formula_after", 16, L"display_formula", L"$$ Ny=1 $$");

        const std::wstring cloudMarkdown =
            LR"MD($$ Hx=0 \tag{2.5} $$

<div style="text-align: center;"><img src="https://zencrop-ocr-images.invalid/cloud%20figure.png" alt="Cloud figure" width="41%" /></div>

<div style="text-align: center;">FIG. 2. Cloud HTML caption.</div>

Continuation after the cloud figure.

$$ Ny=1 $$)MD";

        host.RenderMarkdownBlocks(80, cloudMarkdown, cloudBlocks, cloudMarkdown);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelectorAll(".ocr-preview-linked-block[data-block-id]").length;})())JS",
                5)) {
            std::wstring mappingDetails;
            ExecuteScriptSync(
                host,
                LR"JS((function(){return Array.from(document.querySelectorAll('.ocr-preview-linked-block[data-block-id]')).map(function(node){return node.getAttribute('data-block-id')+':'+node.nodeName;}).join('|');})())JS",
                mappingDetails);
            runtimeError = L"Cloud mixed-order image/caption blocks were not mapped in source order: " + mappingDetails;
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        if (!ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var image=document.querySelector('[data-block-id="page_1:cloud_image"]');
                  var caption=document.querySelector('[data-block-id="page_1:cloud_caption"]');
                  var continuation=document.querySelector('[data-block-id="page_1:cloud_continuation"]');
                  var before=document.querySelector('[data-block-id="page_1:cloud_formula_before"]');
                  var after=document.querySelector('[data-block-id="page_1:cloud_formula_after"]');
                  return image&&image.nodeName==="IMG"&&caption&&caption.nodeName==="DIV"&&
                    continuation&&continuation.nodeName==="P"&&before&&after&&
                    (before.getAttribute("data-linked-block-ids")||"").indexOf("page_1:cloud_formula_number")>=0?1:0;
                })())JS",
                result) ||
            _wtoi(result.c_str()) != 1) {
            runtimeError = L"Cloud image, HTML caption, continuation, or formula-number alias mapped to the wrong DOM node.";
        }
    }
    if (runtimeError.empty()) {
        int selectCount = previewSelectCount;
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){var node=document.querySelector('[data-block-id="page_1:cloud_image"]');if(node)node.click();return node?1:0;})())JS",
            result);
        if (_wtoi(result.c_str()) != 1 ||
            !PumpUntil([&]() {
                return previewSelectCount > selectCount && selectedPreviewBlockId == L"page_1:cloud_image";
            }, 3000)) {
            runtimeError = L"Cloud image block did not support click selection.";
        }
    }
    if (runtimeError.empty()) {
        int selectCount = previewSelectCount;
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){var node=document.querySelector('[data-block-id="page_1:cloud_caption"]');if(node)node.click();return node?1:0;})())JS",
            result);
        if (_wtoi(result.c_str()) != 1 ||
            !PumpUntil([&]() {
                return previewSelectCount > selectCount && selectedPreviewBlockId == L"page_1:cloud_caption";
            }, 3000)) {
            runtimeError = L"Cloud HTML caption block did not support click selection.";
        }
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:cloud_image");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-image-editor-layout")?1:0;})())JS",
                1)) {
            runtimeError = L"Cloud image block did not open the image editor.";
        }
        host.SetEditingBlock(L"");
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"page_1:cloud_caption");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){var editor=document.querySelector(".ocr-preview-rich-editor-body");return editor&&editor.textContent.indexOf("Cloud HTML caption")>=0?1:0;})())JS",
                1)) {
            runtimeError = L"Cloud HTML caption block did not open the text editor with its source.";
        }
        host.SetEditingBlock(L"");
    }

    // No-op rich-text saves must retain the exact escaped source spelling.
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"");
        OcrMarkdownPreviewHost::PreviewBlock literalBlock;
        literalBlock.id = L"preview-edit:literal";
        literalBlock.order = 1;
        literalBlock.label = L"text";
        literalBlock.displayLabel = L"Text";
        literalBlock.content = L"*literal stars* and # marker";
        const std::wstring literalMarkdown = L"\\*literal stars\\* and \\# marker";
        host.RenderMarkdownBlocks(9, literalMarkdown, {literalBlock}, literalMarkdown);
        host.SetSelectedBlock(literalBlock.id, false);
        host.SetEditingBlock(literalBlock.id);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-rich-editor-body")?1:0;})())JS",
                1)) {
            runtimeError = L"Escaped-literal editor did not open.";
        } else {
            int saveCount = previewSaveCount;
            std::wstring result;
            ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var editor=document.querySelector(".ocr-preview-inline-editor");
                  var save=Array.from(document.querySelectorAll(".ocr-preview-inline-editor-toolbar button"))
                    .find(function(button){return button.textContent==="Save";});
                  if(!editor||!save)return 0;
                  if(save.disabled)return 2;
                  save.click();
                  return editor.getAttribute("data-block-id")==="preview-edit:literal"?1:3;
                })())JS",
                result);
            if (_wtoi(result.c_str()) != 1 ||
                !PumpUntil([&]() { return previewSaveCount == saveCount + 1; }, 3000) ||
                savedContent != literalMarkdown || savedOriginalContent != literalMarkdown) {
                runtimeError = L"No-op rich-text save changed escaped Markdown source.";
            }
        }
    }

    // A failed ACK must keep the user's draft in the editor without mutating
    // the backing block. Cancel + reopen must show the original source again.
    if (runtimeError.empty()) {
        OcrMarkdownPreviewHost::PreviewBlock literalBlock;
        literalBlock.id = L"preview-edit:literal";
        literalBlock.order = 1;
        literalBlock.label = L"text";
        literalBlock.displayLabel = L"Text";
        literalBlock.content = L"*literal stars* and # marker";
        const std::wstring literalMarkdown = L"\\*literal stars\\* and \\# marker";
        host.RenderMarkdownBlocks(10, literalMarkdown, {literalBlock}, literalMarkdown);
        host.SetSelectedBlock(literalBlock.id, false);
        host.SetEditingBlock(literalBlock.id);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-rich-editor-body")?1:0;})())JS",
                1)) {
            runtimeError = L"Save-failure editor did not open.";
        } else {
            rejectNextPreviewSave = true;
            int saveCount = previewSaveCount;
            std::wstring result;
            ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var body=document.querySelector(".ocr-preview-rich-editor-body");
                  body.textContent="unsaved replacement";
                  body.dispatchEvent(new Event("input",{bubbles:true}));
                  var save=Array.from(document.querySelectorAll(".ocr-preview-inline-editor-toolbar button"))
                    .find(function(button){return button.textContent==="Save";});
                  if(save)save.click();
                  return save?1:0;
                })())JS",
                result);
            if (_wtoi(result.c_str()) != 1 ||
                !PumpUntil([&]() { return previewSaveCount == saveCount + 1; }, 3000) ||
                !WaitForScriptInt(
                    host,
                    LR"JS((function(){var s=document.querySelector(".ocr-preview-editor-status");return s&&s.textContent.indexOf("could not be saved")>=0?1:0;})())JS",
                    1)) {
                runtimeError = L"Save NACK did not keep the draft editor open with feedback.";
            }
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var cancel=Array.from(document.querySelectorAll(".ocr-preview-inline-editor-toolbar button"))
                .find(function(button){return button.textContent==="Cancel";});
              if(cancel)cancel.click();
              return cancel?1:0;
            })())JS",
            result);
        host.SetEditingBlock(L"preview-edit:literal");
        if (_wtoi(result.c_str()) != 1 || !WaitForScriptInt(
                host,
                LR"JS((function(){
                  var body=document.querySelector(".ocr-preview-rich-editor-body");
                  return body&&body.textContent.indexOf("literal stars")>=0&&
                    body.textContent.indexOf("unsaved replacement")<0?1:0;
                })())JS",
                1)) {
            runtimeError = L"Failed-save Cancel left phantom block content behind.";
        }
    }
    if (runtimeError.empty()) {
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              var editor=document.querySelector(".ocr-preview-rich-editor-body");
              editor.dispatchEvent(new KeyboardEvent("keydown",{key:"Escape",bubbles:true}));
              var selected=document.querySelector('[data-block-id="preview-edit:literal"]');
              return selected&&selected.classList.contains("is-selected")?1:0;
            })())JS",
            result);
        if (_wtoi(result.c_str()) != 1) {
            runtimeError = L"Editor Escape incorrectly cleared Preview selection.";
        }
    }
    if (runtimeError.empty()) {
        host.SetEditingBlock(L"");
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-rich-editor-body")?0:1;})())JS",
                1)) {
            runtimeError = L"Escaped-literal editor did not close before the next document render.";
        }
    }

    // Grid serialization preserves alignment and the widest row, even when
    // source rows are uneven.
    if (runtimeError.empty()) {
        OcrMarkdownPreviewHost::PreviewBlock tableBlock;
        tableBlock.id = L"preview-edit:aligned-table";
        tableBlock.order = 1;
        tableBlock.label = L"table";
        tableBlock.displayLabel = L"Table";
        const std::wstring tableMarkdown =
            L"| Left | Center | Right |\n"
            L"| :--- | :---: | ---: |\n"
            L"| 1 | 2 | 3 | extra |";
        tableBlock.content = tableMarkdown;
        host.RenderMarkdownBlocks(11, tableMarkdown, {tableBlock}, tableMarkdown);
        if (!WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector('[data-block-id="preview-edit:aligned-table"]')?1:0;})())JS",
                1)) {
            runtimeError = L"Aligned uneven table did not render before editing.";
        } else {
            host.SetEditingBlock(tableBlock.id);
        }
        if (runtimeError.empty() && !WaitForScriptInt(
                host,
                LR"JS((function(){return document.querySelector(".ocr-preview-table-grid-scroll table")?1:0;})())JS",
                1)) {
            runtimeError = L"Aligned uneven table editor did not open.";
        } else {
            int saveCount = previewSaveCount;
            std::wstring result;
            ExecuteScriptSync(
                host,
                LR"JS((function(){
                  var cell=document.querySelector(".ocr-preview-table-grid-scroll th");
                  cell.textContent="Changed";
                  cell.dispatchEvent(new Event("input",{bubbles:true}));
                  var editor=cell.closest(".ocr-preview-inline-editor");
                  var save=editor&&editor.querySelector(".ocr-preview-inline-editor-toolbar .is-primary");
                  if(save)save.click();
                  return save?1:0;
                })())JS",
                result);
            if (_wtoi(result.c_str()) != 1 ||
                !PumpUntil([&]() {
                    return previewSaveCount >= saveCount + 1 && savedBlockId == tableBlock.id;
                }, 3000) ||
                savedContent.find(L"| :--- | :---: | ---: | --- |") == std::wstring::npos ||
                savedContent.find(L"| 1 | 2 | 3 | extra |") == std::wstring::npos) {
                runtimeError = L"Table Grid save lost alignment or uneven trailing cells; id=" + savedBlockId +
                    L"; source=" + savedOriginalContent + L"; content=" + savedContent +
                    L"; clickResult=" + result;
            }
        }
    }

    // A stale render token must be rejected before reaching the dashboard.
    if (runtimeError.empty()) {
        int saveCount = previewSaveCount;
        std::wstring result;
        ExecuteScriptSync(
            host,
            LR"JS((function(){
              window.chrome.webview.postMessage({
                type:"previewBlockSave",id:"preview-edit:aligned-table",renderToken:"stale",
                content:"stale",canonicalSource:"markdown-body-lf",offsetUnit:"utf16-code-unit",
                sourceStart:0,sourceEnd:1,revisionSha256:"stale",expectedSource:"x"
              });
              return 1;
            })())JS",
            result);
        PumpFor(100);
        if (previewSaveCount != saveCount) {
            runtimeError = L"Host accepted a save from a stale render token.";
        }
    }

    PumpFor(100);

    host.Destroy();
    RemoveMappingFixture(mappingRootA);
    RemoveMappingFixture(mappingRootB);
    RemoveDirectoryW(mappingFixtureBase.c_str());
    DestroyWindow(hwnd);
    CoUninitialize();

    if (renderError) {
        std::wcerr << L"Preview render error: " << renderErrorMessage << L"\n";
        return 1;
    }
    if (!runtimeError.empty()) {
        std::wcerr << L"Preview WYSIWYG runtime contract failed: " << runtimeError << L"\n";
        return 1;
    }

    std::wcout << L"WebView2 preview contract passed.\n";
    return 0;
}
