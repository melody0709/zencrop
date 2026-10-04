#include "translation/TranslationResultWindow.h"
#include "core/Settings.h"
#include "core/Strings.h"

#include <WebView2.h>
#include <ole2.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>

namespace {
using namespace translation;

bool PumpUntil(const std::function<bool()> &condition, DWORD timeout) {
    const auto deadline = GetTickCount64() + timeout;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (condition())
            return true;
        Sleep(2);
    } while (GetTickCount64() < deadline);
    return condition();
}

struct OleSession {
    HRESULT result = OleInitialize(nullptr);
    ~OleSession() {
        if (SUCCEEDED(result))
            OleUninitialize();
    }
};

double Milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

bool PreviewVisible(HWND window) {
    const HWND source = GetDlgItem(window, 3101);
    const HWND translation = GetDlgItem(window, 3102);
    return IsWindow(window) && IsWindowVisible(window) && !IsIconic(window) && IsWindow(source) &&
           IsWindow(translation) && !IsWindowVisible(source) && !IsWindowVisible(translation);
}

nlohmann::json ProcessMemory(nlohmann::json &errors) {
    std::vector<PROCESSENTRY32W> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry{sizeof(entry)};
    if (snapshot == INVALID_HANDLE_VALUE) {
        errors.push_back({{"api", "CreateToolhelp32Snapshot"}, {"win32_error", GetLastError()}});
        return nlohmann::json::array();
    }
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.push_back(entry);
        } while (Process32NextW(snapshot, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES)
            errors.push_back({{"api", "Process32NextW"}, {"win32_error", GetLastError()}});
    } else {
        errors.push_back({{"api", "Process32FirstW"}, {"win32_error", GetLastError()}});
    }
    CloseHandle(snapshot);
    std::vector<DWORD> descendants{GetCurrentProcessId()};
    for (size_t i = 0; i < descendants.size(); ++i) {
        for (const auto &process : processes) {
            if (process.th32ParentProcessID == descendants[i] &&
                std::find(descendants.begin(), descendants.end(), process.th32ProcessID) == descendants.end()) {
                descendants.push_back(process.th32ProcessID);
            }
        }
    }
    nlohmann::json result = nlohmann::json::array();
    for (const auto &process : processes) {
        if (_wcsicmp(process.szExeFile, L"msedgewebview2.exe") ||
            std::find(descendants.begin(), descendants.end(), process.th32ProcessID) == descendants.end())
            continue;
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, process.th32ProcessID);
        PROCESS_MEMORY_COUNTERS_EX2 memory{};
        if (handle &&
            GetProcessMemoryInfo(handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
            result.push_back({{"pid", process.th32ProcessID},
                              {"parent_pid", process.th32ParentProcessID},
                              {"working_set_bytes", memory.WorkingSetSize},
                              {"private_working_set_bytes", memory.PrivateWorkingSetSize},
                              {"private_bytes", memory.PrivateUsage}});
        } else {
            errors.push_back({{"api", handle ? "GetProcessMemoryInfo" : "OpenProcess"},
                              {"pid", process.th32ProcessID},
                              {"win32_error", GetLastError()}});
        }
        if (handle)
            CloseHandle(handle);
    }
    return result;
}
} // namespace

