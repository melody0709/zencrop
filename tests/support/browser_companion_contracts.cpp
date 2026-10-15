#include "detect/BrowserDomDetector.h"
#include "detect/SmartDetectorThread.h"
#include "net/BrowserCompanionBridge.h"
#include "net/BrowserCompanionWebSocket.h"
#include "core/BrowserCompanionPairing.h"
#include "core/BrowserCompanionRegistration.h"
#include "core/WideFormatLabels.h"
#include "core/SettingsFileIo.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <shellapi.h>
#include <dwmapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace browser_companion;
using namespace std::chrono_literals;
int RunIsolatedCompanionRegistrationContracts();

struct Pipe {
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    Pipe() { CreatePipe(&read, &write, nullptr, 4096); }
    ~Pipe() {
        if (read)
            CloseHandle(read);
        if (write)
            CloseHandle(write);
    }
    void CloseWriter() {
        if (write)
            CloseHandle(write);
        write = nullptr;
    }
};

Json Hello() {
    return {{"version", kProtocolVersion}, {"type", "hello"}, {"connectionId", "profile-one"}};
}

Json ResultMessage() {
    return {{"version", kProtocolVersion},
            {"type", "result"},
            {"sessionId", "session-one"},
            {"requestId", "1"},
            {"connectionId", "profile-one"},
            {"windowId", 1},
            {"tabId", 2},
            {"frameId", 0},
            {"documentId", "document-one"},
            {"geometryVersion", 1},
            {"scale", 1.5},
            {"candidates", Json::array({Json::array({0, 0, 100, 50})})}};
}

std::array<char, 4> Header(uint32_t length) {
    std::array<char, 4> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>((length >> (8 * i)) & 255);
    return bytes;
}

bool WriteBytes(HANDLE pipe, std::span<const char> bytes) {
    DWORD count = 0;
    return WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size();
}

Json RectJson(const RECT &rect) {
    return Json::array({rect.left, rect.top, rect.right, rect.bottom});
}

bool NearRect(const RECT &rect, const RECT &expected, LONG tolerance) {
    return tolerance >= 0 && tolerance <= 2 && std::abs(rect.left - expected.left) <= tolerance &&
           std::abs(rect.top - expected.top) <= tolerance && std::abs(rect.right - expected.right) <= tolerance &&
           std::abs(rect.bottom - expected.bottom) <= tolerance;
}

int RunRuntimeProbe(const std::filesystem::path &inputPath) {
    const auto oldDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct RestoreDpi {
        DPI_AWARENESS_CONTEXT previous;
        ~RestoreDpi() { SetThreadDpiAwarenessContext(previous); }
    } restoreDpi{oldDpi};
    int failures = 0;
    Json report = {{"results", Json::array()}};
    const auto expect = [&](bool passed, const char *name) {
        if (!passed) {
            ++failures;
            std::cerr << "FAIL companion runtime " << name << '\n';
        }
    };
    try {
        std::ifstream input(inputPath);
        const auto fixture = Json::parse(input);
        HWND window =
            reinterpret_cast<HWND>(static_cast<uintptr_t>(std::stoull(fixture.at("hwnd").get<std::string>())));
        wchar_t title[512]{};
        GetWindowTextW(window, title, static_cast<int>(std::size(title)));
        if (!IsWindow(window) ||
            std::wstring_view(title).find(L"ZenCrop companion public fixture") == std::wstring_view::npos) {
            throw std::runtime_error("probe target is not the public fixture");
        }
        const POINT point{fixture.at("point").at(0).get<LONG>(), fixture.at("point").at(1).get<LONG>()};
        const RECT expected{fixture.at("expected").at(0).get<LONG>(), fixture.at("expected").at(1).get<LONG>(),
                            fixture.at("expected").at(2).get<LONG>(), fixture.at("expected").at(3).get<LONG>()};
        const LONG tolerance = fixture.value("tolerance", 1L);
        const auto matchesExpected = [&](const RECT &rect) { return NearRect(rect, expected, tolerance); };
        report["hwnd"] = fixture.at("hwnd");
        report["point"] = fixture.at("point");
        report["expected"] = RectJson(expected);
        auto &bridge = Bridge::Instance();
        if (!bridge.Start())
            throw std::runtime_error("bridge start failed");
        if (fixture.value("direct", false)) {
            const auto readyBy = std::chrono::steady_clock::now() + 10s;
            while (std::chrono::steady_clock::now() < readyBy) {
                const auto status = bridge.QueryStatus();
                if (!status.error.empty())
                    throw std::runtime_error("direct listener or pairing store unavailable");
                if (status.readyChrome + status.readyEdge > 0)
                    break;
                std::this_thread::sleep_for(10ms);
            }
            const auto status = bridge.QueryStatus();
            if (status.readyChrome + status.readyEdge == 0)
                throw std::runtime_error("fixture did not authenticate");
        }
        std::mutex mutex;
        std::condition_variable cv;
        std::optional<SmartDetectorThread::Result> received;
        SmartDetectorThread detector;
        detector.OnResultReady = [&](SmartDetectorThread::Result result) {
            {
                std::lock_guard lock(mutex);
                received = std::move(result);
            }
            cv.notify_one();
        };
        const auto started = std::chrono::steady_clock::now();
        const auto deadline =
            started + (fixture.value("leaseLoss", false) || fixture.contains("continuousLease") ? 40s : 5s);
        detector.Start(fixture.value("frozen", true));
        uint64_t lastRequest = 0;
        const auto wait = [&](const char *operation, const auto &send) -> std::optional<SmartDetectorThread::Result> {
            {
                std::lock_guard lock(mutex);
                received.reset();
            }
            const auto queryStart = std::chrono::steady_clock::now();
            send();
            std::unique_lock lock(mutex);
            if (!cv.wait_until(lock, deadline, [&] { return received && received->request > lastRequest; }))
                return std::nullopt;
            const auto result = received;
            lastRequest = result->request;
            report["results"].push_back(
                {{"operation", operation},
                 {"rect", RectJson(result->isNavigation ? result->navigationRect : result->rect)},
                 {"document", result->browserDocument},
                 {"status", result->browserStatus},
                 {"generation", result->generation},
                 {"request", result->request},
                 {"targetHwnd", std::to_string(reinterpret_cast<uintptr_t>(result->targetHwnd))},
                 {"windowRect", RectJson(result->windowRect)},
                 {"elapsedMs",
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - queryStart).count()}});
            return result;
        };
        std::optional<SmartDetectorThread::Result> hover;
        do {
            hover = wait("hover", [&] { detector.AsyncGetRectByPoint(point, nullptr); });
            if (!hover || !hover->browserDocument.empty())
                break;
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < deadline);
        expect(hover && hover->targetHwnd == window, "public fixture is the selected window");
        expect(hover && hover->browser && !hover->browserDocument.empty(), "DOM hover connected");
        if (hover && !hover->browserDocument.empty()) {
            expect(matchesExpected(hover->rect), "precise leaf rectangle");
            const auto parent =
                wait("parent", [&] { detector.AsyncNavigateParent(point, window, nullptr, hover->windowRect); });
            expect(parent && parent->navigationSuccess && parent->navigationRect.left <= hover->rect.left &&
                       parent->navigationRect.top <= hover->rect.top &&
                       parent->navigationRect.right >= hover->rect.right &&
                       parent->navigationRect.bottom >= hover->rect.bottom &&
                       !EqualRect(&parent->navigationRect, &hover->rect),
                   "parent contains leaf and is larger");
            const auto retained =
                wait("hover_after_parent", [&] { detector.AsyncGetRectByPoint(POINT{point.x + 1, point.y}, nullptr); });
            expect(parent && retained && retained->rectSuccess && EqualRect(&parent->navigationRect, &retained->rect),
                   "hover preserves wheel-selected parent");
            if (fixture.contains("navigationTarget") && parent) {
                const auto &target = fixture.at("navigationTarget").at("point");
                const POINT inside{target.at(0).get<LONG>(), target.at(1).get<LONG>()};
                expect(PtInRect(&parent->navigationRect, inside), "different leaf is inside the locked parent");
                const auto locked =
                    wait("hover_within_locked_parent", [&] { detector.AsyncGetRectByPoint(inside, nullptr); });
                expect(locked && locked->rectSuccess && EqualRect(&locked->rect, &parent->navigationRect),
                       "ordinary wheel-selected parent stays locked across child elements");
                const auto back =
                    wait("hover_back_within_parent", [&] { detector.AsyncGetRectByPoint(point, nullptr); });
                expect(back && back->rectSuccess && EqualRect(&back->rect, &parent->navigationRect),
                       "return within ordinary parent keeps the wheel selection");
            }
            const auto child =
                wait("child", [&] { detector.AsyncNavigateChild(point, window, nullptr, hover->windowRect); });
            expect(child && child->navigationSuccess && matchesExpected(child->navigationRect), "child restores leaf");
            for (int sample = 0; sample < 5; ++sample) {
                const auto warm = wait("warm_hover", [&] { detector.AsyncGetRectByPoint(point, nullptr); });
                expect(warm && warm->rectSuccess && matchesExpected(warm->rect), "warm hover keeps precise geometry");
            }
            if (fixture.contains("browserChromePoint")) {
                const auto &chromePoint = fixture.at("browserChromePoint");
                const POINT chrome{chromePoint.at(0).get<LONG>(), chromePoint.at(1).get<LONG>()};
                const auto miss = wait("hover_browser_chrome", [&] { detector.AsyncGetRectByPoint(chrome, nullptr); });
                expect(miss && miss->targetHwnd == window && miss->browserDocument.empty() &&
                           miss->browserStatus == "outside_viewport" && EqualRect(&miss->rect, &hover->windowRect),
                       "browser chrome produces a temporary whole-window fallback");
                expect(bridge.IsValid(hover->browserSession, hover->browserDocument),
                       "browser chrome keeps the captured document valid");
                const auto returned =
                    wait("hover_after_browser_chrome", [&] { detector.AsyncGetRectByPoint(point, nullptr); });
                expect(returned && returned->rectSuccess && matchesExpected(returned->rect) &&
                           returned->browserSession == hover->browserSession &&
                           returned->browserDocument == hover->browserDocument,
                       "small element recovers from whole-window fallback without leaving browser");
            }
            if (fixture.contains("navigationTarget")) {
                const auto &target = fixture.at("navigationTarget");
                const POINT targetPoint{target.at("point").at(0).get<LONG>(), target.at("point").at(1).get<LONG>()};
                const auto readRect = [](const Json &rect) {
                    return RECT{rect.at(0).get<LONG>(), rect.at(1).get<LONG>(), rect.at(2).get<LONG>(),
                                rect.at(3).get<LONG>()};
                };
                const RECT targetRect = readRect(target.at("expected"));
                const RECT pageRect = readRect(target.at("page"));
                RECT largest = hover->rect;
                bool atLargest = false;
                for (size_t step = 0; step < kMaxCandidates; ++step) {
                    const auto expanded = wait("expand_to_page", [&] {
                        detector.AsyncNavigateParent(point, window, nullptr, hover->windowRect);
                    });
                    if (!expanded || !expanded->navigationSuccess || expanded->browserDocument.empty())
                        break;
                    if (EqualRect(&largest, &expanded->navigationRect)) {
                        atLargest = true;
                        break;
                    }
                    largest = expanded->navigationRect;
                }
                expect(atLargest && NearRect(largest, pageRect, tolerance), "wheel reaches largest page candidate");
                const auto moved = wait("hover_new_leaf", [&] { detector.AsyncGetRectByPoint(targetPoint, nullptr); });
                expect(moved && moved->rectSuccess && NearRect(moved->rect, targetRect, tolerance),
                       "new leaf releases page-sized wheel selection without leaving browser");
                const auto childAtTarget = wait("child_at_new_leaf", [&] {
                    detector.AsyncNavigateChild(targetPoint, window, nullptr, hover->windowRect);
                });
                expect(childAtTarget && childAtTarget->navigationSuccess &&
                           NearRect(childAtTarget->navigationRect, targetRect, tolerance),
                       "wheel at new leaf starts from its own candidate path");
                const auto returned =
                    wait("hover_original_leaf", [&] { detector.AsyncGetRectByPoint(point, nullptr); });
                expect(returned && returned->rectSuccess && matchesExpected(returned->rect),
                       "return to original leaf stays precise after page navigation");
            }
            if (fixture.value("liveWindowMove", false)) {
                RECT original{};
                expect(GetWindowRect(window, &original) != FALSE, "live fixture bounds captured");
                struct RestorePosition {
                    HWND window;
                    RECT rect;
                    ~RestorePosition() {
                        SetWindowPos(window, nullptr, rect.left, rect.top, 0, 0,
                                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                } restore{window, original};
                expect(SetWindowPos(window, nullptr, original.left + 30, original.top + 20, 0, 0,
                                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                       "live fixture moved");
                RECT movedExpected = expected;
                OffsetRect(&movedExpected, 30, 20);
                RECT movedBounds{};
                expect(SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &movedBounds, sizeof(movedBounds))),
                       "live fixture physical frame measured");
                const RECT screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                                  GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                                  GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
                RECT movedWindow{};
                IntersectRect(&movedWindow, &movedBounds, &screen);
                --movedWindow.right;
                --movedWindow.bottom;
                const POINT movedPoint{point.x + 30, point.y + 20};
                bool updated = false;
                while (std::chrono::steady_clock::now() < deadline) {
                    const auto moved =
                        wait("live_after_move", [&] { detector.AsyncGetRectByPoint(movedPoint, nullptr); });
                    if (moved && !moved->browserDocument.empty() && NearRect(moved->rect, movedExpected, tolerance) &&
                        EqualRect(&moved->windowRect, &movedWindow)) {
                        updated = true;
                        break;
                    }
                    std::unique_lock lock(mutex);
                    cv.wait_for(lock, 10ms);
                }
                expect(updated, "live mode refreshes both DOM and whole-window fallback coordinates");
            }
            if (fixture.contains("continuousLease")) {
                const auto &p = fixture.at("continuousLease").at("point");
                const POINT childPoint{p.at(0).get<LONG>(), p.at(1).get<LONG>()};
                const auto childHit = wait("lease_child", [&] { detector.AsyncGetRectByPoint(childPoint, nullptr); });
                expect(childHit && !childHit->browserDocument.empty(), "iframe entered before continuous hover");
                const auto until = std::chrono::steady_clock::now() + 35s;
                while (std::chrono::steady_clock::now() < until) {
                    const auto live = wait("lease_main_hover", [&] { detector.AsyncGetRectByPoint(point, nullptr); });
                    if (!live || live->browserDocument.empty()) {
                        expect(false, "visited iframe remains leased during continuous main-document hover");
                        break;
                    }
                    std::unique_lock lock(mutex);
                    cv.wait_for(lock, 1s, [&] { return received && received->browserDocument.empty(); });
                }
            }
            if (fixture.value("invalidate", false)) {
                {
                    std::lock_guard lock(mutex);
                    received.reset();
                }
                std::unique_lock lock(mutex);
                const bool invalidated = cv.wait_until(lock, deadline, [&] {
                    return received && received->browserDocument.empty() &&
                           received->browserStatus == "geometry_invalidated";
                });
                expect(invalidated && !detector.IsCurrent(*hover), "mutation invalidates stationary selection");
                expect(!invalidated || (received->isHoverRect && !received->isNavigation),
                       "invalidation is a hover fallback rather than a wheel command");
                report["invalidated"] = invalidated;
            }
            const auto endStart = std::chrono::steady_clock::now();
            detector.EndSession();
            report["endMs"] =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - endStart).count();
            expect(report["endMs"].get<double>() <= 250, "end within 250 ms");
            expect(!detector.IsCurrent(*hover), "ended hover rejected");
            if (parent)
                expect(!detector.IsCurrent(*parent), "ended parent rejected");
            if (child)
                expect(!detector.IsCurrent(*child), "ended child rejected");
        }
        const auto stopStart = std::chrono::steady_clock::now();
        detector.Stop();
        report["stopMs"] =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stopStart).count();
        expect(report["stopMs"].get<double>() <= 250, "stop within 250 ms");
        bridge.Stop();
        report["totalMs"] =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    } catch (const std::exception &error) {
        ++failures;
        report["error"] = error.what();
        std::cerr << "FAIL companion runtime " << error.what() << '\n';
        Bridge::Instance().Stop();
    }
    report["failures"] = failures;
    std::filesystem::path resultPath(inputPath.native() + L".result.json");
    std::ofstream output(resultPath);
    output << report.dump(2) << '\n';
    if (!output) {
        ++failures;
        std::cerr << "FAIL companion runtime result write\n";
    }
    return failures;
}
}