int MeasureTranslationWindowLifecycle() {
    OleSession ole;
    if (FAILED(ole.result))
        return 1;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    S::SetLanguage(false);
    const TranslationSettings saved = LoadTranslationSettings();
    TranslationSettings settings;
    settings.showSourceText = true;
    SaveTranslationSettings(settings);
    const std::vector<std::pair<std::string, std::wstring>> samples = {
        {"plain", L"Lifecycle sample.\n\nA second paragraph with ordinary text."},
        {"table", L"<table><tr><th colspan=\"2\">Merged heading</th></tr><tr><td "
                  L"rowspan=\"2\">A</td><td>B</td></tr><tr><td>C</td></tr></table>"},
        {"diagram_math", L"```mermaid\ngraph TD\nA[Start] --> B[Finish]\n```\n\n$$x^2+y^2=z^2$$"},
        {"long", std::wstring(12000, L'x') + L"\n\nEnd of long document."}};
    nlohmann::json report;
    report["memory_errors"] = nlohmann::json::array();
    report["notes"] = "Controller cold/reopen, not OS cache cold. Preview visible means both native fallbacks are "
                      "hidden after metrics. No network or external selection acquisition. Memory covers this test's "
                      "descendant WebView2 processes; no dashboard.";
    LPWSTR version = nullptr;
    if (SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) && version) {
        report["runtime"] = std::filesystem::path(version).string();
        CoTaskMemFree(version);
    }
    int result = 0;
    for (const auto &[name, text] : samples) {
        std::unique_ptr<TranslationResultWindow> window;
        for (int cycle = 0; cycle < 35; ++cycle) {
            // Five separately-created windows, then thirty close/reopen cycles.
            if (cycle < 5)
                window.reset();
            const bool reused = window && window->IsValid();
            const auto start = std::chrono::steady_clock::now();
            TranslationLaunchContext context{TranslationSourceMode::SelectedText, RECT{60, 60, 180, 90}};
            if (!reused)
                window = std::make_unique<TranslationResultWindow>(TranslationRequest{}, context,
                                                                   [](TranslationResultWindow::Command) {});
            else
                window->PrepareForReuse(context.anchorRect);
            window->SetShowSourceText(true);
            window->SetBusy(false);
            // Alternate text to force a fresh render and prevent a stale-ready measurement.
            const std::wstring content = text + std::to_wstring(cycle % 2);
            window->SetSourceText(content);
            window->SetTranslationText(content);
            window->Show(nullptr);
            const double nativeMs = Milliseconds(start);
            const HWND hwnd = window->WindowHandle();
            const bool previewVisible = PumpUntil([&] { return PreviewVisible(hwnd); }, 15000);
            report["samples"].push_back({{"content", name},
                                         {"cycle", cycle},
                                         {"phase", cycle < 5 ? "created" : "reopen"},
                                         {"reused", reused},
                                         {"native_visible_ms", nativeMs},
                                         {"preview_visible_ms", Milliseconds(start)},
                                         {"preview_verified", previewVisible}});
            if (!previewVisible) {
                wchar_t mode[80]{};
                GetWindowTextW(GetDlgItem(hwnd, 3120), mode, ARRAYSIZE(mode));
                report["failure_mode"] = std::filesystem::path(mode).string();
                report["failure_memory"] = ProcessMemory(report["memory_errors"]);
                result = 2;
                break;
            }
            if (cycle == 34)
                report["active_memory"][name] = ProcessMemory(report["memory_errors"]);
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            if (cycle == 34) {
                PumpUntil([] { return false; }, 1000);
                report["hidden_1s_memory"][name] = ProcessMemory(report["memory_errors"]);
            }
        }
        window.reset();
        if (result)
            break;
    }
    wchar_t idleCheck[2]{};
    if (!result && GetEnvironmentVariableW(L"ZENCROP_TRANSLATION_LIFECYCLE_IDLE", idleCheck, ARRAYSIZE(idleCheck))) {
        int closed = 0;
        TranslationLaunchContext context{TranslationSourceMode::SelectedText, RECT{60, 60, 180, 90}};
        TranslationResultWindow window(TranslationRequest{}, context, [&](TranslationResultWindow::Command command) {
            if (command == TranslationResultWindow::Command::Close)
                ++closed;
        });
        window.SetBusy(false);
        window.SetSourceText(L"Actual fifteen-minute eviction check");
        window.SetTranslationText(L"Eviction result");
        window.Show(nullptr);
        HWND hwnd = window.WindowHandle();
        if (!PumpUntil([&] { return PreviewVisible(hwnd); }, 15000)) {
            result = 4;
        } else {
            constexpr DWORD idleTimeoutMs = 15 * 60 * 1000;
            const auto start = std::chrono::steady_clock::now();
            const ULONGLONG startTick = GetTickCount64();
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            SendMessageW(hwnd, WM_TIMER, 5, 0);
            report["idle_early_timer_retained"] = window.IsValid();
            std::cout << "idle eviction: real fifteen-minute wait started\n" << std::flush;
            const bool evicted = PumpUntil([&] { return !window.IsValid(); }, idleTimeoutMs + 5000);
            const double evictionMs = Milliseconds(start);
            const ULONGLONG evictionTicks = GetTickCount64() - startTick;
            report["idle_eviction_ms"] = evictionMs;
            report["idle_eviction_tick_ms"] = evictionTicks;
            report["idle_evicted"] = evicted;
            report["idle_close_notifications"] = closed;
            // Use the same clock as the retention deadline for the lower bound;
            // different clock resolutions must not make a correct timer fail.
            if (!evicted || evictionTicks < idleTimeoutMs || closed != 1 ||
                !report["idle_early_timer_retained"].get<bool>())
                result = 5;
            PumpUntil([] { return false; }, 1000);
            report["idle_evicted_1s_memory"] = ProcessMemory(report["memory_errors"]);
        }
    }
    PumpUntil([] { return false; }, 1000);
    report["released_1s_memory"] = ProcessMemory(report["memory_errors"]);
    if (!report["memory_errors"].empty()) {
        std::cerr << "lifecycle memory collection failed: " << report["memory_errors"].dump() << '\n';
        if (!result)
            result = 6;
    }
    SaveTranslationSettings(saved);
    wchar_t label[32]{};
    GetEnvironmentVariableW(L"ZENCROP_TRANSLATION_LIFECYCLE_MEASURE", label, ARRAYSIZE(label));
    wchar_t outputRoot[32768]{};
    if (!GetEnvironmentVariableW(L"ZENCROP_TEST_OUTPUT_ROOT", outputRoot, ARRAYSIZE(outputRoot)))
        return 3;
    const std::filesystem::path output = std::filesystem::path(outputRoot).parent_path() / "diagnostics" /
                                         (L"translation-window-lifecycle-" + std::wstring(label) + L".json");
    std::filesystem::create_directories(output.parent_path());
    std::ofstream file(output);
    file << report.dump(2) << '\n';
    std::cout << "lifecycle measurement samples=" << report["samples"].size() << " result=" << result << '\n';
    return file ? result : 3;
}