int RunBrowserCompanionHostIfRequested() {
    int count = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv)
        return -1;
    std::vector<std::wstring> args(argv, argv + count);
    LocalFree(argv);
    if (args.size() == 2 && args[1] == L"--test-companion-registration")
        return RunIsolatedCompanionRegistrationContracts();
    return browser_companion::IsNativeInvocation(args) ? browser_companion::RunNativeHost(args) : -1;
}

namespace {
int RunIsolatedCompanionRegistrationContracts() {
    using zencrop::BrowserCompanionHostState;
    int failures = 0;
    const auto check = [&](bool passed, const char* name) {
        if (!passed) { ++failures; std::cerr << "FAIL companion registry " << name << '\n'; }
    };
    const std::wstring rootPath = L"Software\\ZenCrop\\Tests\\Companion-" + std::to_wstring(GetCurrentProcessId());
    struct RegistryIsolation {
        std::wstring path;
        HKEY root = nullptr, user = nullptr, machine = nullptr;
        bool ownsRoot = false, userOverridden = false, machineOverridden = false;
        ~RegistryIsolation() {
            if (userOverridden) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            if (machineOverridden) RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
            if (user) RegCloseKey(user);
            if (machine) RegCloseKey(machine);
            if (root) RegCloseKey(root);
            if (ownsRoot) RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        }
    } isolation{rootPath};
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, rootPath.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
            nullptr, &isolation.root, &disposition) != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY) {
        check(false, "unique registry fixture"); return failures;
    }
    isolation.ownsRoot = true;
    if (RegCreateKeyExW(isolation.root, L"User", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &isolation.user, nullptr) ||
        RegCreateKeyExW(isolation.root, L"Machine", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &isolation.machine, nullptr)) {
        check(false, "registry fixture roots"); return failures;
    }
    isolation.userOverridden = RegOverridePredefKey(HKEY_CURRENT_USER, isolation.user) == ERROR_SUCCESS;
    isolation.machineOverridden = RegOverridePredefKey(HKEY_LOCAL_MACHINE, isolation.machine) == ERROR_SUCCESS;
    if (!isolation.userOverridden || !isolation.machineOverridden) {
        check(false, "registry isolation enabled before testing"); return failures;
    }

    wchar_t outputRoot[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"ZENCROP_TEST_OUTPUT_ROOT", outputRoot, 32768);
    if (!length || length >= 32768) { check(false, "test output root"); return failures; }
    const auto directory = std::filesystem::path(outputRoot) /
        (L"companion-registration-" + std::to_wstring(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(directory)) { check(false, "unique file fixture"); return failures; }
    struct Files {
        std::filesystem::path directory;
        ~Files() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } files{directory};
    const auto dataDirectory = directory / L"data";
    SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", dataDirectory.c_str());
    const auto statePath = dataDirectory / L"browser-companion-registration.json";
    const auto runtime = [&](const wchar_t* name) {
        const auto path = directory / name;
        std::filesystem::create_directories(path / L"native-messaging");
        std::ofstream(path / L"ZenCrop.exe") << "fixture";
        const auto manifest = path / L"native-messaging/com.zencrop.browser_companion.json";
        std::ofstream(manifest) << R"({"name":"com.zencrop.browser_companion","path":"../ZenCrop.exe","type":"stdio","allowed_origins":["chrome-extension://halonieocgkgbdogndcipmlpklljmoja/"]})";
        return manifest.wstring();
    };
    const auto manifest = runtime(L"runtime one \u4e2d\u6587");
    const auto moved = runtime(L"runtime two");
    constexpr const wchar_t* chrome = L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.zencrop.browser_companion";
    constexpr const wchar_t* edge = L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.zencrop.browser_companion";
    constexpr const wchar_t* chromium = L"Software\\Chromium\\NativeMessagingHosts\\com.zencrop.browser_companion";
    const auto set = [&](HKEY root, const wchar_t* path, const std::wstring& value,
                         DWORD type = REG_SZ, REGSAM view = KEY_WOW64_64KEY) {
        HKEY key = nullptr;
        const auto opened = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_ALL_ACCESS | view,
            nullptr, &key, nullptr);
        check(opened == ERROR_SUCCESS, "open fixture host key");
        if (opened != ERROR_SUCCESS) return;
        check(RegSetValueExW(key, nullptr, 0, type, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS, "set fixture host value");
        RegCloseKey(key);
    };
    const auto read = [&](HKEY root, const wchar_t* path, const wchar_t* name = nullptr) {
        wchar_t value[32768]{};
        DWORD size = sizeof(value);
        RegGetValueW(root, path, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND | RRF_SUBKEY_WOW6464KEY,
            nullptr, value, &size);
        return std::wstring(value);
    };
    const auto erase = [&](const wchar_t* path, REGSAM view = KEY_WOW64_64KEY) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE | view, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, nullptr); RegCloseKey(key);
        }
    };
    zencrop::BrowserCompanionBridgeInfo query;
    check(zencrop::QueryBrowserCompanionBridge().edge == BrowserCompanionHostState::NotRegistered, "empty roots");
    set(HKEY_LOCAL_MACHINE, chrome, manifest);
    query = zencrop::QueryBrowserCompanionBridge();
    check(query.chrome == BrowserCompanionHostState::PathMismatch && query.edge == BrowserCompanionHostState::PathMismatch,
        "Edge uses machine Chrome fallback");
    set(HKEY_CURRENT_USER, chromium, L"C:\\absent-companion-test.json");
    check(zencrop::QueryBrowserCompanionBridge().edge == BrowserCompanionHostState::TargetMissing,
        "user Chromium fallback precedes machine Chrome");
    erase(chromium);
    set(HKEY_CURRENT_USER, chrome, L"");
    query = zencrop::QueryBrowserCompanionBridge();
    check(query.chrome == BrowserCompanionHostState::TargetMissing && query.edge == BrowserCompanionHostState::TargetMissing,
        "empty user value blocks machine fallback");
    check(!zencrop::RegisterBrowserCompanionBridge(manifest), "unowned empty value is refused");
    erase(chrome);
    set(HKEY_CURRENT_USER, edge, L"C:\\absent-companion-test.json");
    check(zencrop::QueryBrowserCompanionBridge().edge == BrowserCompanionHostState::TargetMissing,
        "Edge-specific value blocks Chrome fallback");
    check(!zencrop::RegisterBrowserCompanionBridge(manifest) && read(HKEY_CURRENT_USER, chrome).empty() &&
        !std::filesystem::exists(statePath), "unknown second key prevents all changes");
    erase(edge);

    check(zencrop::RegisterBrowserCompanionBridge(manifest).has_value(), "first registration");
    check(read(HKEY_CURRENT_USER, chrome) == manifest && read(HKEY_CURRENT_USER, edge) == manifest &&
        read(HKEY_LOCAL_MACHINE, chrome) == manifest, "both user values written; machine value preserved");
    const auto savedText = ReadFileToString(statePath.wstring());
    const auto saved = Json::parse(savedText);
    check(saved.at("records").size() == 2 && saved["records"][0]["existed"] == true &&
        saved["records"][0]["previous"].is_null(), "script-compatible original ownership recorded");
    HKEY sentinelKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, chrome, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &sentinelKey) == ERROR_SUCCESS) {
        const wchar_t sentinel[] = L"leave me alone";
        RegSetValueExW(sentinelKey, L"unrelated", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(sentinel), sizeof(sentinel));
        RegCloseKey(sentinelKey);
    }
    check(zencrop::RegisterBrowserCompanionBridge(manifest).has_value(), "repeat registration");
    check(zencrop::RegisterBrowserCompanionBridge(moved).has_value(), "owned portable move");
    check(read(HKEY_CURRENT_USER, chrome) == moved && read(HKEY_CURRENT_USER, edge) == moved &&
        read(HKEY_CURRENT_USER, chrome, L"unrelated") == L"leave me alone", "move preserves unrelated data");
    const auto afterMove = Json::parse(ReadFileToString(statePath.wstring()));
    check(afterMove["records"][0]["previous"].is_null() && afterMove["records"][0]["existed"] == true,
        "repeat and move retain original undo state");

    // A durable record can precede one or both registry writes if the process is interrupted.
    set(HKEY_CURRENT_USER, chrome, manifest);
    check(zencrop::RegisterBrowserCompanionBridge(moved).has_value(), "interrupted move resumes from priorManifest");
    std::filesystem::create_directory(statePath.wstring() + L".tmp");
    check(!zencrop::RegisterBrowserCompanionBridge(manifest) && read(HKEY_CURRENT_USER, chrome) == moved &&
        read(HKEY_CURRENT_USER, edge) == moved, "record-write failure leaves both keys untouched");
    std::filesystem::remove(statePath.wstring() + L".tmp");

    // Deny writes only to the private Edge fixture: a second-write failure must restore Chrome.
    set(HKEY_CURRENT_USER, chrome, moved, REG_EXPAND_SZ);
    HKEY protectedKey = nullptr;
    PSECURITY_DESCRIPTOR oldSecurity = nullptr, deniedSecurity = nullptr;
    PACL oldAcl = nullptr, deniedAcl = nullptr;
    bool present = false;
    BOOL aclPresent = FALSE, aclDefaulted = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, edge, 0, READ_CONTROL | WRITE_DAC | KEY_WOW64_64KEY, &protectedKey) == ERROR_SUCCESS &&
        GetSecurityInfo(protectedKey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl,
            nullptr, &oldSecurity) == ERROR_SUCCESS &&
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x2;;;WD)(A;;KA;;;WD)", SDDL_REVISION_1,
            &deniedSecurity, nullptr) && GetSecurityDescriptorDacl(deniedSecurity, &aclPresent, &deniedAcl, &aclDefaulted)) {
        present = SetSecurityInfo(protectedKey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, deniedAcl, nullptr) == ERROR_SUCCESS;
    }
    check(present, "write-denial fixture");
    if (present) {
        const auto beforeFailure = ReadFileToString(statePath.wstring());
        check(!zencrop::RegisterBrowserCompanionBridge(manifest) && read(HKEY_CURRENT_USER, chrome) == moved &&
            read(HKEY_CURRENT_USER, edge) == moved && ReadFileToString(statePath.wstring()) == beforeFailure,
            "second-key failure restores first value and ownership record");
        DWORD type = 0, size = 0;
        RegGetValueW(HKEY_CURRENT_USER, chrome, nullptr, RRF_RT_ANY | RRF_NOEXPAND | RRF_SUBKEY_WOW6464KEY,
            &type, nullptr, &size);
        check(type == REG_EXPAND_SZ, "rollback preserves value type");
        SetSecurityInfo(protectedKey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, oldAcl, nullptr);
    }
    if (oldSecurity) LocalFree(oldSecurity);
    if (deniedSecurity) LocalFree(deniedSecurity);
    if (protectedKey) RegCloseKey(protectedKey);
    std::ofstream(statePath, std::ios::trunc) << "{broken";
    check(!zencrop::RegisterBrowserCompanionBridge(manifest) && read(HKEY_CURRENT_USER, chrome) == moved,
        "corrupt record is preserved without registration changes");
    check(!zencrop::RegisterBrowserCompanionBridge((directory / L"missing.json").wstring()), "missing manifest refused");
    std::ofstream(std::filesystem::path(manifest), std::ios::trunc) << R"({"name":"foreign","path":"../ZenCrop.exe"})";
    check(!zencrop::RegisterBrowserCompanionBridge(manifest), "foreign manifest refused");
    if (!failures) std::cout << "PASS isolated companion registry lookup, ownership, move and failure contracts\n";
    return failures;
}
}

int RunCompanionRegistrationSubprocess() {
    wchar_t exe[32768]{};
    if (!GetModuleFileNameW(nullptr, exe, 32768)) return 1;
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --test-companion-registration";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        std::cerr << "FAIL companion registry test subprocess\n"; return 1;
    }
    const auto wait = WaitForSingleObject(process.hProcess, 15000);
    DWORD code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else { TerminateProcess(process.hProcess, 1); std::cerr << "FAIL companion registry subprocess timed out\n"; }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

// ---------------------------------------------------------------------------
// Direct connection contracts (WebSocket transport, pairing store, bootstrap).
//
// Everything here runs offline against loopback sockets and temporary files: no
// browser is opened and no user window is touched. The identity binding that needs a
// real Chromium tree (BrowserRootPid on a live browser, and a query answered through an
// authenticated peer) is covered by the live acceptance run instead, and this file
// states that boundary rather than mocking it away.
// ---------------------------------------------------------------------------
namespace {

int g_directFailures = 0;
void Check(bool passed, const char *description) {
    if (passed)
        return;
    ++g_directFailures;
    std::cerr << "FAIL companion direct " << description << '\n';
}

DWORD CurrentHandleCount() {
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

using browser_companion::websocket::Listener;
using zencrop::browser_pairing::Store;

bool WriteSocket(SOCKET socket, std::string_view data) {
    while (!data.empty()) {
        const int count = send(socket, data.data(), static_cast<int>(data.size()), 0);
        if (count <= 0)
            return false;
        data.remove_prefix(count);
    }
    return true;
}

// Server-to-client frames are unmasked text frames; this reads exactly one message.
std::string ReadServerMessage(SOCKET socket) {
    const auto read = [&](std::span<char> bytes) {
        while (!bytes.empty()) {
            const int count = recv(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
            if (count <= 0)
                return false;
            bytes = bytes.subspan(count);
        }
        return true;
    };
    unsigned char header[2]{};
    if (!read(std::span(reinterpret_cast<char *>(header), 2)) || header[0] != 0x81 || (header[1] & 0x80))
        return {};
    uint64_t length = header[1] & 0x7f;
    if (length >= 126) {
        const size_t count = length == 126 ? 2 : 8;
        unsigned char extended[8]{};
        if (!read(std::span(reinterpret_cast<char *>(extended), count)))
            return {};
        length = 0;
        for (size_t i = 0; i < count; ++i)
            length = (length << 8) | extended[i];
    }
    if (length == 0 || length > 65536)
        return {};
    std::string message(static_cast<size_t>(length), '\0');
    return read(std::span(message.data(), message.size())) ? message : std::string{};
}

// Reads exactly one server frame and reports its opcode: a protocol-level control frame
// has to be told apart from a message, and decoding it as text would hide it.
bool ReadServerFrame(SOCKET socket, unsigned char &opcode, std::string &payload) {
    const auto read = [&](std::span<char> bytes) {
        while (!bytes.empty()) {
            const int count = recv(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
            if (count <= 0)
                return false;
            bytes = bytes.subspan(count);
        }
        return true;
    };
    unsigned char header[2]{};
    if (!read(std::span(reinterpret_cast<char *>(header), 2)))
        return false;
    opcode = header[0] & 0x0f;
    if ((header[0] & 0x80) == 0 || (header[1] & 0x80))
        return false;
    uint64_t length = header[1] & 0x7f;
    if (length >= 126) {
        const size_t count = length == 126 ? 2 : 8;
        unsigned char extended[8]{};
        if (!read(std::span(reinterpret_cast<char *>(extended), count)))
            return false;
        length = 0;
        for (size_t i = 0; i < count; ++i)
            length = (length << 8) | extended[i];
    }
    if (length > 65536)
        return false;
    payload.assign(static_cast<size_t>(length), '\0');
    return payload.empty() || read(std::span(payload.data(), payload.size()));
}

std::string ClientFrame(std::string_view text, unsigned char opcode = 1, bool masked = true, bool final = true) {
    std::string frame;
    frame += static_cast<char>((final ? 0x80 : 0) | opcode);
    const unsigned char mask = masked ? 0x80 : 0;
    if (text.size() < 126)
        frame += static_cast<char>(mask | text.size());
    else if (text.size() <= 65535) {
        frame += static_cast<char>(mask | 126);
        frame += static_cast<char>(text.size() >> 8);
        frame += static_cast<char>(text.size());
    } else {
        frame += static_cast<char>(mask | 127);
        for (int shift = 56; shift >= 0; shift -= 8)
            frame += static_cast<char>(static_cast<uint64_t>(text.size()) >> shift);
    }
    const unsigned char key[]{1, 2, 3, 4};
    if (masked)
        frame.append(reinterpret_cast<const char *>(key), sizeof(key));
    for (size_t i = 0; i < text.size(); ++i)
        frame += static_cast<char>(static_cast<unsigned char>(text[i]) ^ (masked ? key[i % 4] : 0));
    return frame;
}

enum class AuthAttack { None, WrongSecret, WrongVersion, UnknownProfile, WrongInstance, Reflected, Replay };

struct AuthFixture {
    Store *store = nullptr;
    std::string profile;
    std::string secret;
    std::string instance;
    std::string savedProof;
    AuthAttack attack = AuthAttack::None;
    bool dropAck = false;
};

// Client half of the mutual authentication, mirroring the extension exactly.
void AuthenticateClient(SOCKET client, AuthFixture &fixture) {
    using namespace browser_companion;
    const auto challenge = ReadServerMessage(client);
    if (challenge.empty())
        return;
    auto challengeFields = ParseChallenge(challenge);
    if (!challengeFields) {
        Check(false, "server challenge matches the v2 control schema");
        return;
    }
    if (fixture.attack == AuthAttack::WrongInstance)
        challengeFields->instance = std::string(32, '1');
    const auto clientNonce = zencrop::browser_pairing::RandomHex(32);
    const auto profile = fixture.attack == AuthAttack::UnknownProfile ? std::string(32, '0') : fixture.profile;
    if (fixture.attack == AuthAttack::WrongVersion) {
        // A stale extension speaking the old control version must not be accepted.
        WriteSocket(client, ClientFrame(std::format(R"({{"version":3,"type":"auth_start","profile":"{}","nonce":"{}"}})",
                                                 profile, clientNonce)));
    } else {
        WriteSocket(client, ClientFrame(BuildAuthStart(profile, clientNonce)));
    }
    const auto serverProof = ReadServerMessage(client);
    if (serverProof.empty())
        return;
    const auto proof = ParseAuthProof(serverProof, kAuthServerType);
    const auto transcript = challengeFields->instance + ":" + clientNonce + ":" + challengeFields->nonce;
    if (fixture.attack == AuthAttack::WrongInstance) {
        // The client believes it is talking to another installation, so the desktop
        // proof must not verify against its own transcript.
        Check(!proof || !zencrop::browser_pairing::EqualProof(
                            *proof, zencrop::browser_pairing::Hmac(fixture.secret, "server:" + transcript)),
              "a challenge from another installation does not verify");
    } else {
        Check(proof && zencrop::browser_pairing::EqualProof(
                            *proof, zencrop::browser_pairing::Hmac(fixture.secret, "server:" + transcript)),
              "the desktop proves its identity before accepting the client");
    }
    std::string answer;
    if (fixture.attack == AuthAttack::Replay)
        answer = fixture.savedProof;
    else {
        const auto secret = fixture.attack == AuthAttack::WrongSecret ? std::string(64, '0') : fixture.secret;
        answer = zencrop::browser_pairing::Hmac(secret,
            (fixture.attack == AuthAttack::Reflected ? "server:" : "client:") + transcript);
        if (fixture.attack == AuthAttack::None)
            fixture.savedProof = answer;
    }
    WriteSocket(client, ClientFrame(BuildAuthProof(kAuthClientType, answer)));
    const auto accepted = ReadServerMessage(client);
    if (fixture.attack == AuthAttack::None && !fixture.dropAck)
        Check(ParseControlAck(accepted, kAuthOkType), "a valid client receives the authenticated acknowledgement");
    else
        Check(accepted.empty() || !ParseControlAck(accepted, kAuthOkType),
              "an invalid client never receives an authenticated acknowledgement");
    if (!accepted.empty())
        WriteSocket(client, ClientFrame("{\"ack\":true}"));
}

struct ExchangeResult {
    bool upgraded = false;
    DWORD pid = 0;
    std::string reason;
    std::expected<std::string, std::string> received = std::unexpected("not_received");
    ULONGLONG cancelledMs = 0;
    bool authenticated = false;
};

ExchangeResult Exchange(const std::string &headers, const std::string &frames, bool cancel = false,
                        AuthFixture *auth = nullptr, DWORD closeDelayMs = 20) {
    using namespace browser_companion;
    Listener listener;
    if (!listener.Start(0)) {
        Check(false, "test listener binds loopback without elevation");
        return {};
    }
    Listener collision;
    Check(!collision.Start(listener.Port()), "a listener cannot share or steal an occupied port");
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::promise<ExchangeResult> promise;
    auto result = promise.get_future();
    std::jthread server([&] {
        ExchangeResult output;
        auto channel = listener.Accept(stop);
        if (channel) {
            output.pid = channel->ProcessId();
            const auto upgrade = channel->Upgrade(kExtensionHandshakeOrigin);
            output.reason = upgrade ? std::string{} : upgrade.error();
            output.upgraded = upgrade.has_value();
            if (output.upgraded) {
                if (auth) {
                    output.authenticated =
                        AuthenticateChannel(*channel, *auth->store, L"C:\\Browser\\chrome.exe").has_value();
                    if (output.authenticated)
                        (void)channel->Receive(500);
                } else if (cancel) {
                    const auto start = GetTickCount64();
                    std::jthread closer([&] {
                        Sleep(closeDelayMs);
                        channel->Close();
                    });
                    output.received = channel->Receive(closeDelayMs == 20 ? INFINITE : 1000);
                    closer.join();
                    output.cancelledMs = GetTickCount64() - start;
                    Check(!channel->Receive(0), "a receive started after closure cannot wait or return a message");
                } else {
                    output.received = channel->Receive(1500);
                }
            }
        }
        promise.set_value(std::move(output));
    });
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(listener.Port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    DWORD timeout = 2500;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
    const bool connected = connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    Check(connected, "loopback test client connects");
    if (connected) {
        const auto request = std::format("GET {} HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n"
                                         "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                                         "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                         "Sec-WebSocket-Version: 13\r\n{}\r\n",
                                         websocket::kDirectPath, listener.Port(), headers);
        WriteSocket(client, request);
        std::string response;
        char byte{};
        while (response.size() < 8192 && !response.ends_with("\r\n\r\n") && recv(client, &byte, 1, 0) == 1)
            response += byte;
        if (response.starts_with("HTTP/1.1 101")) {
            Check(response.find("Sec-WebSocket-Accept") != std::string::npos,
                  "the Windows component constructs the server accept proof");
            if (!frames.empty())
                WriteSocket(client, frames);
            if (auth)
                AuthenticateClient(client, *auth);
        }
    }
    const bool finished = result.wait_for(std::chrono::seconds(4)) == std::future_status::ready;
    Check(finished, "handshake, receive and cancellation stay bounded");
    if (!finished)
        shutdown(client, SD_BOTH);
    SetEvent(stop);
    closesocket(client);
    server.join();
    CloseHandle(stop);
    return result.get();
}

int RunDirectConnectionContracts() {
    using namespace browser_companion;
    int failures = 0;
    const auto expect = [&](bool passed, const char *name) {
        if (!passed) {
            ++failures;
            Check(false, name);
        }
    };

    // --- T01: the v1 message set is unchanged, and the control plane is not part of it.
    {
        const auto v1 = Hello().dump();
        expect(ParseMessage(v1).has_value(), "v1 hello still accepted");
        for (const std::string control : {BuildPairRequest(kExtensionIdUtf8, std::string(32, 'a')),
                                          BuildChallenge(std::string(32, 'a'), std::string(64, 'b')),
                                          BuildControlAck(kPingType)}) {
            expect(!ParseMessage(control), "control messages never enter the v1 message set");
            expect(ParseVersionEnvelope(control).has_value(), "control messages are structurally parsable");
        }
        auto large = Hello();
        large["padding"] = "";
        large["padding"] = std::string(kMaxMessageBytes - large.dump().size(), 'x');
        expect(ParseMessage(large.dump()).has_value(), "maximum v1 message accepted");
        large["padding"].get_ref<std::string &>().push_back('x');
        expect(!ParseMessage(large.dump()), "oversized v1 message rejected");
    }

    // --- T02: the pairing bootstrap travels over real framed bytes, in both directions.
    {
        Pipe pipe;
        const auto request = BuildPairRequest(kExtensionIdUtf8, std::string(32, 'b'));
        expect(WriteFrameBytes(pipe.write, request), "bootstrap request framed onto stdio");
        const auto read = ReadFrameBytes(pipe.read);
        expect(read && *read == request, "bootstrap request survives the round trip");
        const auto parsed = read ? ParsePairRequest(*read) : std::unexpected(std::string("unreadable"));
        expect(parsed && parsed->profile == std::string(32, 'b'), "bootstrap request matches its schema");
        const auto grant = BuildPairedReply(websocket::kDirectPort, std::string(32, 'c'), parsed->profile,
                                            std::string(64, 'd'));
        expect(WriteFrameBytes(pipe.write, grant), "paired reply framed onto stdio");
        const auto grantBytes = ReadFrameBytes(pipe.read);
        const auto grantFields = grantBytes ? ParsePairedReply(*grantBytes) : std::unexpected(std::string("unreadable"));
        expect(grantFields && grantFields->port == websocket::kDirectPort && grantFields->secret == std::string(64, 'd'),
              "paired reply matches its schema");
        expect(ParsePairingError(BuildPairingError("desktop_unavailable")).has_value(), "pairing error is parsable");
        auto wrapped = Json::parse(request);
        wrapped["version"] = 4294967297ULL;
        expect(!ParsePairRequest(wrapped.dump()), "bootstrap version cannot wrap to v1");
        auto wrappedAuth = Json::parse(BuildAuthStart(std::string(32, 'a'), std::string(64, 'b')));
        wrappedAuth["version"] = 4294967298ULL;
        expect(!ParseAuthStart(wrappedAuth.dump()), "authentication version cannot wrap to v2");
        auto wrappedGrant = Json::parse(grant);
        wrappedGrant["wsVersion"] = 4294967298ULL;
        expect(!ParsePairedReply(wrappedGrant.dump()), "negotiated version cannot wrap to v2");
        wrappedGrant["wsVersion"] = kPairingWsVersion;
        wrappedGrant["port"] = std::numeric_limits<uint64_t>::max();
        expect(!ParsePairedReply(wrappedGrant.dump()), "negotiated port is range-checked before conversion");
        for (const std::string bad : {BuildPairRequest("someone-else", std::string(32, 'b')),
                                      BuildPairRequest(kExtensionIdUtf8, "NOT-HEX"),
                                      BuildPairRequest(kExtensionIdUtf8, std::string(31, 'b')),
                                      std::string(R"({"version":2,"type":"pair","ext":"x","profile":"y"})"),
                                      std::string(R"({"version":1,"type":"pair","ext":"halonieocgkgbdogndcipmlpklljmoja","profile":"y","extra":1})"),
                                      std::string(R"({"version":1,"type":"pair","ext":"halonieocgkgbdogndcipmlpklljmoja","ext":"halonieocgkgbdogndcipmlpklljmoja","profile":"y"})")}) {
            expect(!ParsePairRequest(bad), "wrong extension, wrong hex, wrong version, extra or duplicate keys rejected");
        }
        std::string oversized = BuildPairRequest(kExtensionIdUtf8, std::string(32, 'b'));
        oversized += std::string(1024, ' ');
        expect(!ParsePairRequest(oversized), "an oversized bootstrap frame is rejected");
    }

    // --- T03: handshake field validation, including the two Origin spellings.
    {
        const std::string valid = "Origin: chrome-extension://halonieocgkgbdogndcipmlpklljmoja\r\n"
                                  "Sec-WebSocket-Protocol: zencrop-companion-v2\r\n";
        const auto normal = Exchange(valid, ClientFrame(R"({"ok":true})"));
        expect(normal.upgraded && normal.received && *normal.received == R"({"ok":true})",
              "a masked text frame reaches the bounded message queue");
        expect(normal.pid == GetCurrentProcessId(), "the TCP peer PID comes from the kernel table, not from JSON");
        expect(websocket::kDirectPath == "/zencrop/browser/v2" &&
                   websocket::kDirectSubprotocol == "zencrop-companion-v2" && websocket::kDirectPort == 19227,
              "transport constants are pinned");
        for (const std::string bad : {
                 std::string("Origin: https://example.test\r\nSec-WebSocket-Protocol: zencrop-companion-v2\r\n"),
                 std::string("Origin: chrome-extension://anotherextension\r\nSec-WebSocket-Protocol: zencrop-companion-v2\r\n"),
                 // The native argv/manifest form carries a trailing slash; HTTP Origin does not.
                 std::string("Origin: chrome-extension://halonieocgkgbdogndcipmlpklljmoja/\r\nSec-WebSocket-Protocol: zencrop-companion-v2\r\n"),
                 std::string("Origin: chrome-extension://halonieocgkgbdogndcipmlpklljmp\r\nSec-WebSocket-Protocol: zencrop-companion-v2\r\n"),
                 std::string("Origin: chrome-extension://halonieocgkgbdogndcipmlpklljmoja\r\nSec-WebSocket-Protocol: wrong\r\n"),
                 valid + "Origin: chrome-extension://halonieocgkgbdogndcipmlpklljmoja\r\n",
                 valid + "Content-Length: 2\r\n",
                 std::string("Sec-WebSocket-Protocol: zencrop-companion-v2\r\n")}) {
            expect(!Exchange(bad, {}).upgraded,
                  "pages, other extensions, slash spellings, prefixes, wrong protocol and ambiguous headers rejected");
        }
    }

    // --- T04: framing limits, malformed frames and the phase deadlines.
    {
        const std::string headers = "Origin: chrome-extension://halonieocgkgbdogndcipmlpklljmoja\r\n"
                                    "Sec-WebSocket-Protocol: zencrop-companion-v2\r\n";
        const auto fragmented = Exchange(headers, ClientFrame(R"({"ok":)", 1, true, false) + ClientFrame(R"(true})", 0));
        expect(fragmented.received && *fragmented.received == R"({"ok":true})",
              "the Windows component reassembles a fragmented UTF-8 message");
        const std::string oversized = R"({"v":")" + std::string(65536, 'a') + R"("})";
        std::string deep(66, '[');
        deep += '0';
        deep.append(66, ']');
        // Two layers, two properties. The transport refuses what it can see (opcode,
        // size, UTF-8); structural validity is the protocol layer's job, and a frame it
        // would reject must never reach a peer loop as a usable message.
        struct Hostile {
            const char *name;
            std::string frame;
            bool transportMustRefuse;
        };
        for (const auto &hostile : std::vector<Hostile>{
                 {"binary opcode", ClientFrame(R"({"ok":true})", 2), true},
                 {"unmasked client frame", ClientFrame(R"({"ok":true})", 1, false), true},
                 {"stray continuation frame", ClientFrame(R"({"ok":true})", 0), true},
                 {"oversized frame", ClientFrame(oversized), true},
                 {"invalid UTF-8", ClientFrame("{\"v\":\"\xff\"}"), true},
                 {"non-JSON payload", ClientFrame("not json"), false},
                 {"over-deep nesting", ClientFrame(deep), false}}) {
            const auto result = Exchange(headers, hostile.frame);
            expect(result.upgraded, (std::string("hostile frame still completed the handshake: ") + hostile.name).c_str());
            if (hostile.transportMustRefuse) {
                expect(!result.received, (std::string("transport never delivers: ") + hostile.name).c_str());
            } else {
                // Delivered as bytes, refused as a message: both validators must reject.
                const auto delivered = result.received ? *result.received : std::string{};
                expect(!delivered.empty() ? !ParseVersionEnvelope(delivered).has_value() : true,
                      (std::string("envelope validation refuses: ") + hostile.name).c_str());
                expect(!delivered.empty() ? !ParseMessage(delivered).has_value() : true,
                      (std::string("v1 validation refuses: ") + hostile.name).c_str());
            }
        }
        // A handshake that stops half way must fail on its own deadline, not by luck.
        Listener listener;
        expect(listener.Start(0).has_value(), "half-handshake fixture listener started");
        if (listener.Port() != 0) {
            HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::jthread server([&] {
                auto channel = listener.Accept(stop);
                if (channel) {
                    const auto start = GetTickCount64();
                    const auto upgrade = channel->Upgrade(kExtensionHandshakeOrigin);
                    Check(!upgrade.has_value() && GetTickCount64() - start <= 3000,
                          "a half-finished handshake fails on its bounded deadline");
                    channel->Close();
                }
            });
            SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(listener.Port());
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            DWORD timeout = 4000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
            if (connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
                // Request line only: no terminating CRLFCRLF.
                WriteSocket(client, "GET /zencrop/browser/v2 HTTP/1.1\r\nHost: 127.0.0.1\r\n");
                server.join();
            } else {
                Check(false, "half-handshake fixture connected");
                SetEvent(stop);
                server.join();
            }
            closesocket(client);
            CloseHandle(stop);
        }
        // Cancellation must not wait for a natural timeout (T07/T09).
        const auto cancelled = Exchange(headers, {}, true);
        expect(cancelled.upgraded && !cancelled.received && cancelled.cancelledMs < 1000,
              "shutdown interrupts an idle receive and joins its I/O worker promptly");
        for (int attempt = 0; attempt < 64; ++attempt) {
            const auto raced = Exchange(headers, {}, true, nullptr, attempt % 2);
            expect(raced.upgraded && !raced.received && raced.cancelledMs < 500,
                  "closure racing with receive entry cannot lose the wakeup");
        }
    }

    // --- T05: mutual authentication, and the refusal of every wrong variant.
    {
        const std::string headers = "Origin: chrome-extension://halonieocgkgbdogndcipmlpklljmoja\r\n"
                                    "Sec-WebSocket-Protocol: zencrop-companion-v2\r\n";
        const auto directory = std::filesystem::temp_directory_path() /
                               std::format(L"ZenCropPairingTest-{}", GetCurrentProcessId());
        std::error_code ignored;
        std::filesystem::create_directories(directory, ignored);
        const auto path = (directory / L"pairing.dat").wstring();
        Store store(path);
        expect(store.Load().has_value() && store.Count() == 0, "a fresh credential store begins unpaired");
        const auto instance = store.InstanceId();
        const auto profile = zencrop::browser_pairing::RandomHex(16);
        const auto paired = store.Pair(profile, L"C:\\Browser\\chrome.exe");
        expect(paired && zencrop::browser_pairing::IsLowerHex(paired->secret, 32),
              "pairing issues a per-profile strong credential");
        if (!paired)
            return failures;
        const auto encrypted = [&] {
            std::ifstream file(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), {});
        }();
        expect(!encrypted.empty() && encrypted.find(paired->secret) == std::string::npos,
              "credentials are DPAPI-encrypted on disk rather than stored as plaintext");
        Store restarted(path);
        expect(restarted.Load().has_value() && restarted.InstanceId() == instance &&
                   restarted.Find(profile, L"C:\\Browser\\chrome.exe").has_value(),
              "pairing survives a desktop restart");
        expect(!restarted.Find(profile, L"C:\\Browser\\msedge.exe"),
              "a Chrome pairing cannot authenticate another browser image");
        expect(restarted.CountForBrowser(L"chrome.exe") == 1 && restarted.CountForBrowser(L"msedge.exe") == 0,
              "paired browser states stay independent");
        const auto rotated = restarted.Pair(profile, L"C:\\Browser\\chrome.exe");
        expect(rotated && rotated->secret != paired->secret && restarted.Count() == 1,
              "re-pairing rotates only that profile's credential");

        AuthFixture fixture{&restarted, rotated->profile, rotated->secret, restarted.InstanceId()};
        expect(Exchange(headers, {}, false, &fixture).authenticated,
              "a real WebSocket mutual authentication accepts the paired profile");
        for (const auto attack : {AuthAttack::WrongSecret, AuthAttack::WrongVersion, AuthAttack::UnknownProfile,
                                  AuthAttack::WrongInstance, AuthAttack::Reflected, AuthAttack::Replay}) {
            fixture.attack = attack;
            expect(!Exchange(headers, {}, false, &fixture).authenticated,
                  "wrong key, version, profile, instance, reflected and replayed proofs fail authentication");
        }
        fixture.attack = AuthAttack::None;
        expect(!BrowserRootPid(GetCurrentProcessId()),
              "an ordinary local process cannot impersonate the TCP browser owner");

        // --- T20: revocation fences every previously authenticated peer.
        expect(restarted.RevokeAll().has_value() && restarted.Count() == 0 &&
                   restarted.InstanceId() != instance && !restarted.Find(profile, L"C:\\Browser\\chrome.exe"),
              "explicit revocation invalidates every credential and rotates the instance identity");
        fixture.store = &restarted;
        expect(!Exchange(headers, {}, false, &fixture).authenticated,
              "a credential revoked after pairing no longer authenticates");
        // A damaged store refuses pairing and reports nothing countable, so the only
        // way out is an explicit revocation - which is what the settings page offers.
        expect(restarted.Pair(profile, L"C:\\Browser\\chrome.exe").has_value(), "re-pairing after revocation works");
        {
            std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
            corrupt << "{\"instance\":\"" << std::string(32, 'b') << "\",\"credentials\":[{\"profile\":\"zz\"}]}";
        }
        Store damaged(path);
        expect(!damaged.Load().has_value() && !damaged.Error().empty() && damaged.Count() == 0,
              "a damaged store fails closed, reports a named error and counts nothing");
        expect(!damaged.Pair(profile, L"C:\\Browser\\chrome.exe"),
              "a damaged store refuses to issue new credentials");
        expect(!damaged.Find(profile, L"C:\\Browser\\chrome.exe"), "a damaged store hands out no credential");
        expect(damaged.RevokeAll().has_value() && damaged.Error().empty() && damaged.Count() == 0,
              "explicit revocation is the only way out of a damaged store, and it recovers it");
        Store recovered(path);
        expect(recovered.Load().has_value() && recovered.Error().empty() && recovered.Count() == 0,
              "the recovered store loads cleanly with no credentials");
        std::filesystem::remove_all(directory, ignored);
    }

    // --- T10/T11: listener lifecycle stays leak-free and bounded.
    {
        Listener listener;
        expect(listener.Port() == 0, "an unstarted listener reports no port");
        // A failure must not poison the object: the reference it took is released on the
        // failing path, so the same listener can still start once the port is free.
        {
            Listener blocker;
            expect(blocker.Start(0).has_value(), "a blocker listener takes a port");
            Listener unlucky;
            expect(!unlucky.Start(blocker.Port()), "a listener cannot take an occupied port");
            expect(unlucky.Port() == 0, "a failed start leaves the listener unusable but consistent");
            blocker.Close();
            expect(unlucky.Start(blocker.Port()).has_value(),
                  "the same listener starts after a failed start once the port is free");
            unlucky.Close();
        }
        const auto started = listener.Start(0);
        expect(started.has_value() && listener.Port() != 0, "listener starts on an ephemeral loopback port");
        expect(listener.Start(0).has_value(), "starting an already started listener is idempotent");
        listener.Close();
        listener.Close();
        expect(listener.Port() == 0, "a closed listener releases its port");
        for (int attempt = 0; attempt < 16; ++attempt) {
            Listener churned;
            if (!churned.Start(0))
                continue;
            HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::jthread server([&] {
                for (int i = 0; i < 4; ++i) {
                    auto channel = churned.Accept(stop);
                    if (!channel)
                        break;
                    channel->Close();
                }
            });
            for (int i = 0; i < 4; ++i) {
                SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(churned.Port());
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
                    WriteSocket(client, "GET /zencrop/browser/v2 HTTP/1.1\r\n\r\n");
                closesocket(client);
            }
            SetEvent(stop);
            server.join();
            CloseHandle(stop);
            churned.Close();
        }
        // Repeated failed connections must not accumulate threads or handles.
        const auto handles = CurrentHandleCount();
        for (int attempt = 0; attempt < 32; ++attempt) {
            (void)Exchange("Origin: https://example.test\r\nSec-WebSocket-Protocol: zencrop-companion-v2\r\n", {});
        }
        expect(CurrentHandleCount() <= handles + 32,
              "repeated rejected connections release their sockets and threads");
    }

    // --- T09: stopping the bridge with no peers is bounded, and repeatable.
    {
        auto &bridge = Bridge::Instance();
        const auto start = GetTickCount64();
        expect(bridge.Start(), "bridge starts for lifecycle contracts");
        const auto status = bridge.QueryStatus();
        expect(status.pairedChrome + status.pairedEdge == 0 && status.legacy == 0,
              "a fresh bridge reports no paired and no legacy peers");
        bridge.Stop();
        const auto elapsed = GetTickCount64() - start;
        expect(elapsed <= 500, "start and stop without peers stay bounded");
        const auto second = GetTickCount64();
        bridge.Stop();
        expect(GetTickCount64() - second <= 250, "a repeated stop is safe and immediate");
        expect(bridge.IsValid("session", "document") == false && bridge.IsConnectedBrowser(0) == false,
              "a stopped bridge authorizes nothing");
    }

    // --- A control frame whose fields have the wrong type must be refused by the one
    //     gate every caller relies on, rather than throwing out of a peer thread and
    //     taking the product down with it.
    {
        for (const char *hostile : {R"({"version":"2","type":"ping"})",
                                    R"({"version":2,"type":123})",
                                    R"({"version":2})",
                                    R"({"type":"ping"})",
                                    R"({"version":-2,"type":"ping"})",
                                    R"({"version":18446744073709551615,"type":"ping"})"}) {
            expect(!ParseVersionEnvelope(hostile), "a control envelope with a wrong field type or range is refused");
        }
        expect(ParseVersionEnvelope(BuildControlAck(kPingType)).has_value(),
              "the real heartbeat envelope still passes the gate");
        expect(ParseVersionEnvelope(BuildChallenge(std::string(32, 'a'), std::string(64, 'b'))).has_value(),
              "the real challenge envelope still passes the gate");
        // The same refusal, on a real authenticated socket: what the peer thread sees is
        // bytes the gate already rejected, so no typed accessor can ever run on it.
        const auto directory = std::filesystem::temp_directory_path() /
                               ("zencrop-direct-" + std::to_string(GetCurrentProcessId()) + "-hostile");
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory);
        Store store(directory / L"pairing.dat");
        expect(store.Load().has_value(), "a store is available for the wire-level refusal");
        const auto paired = store.Pair(std::string(32, 'a'), L"C:\\Browser\\chrome.exe");
        expect(paired.has_value(), "a credential exists for the wire-level refusal");
        Listener listener;
        expect(listener.Start(0).has_value(), "listener binds for the wire-level refusal");
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::promise<bool> refused;
        auto refusedFuture = refused.get_future();
        std::jthread server([&] {
            bool outcome = false;
            if (auto channel = listener.Accept(stop)) {
                if (channel->Upgrade(kExtensionHandshakeOrigin) &&
                    AuthenticateChannel(*channel, store, L"C:\\Browser\\chrome.exe")) {
                    if (const auto frame = channel->Receive(1500))
                        outcome = !ParseVersionEnvelope(*frame).has_value();
                    channel->Close();
                }
            }
            refused.set_value(outcome);
        });
        SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(listener.Port());
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        DWORD timeout = 2500;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        const bool connected = connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
        expect(connected, "the wire-level refusal client connects");
        WriteSocket(client, std::format("GET {} HTTP/1.1\r\nHost: 127.0.0.1:{}\r\nUpgrade: websocket\r\n"
                                        "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                        "Sec-WebSocket-Version: 13\r\nOrigin: {}\r\n"
                                        "Sec-WebSocket-Protocol: zencrop-companion-v2\r\n\r\n",
                                        websocket::kDirectPath, listener.Port(), kExtensionHandshakeOrigin));

        std::string response;
        {
            char byte{};
            while (response.size() < 8192 && !response.ends_with("\r\n\r\n") && recv(client, &byte, 1, 0) == 1)
                response += byte;
        }
        expect(response.starts_with("HTTP/1.1 101"), "the wire-level refusal client upgraded");
        AuthFixture fixture{&store, paired->profile, paired->secret, store.InstanceId()};
        AuthenticateClient(client, fixture);
        WriteSocket(client, ClientFrame(R"({"version":"2","type":"ping"})"));
        expect(refusedFuture.wait_for(std::chrono::seconds(4)) == std::future_status::ready &&
                   refusedFuture.get(),
              "a type-confused control frame is refused at the shared gate on a real socket");
        SetEvent(stop);
        closesocket(client);
        server.join();
        CloseHandle(stop);
        std::filesystem::remove_all(directory, ignored);
    }

    // --- T21: a protocol-level ping is answered by the component, payload and all.
    //     The application heartbeat is JSON over the same socket, so nothing else in
    //     this suite reaches the control-frame path.
    {
        Listener listener;
        expect(listener.Start(0).has_value(), "listener binds for the control-frame contract");
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::promise<bool> answered;
        auto answeredFuture = answered.get_future();
        std::jthread server([&] {
            bool outcome = false;
            if (auto channel = listener.Accept(stop)) {
                if (channel->Upgrade(kExtensionHandshakeOrigin)) {
                    // The component answers the protocol-level ping itself, so the first
                    // frame the transport ever delivers here is the JSON heartbeat - and
                    // it arrives as bytes the peer loop classifies as v2 control.
                    const auto frame = channel->Receive(1500);
                    const auto envelope = frame ? ParseVersionEnvelope(*frame) : std::expected<Json, std::string>{};
                    outcome = envelope.has_value() &&
                              (*envelope)["version"].get<int>() == kControlVersion &&
                              (*envelope)["type"].get<std::string>() == std::string(kPingType);
                    channel->Close();
                }
            }
            answered.set_value(outcome);
        });
        SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(listener.Port());
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        DWORD timeout = 2500;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        expect(connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
              "the control-frame client connects");
        WriteSocket(client, std::format("GET {} HTTP/1.1\r\nHost: 127.0.0.1:{}\r\nUpgrade: websocket\r\n"
                                        "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                        "Sec-WebSocket-Version: 13\r\nOrigin: {}\r\n"
                                        "Sec-WebSocket-Protocol: zencrop-companion-v2\r\n\r\n",
                                        websocket::kDirectPath, listener.Port(), kExtensionHandshakeOrigin));
        std::string response;
        {
            char byte{};
            while (response.size() < 8192 && !response.ends_with("\r\n\r\n") && recv(client, &byte, 1, 0) == 1)
                response += byte;
        }
        expect(response.starts_with("HTTP/1.1 101"), "the control-frame client upgraded");
        const std::string payload = "zencrop-ping-payload";
        WriteSocket(client, ClientFrame(payload, 0x9));
        unsigned char opcode = 0;
        std::string echoed;
        expect(ReadServerFrame(client, opcode, echoed) && opcode == 0xA && echoed == payload,
              "a protocol-level ping with a payload is answered with a pong carrying that payload");
        // The application heartbeat travels the same socket and reaches the peer loop as
        // v2 control, which is what routes it away from the business dispatch.
        WriteSocket(client, ClientFrame(BuildControlAck(kPingType)));
        expect(answeredFuture.wait_for(std::chrono::seconds(4)) == std::future_status::ready && answeredFuture.get(),
              "a JSON heartbeat reaches the peer loop classified as v2 control, not as a business message");
        SetEvent(stop);
        closesocket(client);
        server.join();
        CloseHandle(stop);
    }

    // --- T22: re-pairing one profile must leave every other connection alone.
    {
        const auto directory = std::filesystem::temp_directory_path() /
                               ("zencrop-direct-" + std::to_string(GetCurrentProcessId()) + "-profiles");
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory);
        Store store(directory / L"pairing.dat");
        expect(store.Load().has_value(), "a store is available for the profile isolation contract");
        const std::string chrome = std::string(32, 'c');
        const std::string edge = std::string(32, 'e');
        const auto first = store.Pair(chrome, L"C:\\Browser\\chrome.exe");
        const auto second = store.Pair(edge, L"C:\\Browser\\msedge.exe");
        expect(first.has_value() && second.has_value(), "two profiles pair independently");
        expect(store.Matches(chrome, L"C:\\Browser\\chrome.exe", first->secret) &&
                   store.Matches(edge, L"C:\\Browser\\msedge.exe", second->secret),
              "both authenticated connections are current");
        // This is the regression: a second profile pairing used to invalidate the first.
        expect(store.Matches(chrome, L"C:\\Browser\\chrome.exe", first->secret),
              "pairing another profile leaves an established connection authenticated");
        const auto rotated = store.Pair(chrome, L"C:\\Browser\\chrome.exe");
        expect(rotated.has_value() && !store.Matches(chrome, L"C:\\Browser\\chrome.exe", first->secret) &&
                   store.Matches(chrome, L"C:\\Browser\\chrome.exe", rotated->secret),
              "re-pairing one profile rotates only that profile's credential");
        expect(store.Matches(edge, L"C:\\Browser\\msedge.exe", second->secret),
              "a re-pairing of one profile leaves the other connection authenticated");
        expect(!store.Matches(chrome, L"C:\\Browser\\msedge.exe", rotated->secret),
              "a credential never matches across browser images");
        auto wrongSecret = rotated->secret;
        wrongSecret.back() = wrongSecret.back() == '0' ? '1' : '0';
        expect(!store.Matches(chrome, L"C:\\Browser\\chrome.exe", wrongSecret) &&
                   !store.Matches(chrome, L"C:\\Browser\\chrome.exe", "invalid"),
               "credential matching rejects a changed final byte and malformed secrets");
        expect(store.RevokeAll().has_value() &&
                   !store.Matches(chrome, L"C:\\Browser\\chrome.exe", rotated->secret) &&
                   !store.Matches(edge, L"C:\\Browser\\msedge.exe", second->secret),
              "revocation invalidates every connection, not just one profile");
        std::array<bool, 2> paired{};
        {
            std::jthread chromeWriter([&] { paired[0] = store.Pair(chrome, L"C:\\Browser\\chrome.exe").has_value(); });
            std::jthread edgeWriter([&] { paired[1] = store.Pair(edge, L"C:\\Browser\\msedge.exe").has_value(); });
        }
        Store reloaded(directory / L"pairing.dat");
        expect(paired[0] && paired[1] && reloaded.Load() && reloaded.Count() == 2,
               "concurrent commits preserve both profiles on disk");
        const auto committed = store.Find(chrome, L"C:\\Browser\\chrome.exe");
        std::filesystem::remove(directory / L"pairing.dat");
        std::filesystem::create_directory(directory / L"pairing.dat");
        expect(committed && !store.Pair(chrome, L"C:\\Browser\\chrome.exe") &&
                   store.Matches(chrome, L"C:\\Browser\\chrome.exe", committed->secret) && store.Count() == 2,
               "a failed disk commit leaves the published credentials intact");
        std::filesystem::remove_all(directory, ignored);
    }

    // --- T23: the native host's first-frame deadline is real on a synchronous handle.
    //     A pipe with no complete frame must end the wait, not hold a host process open.
    {
        Pipe pipe;
        const auto start = GetTickCount64();
        const auto silent = ReadFrameBytes(pipe.read, 300, {}, PipeIo::Synchronous);
        const auto elapsed = GetTickCount64() - start;
        expect(!silent && elapsed < 2000,
              "a silent synchronous pipe read is bounded by its deadline instead of blocking forever");
        Pipe partial;
        const char header[] = {0x09, 0x00};
        DWORD written = 0;
        WriteFile(partial.write, header, sizeof(header), &written, nullptr);
        const auto half = GetTickCount64();
        const auto incomplete = ReadFrameBytes(partial.read, 300, {}, PipeIo::Synchronous);
        expect(!incomplete && GetTickCount64() - half < 2000,
              "a half-written header is bounded by the same deadline");
        for (int attempt = 0; attempt < 32; ++attempt) {
            Pipe raced;
            std::stop_source cancel;
            std::jthread closer([&] {
                if (attempt % 2)
                    std::this_thread::yield();
                cancel.request_stop();
            });
            const auto begin = GetTickCount64();
            expect(!ReadFrameBytes(raced.read, 1000, cancel.get_token(), PipeIo::Synchronous) &&
                       GetTickCount64() - begin < 500,
                   "cancellation racing synchronous read submission drains its worker");
        }
    }

    if (!failures)
        std::cout << "PASS companion direct connection framing, authentication and store contracts\n";
    return failures;
}
}

int RunBrowserCompanionHostRegistrationContracts() {
    using zencrop::BrowserCompanionHostState;
    int failures = 0;
    const auto check = [&](bool passed, const char *name) {
        if (!passed) {
            ++failures;
            std::cerr << "FAIL companion host registration " << name << '\n';
        }
    };

    // A real, existing file stands in for the installed manifest, so the "exists" branch is
    // exercised against the filesystem instead of a mocked answer. The names are PID-scoped so
    // a concurrent run of this fixture cannot collide.
    wchar_t tempDir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDir))
        return 0;
    const std::wstring unique = L"zencrop-companion-classify-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring manifest = std::wstring(tempDir) + unique + L"-json";
    const std::wstring otherManifest = std::wstring(tempDir) + unique + L"-other-json";
    const std::wstring missing = std::wstring(tempDir) + unique + L"-absent-json";
    const auto writeFile = [](const std::wstring &path) {
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        const char payload[] = "{}";
        const bool ok = WriteFile(handle, payload, 2, &written, nullptr) != FALSE;
        CloseHandle(handle);
        return ok;
    };
    struct Cleanup {
        const std::wstring &a;
        const std::wstring &b;
        const std::wstring &c;
        ~Cleanup() {
            DeleteFileW(a.c_str());
            DeleteFileW(b.c_str());
            DeleteFileW(c.c_str());
        }
    } cleanup{manifest, otherManifest, missing};

    if (!writeFile(manifest) || !writeFile(otherManifest)) {
        check(false, "host registration classifier fixture created");
        return failures;
    }

    const auto classify = [&](const std::wstring &registered, const std::wstring &local) {
        return zencrop::ClassifyBrowserCompanionHost(registered, local);
    };

    check(classify(L"", manifest) == BrowserCompanionHostState::NotRegistered,
          "absent host key is not registered");
    check(classify(manifest, manifest) == BrowserCompanionHostState::Ready,
          "host key naming this instance's manifest is ready");
    check(classify(otherManifest, manifest) == BrowserCompanionHostState::PathMismatch,
          "host key naming another existing manifest is a mismatch");
    check(classify(missing, manifest) == BrowserCompanionHostState::TargetMissing,
          "host key naming a missing manifest is reported as missing");

    // Ready must additionally require the registered manifest to exist on disk: a stale
    // registration to a deleted directory is not ready, even though the paths agree.
    check(classify(missing, missing) == BrowserCompanionHostState::TargetMissing,
          "missing manifest is never ready even when the paths agree");

    // Spelling differences must not read as a different instance.
    std::wstring upper = manifest;
    for (wchar_t &ch : upper)
        ch = static_cast<wchar_t>(towupper(ch));
    check(classify(upper, manifest) == BrowserCompanionHostState::Ready,
          "case difference does not read as a mismatch");
    const std::wstring parent = std::filesystem::path(manifest).parent_path().wstring() + L"\\";
    check(classify(parent + L".\\" + unique + L"-json", manifest) == BrowserCompanionHostState::Ready,
          "redundant path segment does not read as a mismatch");
    std::wstring slashed = manifest;
    for (wchar_t &ch : slashed)
        if (ch == L'\\')
            ch = L'/';
    check(classify(slashed, manifest) == BrowserCompanionHostState::Ready,
          "forward slashes do not read as a mismatch");

    // 8.3 short-name spelling. GetFullPathNameW is purely syntactic, so equality here depends
    // on the long-name resolution; the assertion only applies where the volume generates short
    // names at all (many modern volumes have 8.3 disabled).
    {
        std::vector<wchar_t> shortBuffer(MAX_PATH);
        const DWORD shortLength =
            GetShortPathNameW(manifest.c_str(), shortBuffer.data(), static_cast<DWORD>(shortBuffer.size()));
        if (shortLength > 0 && shortLength < shortBuffer.size()) {
            const std::wstring shortName(shortBuffer.data(), shortLength);
            if (shortName != manifest)
                check(classify(shortName, manifest) == BrowserCompanionHostState::Ready,
                      "8.3 short name does not read as a mismatch");
        }
    }

    // The status line composes a translated pattern at runtime, so a bad translation must not
    // throw out of the dialog proc.
    check(WideFormatCompanionStatus(L"Bridge: Chrome {} / Edge {}", L"ready", L"not registered") ==
              L"Bridge: Chrome ready / Edge not registered",
          "english status pattern composes both hosts");
    check(WideFormatCompanionStatus(
              L"\u6865\u63a5\uff1aChrome {} \u00b7 Edge {}", L"\u5c31\u7eea", L"\u672a\u6ce8\u518c") ==
              L"\u6865\u63a5\uff1aChrome \u5c31\u7eea \u00b7 Edge \u672a\u6ce8\u518c",
          "chinese status pattern composes both hosts");
    check(WideFormatCompanionStatus(L"no placeholders", L"ready", L"ready") == L"no placeholders",
          "pattern without placeholders is passed through");
    check(WideFormatCompanionStatus(L"bad {", L"ready", L"ready") == L"bad {",
          "malformed pattern falls back instead of throwing");
    check(WideFormatCompanionStatus(nullptr, L"ready", L"ready") == L"Bridge: Chrome ready / Edge ready",
          "missing pattern uses the built-in fallback");

    return failures;
}