int TestTranslationWindowLifecycleContract() {
    if (PreviewVisible(nullptr))
        return 616;
    // A thread without COM forces synchronous environment creation failure.
    // A retained native fallback must retry creation on its next launch.
    int failedHostResult = 0;
    {
        std::jthread worker([&] {
            const TranslationLaunchContext context{TranslationSourceMode::SelectedText, RECT{60, 60, 180, 90}};
            TranslationResultWindow window(TranslationRequest{}, context, [](TranslationResultWindow::Command) {});
            if (!window.IsValid()) {
                failedHostResult = 614;
                return;
            }
            SendMessageW(window.WindowHandle(), WM_CLOSE, 0, 0);
            if (!window.IsValid() || !window.IsClosed() || window.CanReuse())
                failedHostResult = 615;
        });
    }
    if (failedHostResult)
        return failedHostResult;
    OleSession ole;
    if (FAILED(ole.result))
        return 601;
    S::SetLanguage(false);
    int closes = 0, cancels = 0;
    const TranslationLaunchContext context{TranslationSourceMode::SelectedText, RECT{60, 60, 180, 90}};
    {
        TranslationResultWindow window(TranslationRequest{}, context, [&](TranslationResultWindow::Command command) {
            if (command == TranslationResultWindow::Command::Close)
                ++closes;
            if (command == TranslationResultWindow::Command::Cancel)
                ++cancels;
        });
        const HWND hwnd = window.WindowHandle();
        if (!window.IsValid())
            return 602;
        window.Show(nullptr);
        window.SetBusy(true);
        SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        if (cancels != 1 || closes || !IsWindowVisible(hwnd))
            return 603;
        window.SetBusy(false);
        window.SetSourceText(L"First session");
        window.SetTranslationText(L"Old translation");
        // Close while asynchronous controller/page creation can still be in flight.
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        if (!window.IsClosed() || !window.IsValid() || IsWindowVisible(hwnd) || closes != 1)
            return 604;
        // Early timer and late unscoped error must neither evict nor rewrite the UI.
        SendMessageW(hwnd, WM_TIMER, 5, 0);
        TranslationResultWindow::PostAsyncError(hwnd, L"stale error", false);
        PumpUntil([] { return false; }, 250);
        if (!window.IsValid() || IsWindowVisible(hwnd) || !window.SourceText().empty())
            return 605;
        if (!window.CanReuse())
            return 606;
        RECT newAnchor{500, 240, 610, 260};
        window.PrepareForReuse(newAnchor);
        window.SetSourceText(L"New session");
        window.SetTranslationText(L"New translation");
        window.Show(nullptr);
        SendMessageW(hwnd, WM_TIMER, 5, 0); // A canceled old timer may already be queued.
        if (!IsWindowVisible(hwnd) || window.IsClosed() || !window.IsValid())
            return 607;
        if (window.SourceText() != L"New session")
            return 608;
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(3109, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hwnd, 3109)));
        if (closes != 2 || !window.IsClosed() || IsWindowVisible(hwnd))
            return 609;
        window.PrepareForReuse(context.anchorRect);
        window.Show(nullptr);
        SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        if (closes != 3 || !window.IsClosed())
            return 610;
        S::SetLanguage(true);
        if (window.CanReuse())
            return 611;
        S::SetLanguage(false);
    }
    if (closes != 3)
        return 612; // Destructor must not re-notify a retained close.
    {
        const TranslationLaunchContext ocrContext{TranslationSourceMode::OcrImage, context.anchorRect};
        TranslationResultWindow window(TranslationRequest{}, ocrContext, [](TranslationResultWindow::Command) {});
        HWND hwnd = window.WindowHandle();
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        if (IsWindow(hwnd) || window.IsValid())
            return 613;
    }
    std::cout
        << "translation lifecycle: repeated close, early/stale timer, late error, ESC, language, OCR teardown passed\n";
    return 0;
}