int RunBrowserCompanionContracts() {
    using namespace browser_companion;
    int failures = 0;
    const auto expect = [&](bool passed, const char *name) {
        if (!passed) {
            ++failures;
            std::cerr << "FAIL companion " << name << '\n';
        }
    };
    const auto parses = [](const Json &message) { return ParseMessage(message.dump()).has_value(); };
    expect(parses(Hello()), "valid hello");
    expect(parses(ResultMessage()), "valid result");
    expect(!ParseMessage(""), "empty message rejected");
    expect(!ParseMessage(std::string("\xc3(", 2)), "invalid UTF-8 rejected");
    expect(!ParseMessage("{invalid}"), "invalid JSON rejected");
    expect(!ParseMessage("[]"), "non-object JSON rejected");
    for (const Json version : {Json(0), Json(2), Json("1"), Json(1.0), Json(nullptr)}) {
        auto message = Hello();
        message["version"] = version;
        expect(!parses(message), "unknown or mistyped version rejected");
    }
    for (const Json type : {Json("execute"), Json(1), Json(nullptr)}) {
        auto message = Hello();
        message["type"] = type;
        expect(!parses(message), "unknown or mistyped command rejected");
    }
    for (const char *field : {"sessionId", "requestId", "connectionId", "documentId"}) {
        for (const Json identity : {Json(""), Json(1), Json(std::string(129, 'x'))}) {
            auto message = ResultMessage();
            message[field] = identity;
            expect(!parses(message), "empty numeric or oversized identity rejected");
        }
        auto message = ResultMessage();
        message.erase(field);
        expect(!parses(message), "missing identity rejected");
    }
    auto large = Hello();
    large["padding"] = "";
    large["padding"] = std::string(kMaxMessageBytes - large.dump().size(), 'x');
    expect(parses(large), "maximum message accepted");
    large["padding"].get_ref<std::string &>().push_back('x');
    expect(!parses(large), "oversized message rejected");
    auto quoted = Hello();
    quoted["braces"] = std::string(100, '{') + "\\\"";
    expect(parses(quoted), "quoted braces do not count as nesting");
    expect(!ParseMessage("{\"version\":1,\"type\":\"hello\",\"connectionId\":\"a\",\"nested\":" + std::string(33, '[') +
                         "0" + std::string(33, ']') + "}"),
           "deep nesting rejected");
    for (const char *field : {"windowId", "tabId", "frameId"}) {
        for (const Json identity : {Json(-1), Json(2147483648ULL), Json("1"), Json(1.5)}) {
            auto message = ResultMessage();
            message[field] = identity;
            expect(!parses(message), "unsafe browser ID rejected");
        }
    }
    auto candidates = ResultMessage();
    const auto candidate = candidates["candidates"][0];
    candidates["candidates"] = Json::array();
    expect(!parses(candidates), "empty candidates rejected");
    for (size_t i = 0; i < kMaxCandidates; ++i)
        candidates["candidates"].push_back(candidate);
    expect(parses(candidates), "maximum candidate count accepted");
    candidates["candidates"].push_back(candidate);
    expect(!parses(candidates), "excess candidates rejected");
    for (const Json geometry : {Json(0), Json(2), Json("1")}) {
        auto message = ResultMessage();
        message["geometryVersion"] = geometry;
        expect(!parses(message), "unknown geometry generation rejected");
    }
    auto unavailable = Json{{"version", 1}, {"type", "unavailable"}, {"sessionId", "a"}, {"requestId", "1"}};
    expect(!parses(unavailable), "missing unavailable status rejected");
    unavailable["status"] = "target_unavailable";
    expect(parses(unavailable), "unavailable status accepted");

    {
        Pipe pipe;
        expect(pipe.read && pipe.write, "anonymous pipe created");
        expect(WriteMessage(pipe.write, Hello()), "framed write");
        const auto reply = ReadMessage(pipe.read);
        expect(reply && *reply == Hello(), "framed round trip");
        auto invalid = Hello();
        invalid["type"] = "execute";
        expect(!WriteMessage(pipe.write, invalid), "invalid outgoing message rejected");
        invalid = Hello();
        invalid["connectionId"] = std::string("\xc3(", 2);
        bool threw = false, wrote = false;
        try {
            wrote = WriteMessage(pipe.write, invalid);
        } catch (const std::exception &) {
            threw = true;
        }
        expect(!threw && !wrote, "invalid outgoing UTF-8 rejected without exception");
    }
    {
        Pipe pipe;
        const auto bytes = ResultMessage().dump();
        const auto header = Header(static_cast<uint32_t>(bytes.size()));
        bool wrote = true;
        std::jthread writer([&] {
            for (const char byte : header) {
                wrote = wrote && WriteBytes(pipe.write, std::span(&byte, 1));
                std::this_thread::sleep_for(1ms);
            }
            for (size_t i = 0; i < bytes.size(); i += 7) {
                wrote = wrote &&
                        WriteBytes(pipe.write, std::span(bytes).subspan(i, (std::min)(size_t{7}, bytes.size() - i)));
                std::this_thread::sleep_for(1ms);
            }
            pipe.CloseWriter();
        });
        const auto reply = ReadMessage(pipe.read);
        writer.join();
        expect(wrote && reply && *reply == ResultMessage(), "split header and body reconstructed");
    }
    for (const uint32_t size : {0u, static_cast<uint32_t>(kMaxMessageBytes + 1)}) {
        Pipe pipe;
        expect(WriteBytes(pipe.write, Header(size)), "invalid frame header written");
        pipe.CloseWriter();
        expect(!ReadMessage(pipe.read), "zero or excessive frame rejected");
    }
    for (const size_t headerBytes : {size_t{0}, size_t{2}, size_t{4}}) {
        Pipe pipe;
        const auto header = Header(10);
        expect(WriteBytes(pipe.write, std::span(header).first(headerBytes)), "partial frame written");
        if (headerBytes == 4)
            expect(WriteBytes(pipe.write, std::span("{}", 2)), "partial body written");
        pipe.CloseWriter();
        expect(!ReadMessage(pipe.read), "EOF within header or body rejected");
    }
    {
        Pipe pipe;
        std::stop_source cancel;
        cancel.request_stop();
        expect(!ReadMessage(pipe.read, INFINITE, cancel.get_token()), "cancelled read does not wait");
        expect(!WriteMessage(pipe.write, Hello(), INFINITE, cancel.get_token()), "cancelled write rejected");
    }

    {
        const auto name =
            std::wstring(L"\\\\.\\pipe\\ZenCrop.CompanionContract.") + std::to_wstring(GetCurrentProcessId());
        HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
        HANDLE client = server == INVALID_HANDLE_VALUE
                            ? INVALID_HANDLE_VALUE
                            : CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        expect(server != INVALID_HANDLE_VALUE && client != INVALID_HANDLE_VALUE, "overlapped pipe fixture created");
        if (server != INVALID_HANDLE_VALUE && client != INVALID_HANDLE_VALUE) {
            OVERLAPPED connect{};
            connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            const BOOL connected = ConnectNamedPipe(server, &connect);
            expect(connected || GetLastError() == ERROR_PIPE_CONNECTED, "overlapped fixture client connected");
            if (connect.hEvent)
                CloseHandle(connect.hEvent);
            expect(WriteMessage(client, Hello()), "synchronous client write");
            const auto message = ReadMessage(server, 1000, {}, PipeIo::Overlapped);
            expect(message && (*message)["type"] == "hello", "explicit overlapped read receives synchronous framing");
            std::optional<std::expected<Json, std::string>> cancelled;
            std::jthread reader(
                [&](std::stop_token stop) { cancelled = ReadMessage(server, INFINITE, stop, PipeIo::Overlapped); });
            const auto started = std::chrono::steady_clock::now();
            reader.request_stop();
            reader.join();
            expect(cancelled && !*cancelled && std::chrono::steady_clock::now() - started <= 250ms,
                   "infinite overlapped read cancels without magic timeout values");
        }
        if (client != INVALID_HANDLE_VALUE)
            CloseHandle(client);
        if (server != INVALID_HANDLE_VALUE)
            CloseHandle(server);
    }

    {
        Json window{{"id", 1},       {"left", 2560},   {"top", 0},
                    {"width", 1920}, {"height", 1080}, {"screen", Json::array({2560, 0, 4480, 1080})}};
        const RECT secondary{3840, 0, 5760, 1080};
        expect(MatchWindowBounds(window, RECT{2560, 0, 3840, 720}, secondary, secondary),
               "mixed DPI secondary uses its own origin and scale");
        window["left"] = -1270;
        window["top"] = 10;
        window["width"] = 1000;
        window["height"] = 600;
        window["screen"] = Json::array({-1280, 0, 0, 720});
        const RECT negativeWork{-1920, 0, 0, 1080};
        expect(MatchWindowBounds(window, {}, RECT{-1905, 15, -405, 915}, negativeWork),
               "negative monitor origin uses relative coordinates");
        window["left"] = 0;
        window["top"] = 0;
        window["screen"] = Json::array({0, 0, 2560, 1440});
        expect(MatchWindowBounds(window, {}, RECT{0, 0, 1250, 750}, RECT{0, 0, 3200, 1800}),
               "forced Chrome scale does not have to equal Windows DPI");
        expect(!MatchWindowBounds(window, {}, RECT{0, 0, 1250, 750}, RECT{0, 0, 3200, 1900}),
               "inconsistent monitor geometry rejected");
        for (const Json screen : {Json::array({0, 0, 0, 0}), Json::array({0, 0, 2560}),
                                  Json::array({0, 0, "bad", 1440}), Json::array({0, 0, 10000001, 1440})}) {
            window["screen"] = screen;
            expect(!MatchWindowBounds(window, {}, RECT{0, 0, 1250, 750}, RECT{0, 0, 3200, 1800}),
                   "unsafe monitor metadata rejected");
        }
        window.erase("screen");
        expect(MatchWindowBounds(window, RECT{0, 0, 1000, 600}, RECT{0, 0, 1500, 900}, {}),
               "older companion logical bounds still supported");
        expect(MatchWindowBounds(window, {}, RECT{0, 0, 1250, 750}, {}),
               "older companion forced UI scale still supported");
    }

    const RECT viewport{-1900, -900, -1000, 0};
    const POINT point{-1870, -850};
    const RECT expected{-1880, -860, -1779, -819};
    const auto mapped = MapCandidate(Json::array({10.25, 20.25, 60.25, 40.25}), viewport, 2, point);
    expect(mapped && EqualRect(&*mapped, &expected), "negative-screen mapping and outward rounding");
    const auto clipped = MapCandidate(Json::array({-100, -100, 100, 100}), viewport, 2, point);
    const RECT clipping{-1900, -900, -1700, -700};
    expect(clipped && EqualRect(&*clipped, &clipping), "rectangle clipped to viewport");
    for (const Json rect :
         {Json::array(), Json::array({0, 0, 1}), Json::array({0, "x", 100, 100}), Json::array({0, 0, 0, 100}),
          Json::array({100, 0, 0, 100}), Json::array({0, 0, 5, 5}), Json::array({0, 0, 1000001, 100}),
          Json::array({0, 0, std::numeric_limits<double>::infinity(), 100})}) {
        expect(!MapCandidate(rect, viewport, 1, POINT{-1900, -900}), "malformed unsafe or empty geometry rejected");
    }
    for (const double scale : {0.0, 0.24, 16.01, std::numeric_limits<double>::quiet_NaN()}) {
        expect(!MapCandidate(Json::array({0, 0, 100, 100}), viewport, scale, point), "unsafe scale rejected");
    }
    expect(!MapCandidate(Json::array({0, 0, 100, 100}), viewport, 1, POINT{-1500, -500}),
           "candidate outside query rejected");
    expect(!MapCandidate(Json::array({0, 0, 100, 100}), RECT{0, 0, 0, 0}, 1, POINT{}), "empty viewport rejected");
    expect(IsBrowserProcessName(L"chrome.exe") && IsBrowserProcessName(L"msedge.exe"), "Chrome and Edge classified");
    expect(IsBrowserProcessName(L"firefox.exe"), "Firefox protected without DOM support");
    expect(!IsBrowserProcessName(L"electron.exe") && !IsBrowserProcessName(L"Code.exe"),
           "embedded Chromium stays native path");
    expect(!IsBrowserProcessName(L"renamed-browser.exe"), "unknown browser identity not guessed");

    {
        // Offscreen Win32 geometry only; no browser is opened or user window touched.
        const auto oldDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW widgetClass{};
        widgetClass.lpfnWndProc = DefWindowProcW;
        widgetClass.hInstance = instance;
        widgetClass.lpszClassName = L"Chrome_RenderWidgetHostHWND";
        const ATOM atom = RegisterClassW(&widgetClass);
        HWND window =
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Companion geometry fixture",
                            WS_POPUP | WS_VISIBLE, -30000, -30000, 500, 400, nullptr, nullptr, instance, nullptr);
        HWND widget = CreateWindowExW(0, widgetClass.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 60, 500, 340, window,
                                      nullptr, instance, nullptr);
        expect(atom && window && widget, "offscreen browser geometry fixture created");
        if (atom && window && widget) {
            DomDetector detector;
            detector.Configure("browser-chrome-miss", true, {});
            const RECT viewport = FindBrowserViewport(window);
            expect(!IsRectEmpty(&viewport), "offscreen browser viewport measured");
            const POINT chrome{viewport.left + 20, viewport.top - 20};
            const RECT chromeRect = detector.Inspect(window, viewport, chrome);
            expect(IsRectEmpty(&chromeRect) && detector.Status() == "outside_viewport",
                   "browser chrome is classified before a DOM query");
            const auto session = detector.Session();
            const POINT inside{viewport.left + 20, viewport.top + 20};
            detector.Inspect(window, viewport, inside);
            expect(detector.Status() == "not_connected" && detector.Session() == session,
                   "entering webpage after chrome retries the same session instead of invalidating geometry");
            detector.Inspect(window, viewport, chrome);
            expect(detector.Status() == "outside_viewport", "repeated chrome hover stays a temporary miss");
        }
        if (window)
            DestroyWindow(window);
        if (atom)
            UnregisterClassW(widgetClass.lpszClassName, instance);
        SetThreadDpiAwarenessContext(oldDpi);
    }

    {
        std::mutex mutex;
        std::condition_variable cv;
        std::optional<SmartDetectorThread::Result> received;
        SmartDetectorThread detector;
        detector.OnResultReady = [&](SmartDetectorThread::Result result) {
            {
                std::lock_guard lock(mutex);
                received = std::move(result);
            }
            cv.notify_one();
        };
        detector.Start();
        const auto next = [&]() -> std::optional<SmartDetectorThread::Result> {
            {
                std::lock_guard lock(mutex);
                received.reset();
            }
            detector.AsyncGetRectByPoint(POINT{-10000000, -10000000}, nullptr);
            std::unique_lock lock(mutex);
            if (!cv.wait_for(lock, 2s, [&] { return received.has_value(); }))
                return std::nullopt;
            return received;
        };
        const auto first = next();
        expect(first && detector.IsCurrent(*first), "live detector generation accepted");
        detector.EndSession();
        if (first)
            expect(!detector.IsCurrent(*first), "late result rejected after end");
        const auto second = next();
        expect(first && second && second->generation > first->generation && second->request > first->request &&
                   detector.IsCurrent(*second),
               "next session receives fresh generation");
        if (second) {
            auto missingWindow = *second;
            missingWindow.browser = true;
            expect(!detector.IsCurrent(missingWindow), "browser reply for destroyed target rejected");
        }
        const auto stopStart = std::chrono::steady_clock::now();
        detector.Stop();
        expect(!detector.IsRunning(), "idle worker stopped");
        expect(std::chrono::steady_clock::now() - stopStart <= 250ms, "idle cancellation within budget");
        if (second)
            expect(!detector.IsCurrent(*second), "stopped session result rejected");
    }
    wchar_t probePath[32768]{};
    const DWORD pathSize = GetEnvironmentVariableW(L"ZENCROP_BROWSER_COMPANION_PROBE", probePath,
                                                   static_cast<DWORD>(std::size(probePath)));
    if (pathSize && pathSize < std::size(probePath))
        failures += RunRuntimeProbe(std::filesystem::path(probePath));
    else if (pathSize)
        expect(false, "runtime probe path exceeds capacity");
    failures += RunBrowserCompanionHostRegistrationContracts();
    failures += RunCompanionRegistrationSubprocess();
    // Direct connection contracts run before the live probe: they need the wire free
    // and no browser-owned window, and the probe drives a real browser afterwards.
    failures += RunDirectConnectionContracts();
    if (!failures)
        std::cout << "PASS browser companion protocol, geometry and session contracts\n";
    return failures;
}
