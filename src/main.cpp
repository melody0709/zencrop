#include "Utils.h"
#include "OverlayWindow.h"
#include "ReparentWindow.h"
#include "ThumbnailWindow.h"
#include "ViewportWindow.h"
#include "AlwaysOnTop.h"
#include "Settings.h"
#include "Strings.h"
#include "OcrEngine.h"
#include "LlamaServerManager.h"
#include "OcrEngine_PaddleOCR_Local.h"
#include "OcrEngine_PaddleOCR_Doc.h"
#include "OcrResultWindow.h"
#include "Version.generated.h"
#include "OcrProgressWindow.h"
#include "OcrCopyToastWindow.h"
#include "OcrDashboardWindow.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "BatchOcrImageLinks.h"
#include "MiniHttpServer.h"
#include "ScreenshotSession.h"
#include "AppMessages.h"
#include "image/BitmapCodec.h"
#include "screenshot/ScreenshotUtils.h"
#include "selection/SelectionTranslationController.h"
#include "core/WideJsonUtils.h"
#include "core/WideStringUtils.h"
#include "core/NarrowStringUtils.h"
#include "core/OcrModelRegistry.h"
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <objbase.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <stdio.h>
#include <fstream>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TITLEBAR 1002
#define ID_TRAY_RELEASE 1003
#define ID_TRAY_AOT_SETTINGS 1004
#define ID_TRAY_OCR_DASHBOARD 1007

#define ZENCROP_RELEASE_URL L"https://github.com/melody0709/zencrop/releases"
#define ZENCROP_MUTEX_NAME L"Global\\ZenCrop"

constexpr UINT kTrayIconId = 1;

bool g_showTitlebar = false;
HotkeySettings g_hotkeys;

enum class CropMode { Reparent, Thumbnail, Viewport, Ocr };

std::vector<std::shared_ptr<ReparentWindow>> g_reparents;
std::vector<std::shared_ptr<ThumbnailWindow>> g_thumbnails;
std::vector<std::shared_ptr<ViewportWindow>> g_viewports;
std::vector<std::shared_ptr<OcrResultWindow>> g_ocrResults;
std::shared_ptr<OverlayWindow> g_overlay;
std::unique_ptr<selection::SelectionTranslationController>
    g_selectionTranslation;

HWND g_mainHwnd = nullptr;

// AppMessages.h 声明的访问器实现（H4 硬约束：避免 extern 全局变量）
HWND GetAppMainHwnd() { return g_mainHwnd; }
void SetAppMainHwnd(HWND hwnd) {
    g_mainHwnd = hwnd;
}

static void ConfigureOcrEngineServices() {
    OcrEngineFactory::ConfigurePaddleVlServer(&LlamaServerManager::Instance());
}

// 全局 OCR progressId 生成器（P1.2 修复：单一 id 空间，避免浮层和 Dashboard 各自计数撞车）
static std::atomic<uint64_t> g_nextOcrProgressId{1};
uint64_t NextOcrProgressId() { return g_nextOcrProgressId.fetch_add(1, std::memory_order_relaxed); }

// Stage3 3-F: composition-root OCR progress facade (screenshot↛ocr_ui headers).
uint64_t ShowAppOcrProgress(
    const std::wstring& engineLabel,
    const RECT* anchorRect,
    int ocrFontSize,
    const std::wstring& imagePath,
    bool allowProgressUi)
{
    // Dashboard open → ActiveWorkStrip; else floating OcrProgressWindow.
    // Fast engines still get progressId when Dashboard open (source association).
    const bool fastEngine = IsFastOcrEngine(engineLabel);
    if (OcrDashboardWindow::IsOpen()) {
        return OcrDashboardWindow::ShowExternalOcrProgress(
            engineLabel,
            imagePath,
            allowProgressUi && !fastEngine);
    }
    if (!allowProgressUi || fastEngine) {
        return 0;
    }
    return OcrProgressWindow::Instance().Show(
        GetAppMainHwnd(),
        anchorRect,
        engineLabel,
        ocrFontSize);
}

void CloseAppOcrProgress(uint64_t progressId)
{
    if (progressId == 0) return;
    OcrProgressWindow::Instance().Close(progressId);
    if (OcrDashboardWindow::IsOpen()) {
        OcrDashboardWindow::HideExternalOcrProgress(progressId);
    }
}

std::wstring GetArgValue(const std::vector<std::wstring>& args, const std::wstring& name) {
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (WideEqualsNoCase(args[i], name)) return args[i + 1];
    }
    return L"";
}

std::vector<std::wstring> GetCommandLineArgs() {
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return args;
    for (int i = 0; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

bool HasArg(const std::vector<std::wstring>& args, const std::wstring& name) {
    for (const auto& arg : args) {
        if (WideEqualsNoCase(arg, name)) return true;
    }
    return false;
}

static void WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return;
    std::string utf8((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), len, nullptr, nullptr);
    std::ofstream out(path, std::ios::binary);
    if (out.is_open()) out.write(utf8.data(), (std::streamsize)utf8.size());
}

static int RunModelRegistryDryRun(const std::vector<std::wstring>& args) {
    const std::wstring outputPath = GetArgValue(args, L"--model-dry-run");
    if (outputPath.empty()) return 2;
    const OcrSettings settings = LoadOcrSettings();
    const OcrModelRegistryPlan plan = OcrModelRegistryBuildPlan(
        settings, OcrModelRegistryProcessDir());
    WriteUtf8File(outputPath, OcrModelRegistryDryRunJson(OcrModelRegistryDryRun(plan)));
    return 0;
}

static HBITMAP LoadBitmapFromFile(const std::wstring& path) {
    return ImageCodec::LoadHBitmapFromFile(path);
}

static std::wstring OcrOutputToJson(const std::wstring& mode, const OcrOutput& result) {
    // OWN-127: pure int labels (WideStringUtils).
    return L"{\n"
        L"  \"mode\": \"" + EscapeJsonString(mode) + L"\",\n"
        L"  \"success\": " + std::wstring(WideJsonBoolLiteral(result.success)) + L",\n"
        L"  \"elapsedMs\": " + WideFormatIntLabel(static_cast<int>(result.elapsedMs)) + L",\n"
        L"  \"text\": \"" + EscapeJsonString(result.text) + L"\",\n"
        L"  \"error\": \"" + EscapeJsonString(result.error) + L"\"\n"
        L"}\n";
}

static std::wstring OcrRunsToJson(const std::wstring& mode, const std::vector<OcrOutput>& runs) {
    // OWN-127: pure int labels (WideStringUtils).
    std::wstring json = L"{\n"
        L"  \"mode\": \"" + EscapeJsonString(mode) + L"\",\n"
        L"  \"repeat\": " + WideFormatIntLabel(static_cast<int>(runs.size())) + L",\n"
        L"  \"runs\": [\n";
    for (size_t i = 0; i < runs.size(); i++) {
        const auto& result = runs[i];
        json += L"    {\n"
            L"      \"success\": " + std::wstring(WideJsonBoolLiteral(result.success)) + L",\n"
            L"      \"elapsedMs\": " + WideFormatIntLabel(static_cast<int>(result.elapsedMs)) + L",\n"
            L"      \"text\": \"" + EscapeJsonString(result.text) + L"\",\n"
            L"      \"error\": \"" + EscapeJsonString(result.error) + L"\"\n"
            L"    }";
        if (i + 1 < runs.size()) json += L",";
        json += L"\n";
    }
    json += L"  ]\n}\n";
    return json;
}

static int RunOcrCliTest(const std::vector<std::wstring>& args) {
    std::wstring imagePath = GetArgValue(args, L"--ocr-test");
    std::wstring outputPath = GetArgValue(args, L"--ocr-test-output");
    if (imagePath.empty()) return 2;
    if (outputPath.empty()) outputPath = imagePath + L".ocr.json";
    // OWN-93: pure int parse (WideStringUtils); clamp CLI repeat.
    int repeat = WideParseClampedIntToken(GetArgValue(args, L"--ocr-test-repeat"), 1, 1, 20);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WSADATA wsaData = {};
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    OcrOutput result;
    std::wstring mode = GetArgValue(args, L"--ocr-mode");
    if (mode.empty()) mode = LoadOcrSettings().mode;

    HBITMAP hBitmap = LoadBitmapFromFile(imagePath);
    if (!hBitmap) {
        result.error = L"Failed to load OCR test image: " + imagePath;
        WriteUtf8File(outputPath, OcrOutputToJson(mode, result));
        Gdiplus::GdiplusShutdown(gdiplusToken);
        WSACleanup();
        CoUninitialize();
        return 3;
    }
    DeleteObject(hBitmap);

    auto engine = OcrEngineFactory::Create(mode);
    if (!engine || (!engine->IsAvailable() && mode != L"ppocrv6_onnx")) {
        result.error = L"OCR engine is not available: " + mode;
        WriteUtf8File(outputPath, OcrOutputToJson(mode, result));
        Gdiplus::GdiplusShutdown(gdiplusToken);
        WSACleanup();
        CoUninitialize();
        return 4;
    }

    DWORD waitMs = (std::max)(LoadOcrSettings().timeoutMs + 30000, 120000);
    std::vector<OcrOutput> runs;
    runs.reserve((size_t)repeat);
    for (int i = 0; i < repeat; i++) {
        result = {};
        hBitmap = LoadBitmapFromFile(imagePath);
        if (!hBitmap) {
            result.error = L"Failed to load OCR test image: " + imagePath;
            runs.push_back(result);
            break;
        }

        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        engine->Recognize(hBitmap, [&result, done](OcrOutput out) {
            result = out;
            SetEvent(done);
        });

        DWORD wait = WaitForSingleObject(done, waitMs);
        CloseHandle(done);
        if (wait != WAIT_OBJECT_0) {
            result.success = false;
            result.error = L"OCR test timed out.";
        }
        runs.push_back(result);
        if (!result.success) break;
    }

    WriteUtf8File(outputPath, repeat == 1 ? OcrOutputToJson(mode, runs.empty() ? result : runs[0]) : OcrRunsToJson(mode, runs));
    OcrEnginePaddleDoc::GlobalCleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    WSACleanup();
    CoUninitialize();
    return (!runs.empty() && std::all_of(runs.begin(), runs.end(), [](const OcrOutput& r) { return r.success; })) ? 0 : 5;
}

bool ShouldOpenOcrDashboardOnStartup() {
    wchar_t envBuf[16] = {};
    DWORD envLen = GetEnvironmentVariableW(L"ZENCROP_OPEN_OCR_DASHBOARD", envBuf, 16);
    if (envLen > 0 && WideEqualsNoCase(std::wstring(envBuf), L"1")) return true;

    const wchar_t* cmd = GetCommandLineW();
    // OWN-114: pure substring contains (WideStringUtils).
    return WideContains(cmd, L"--ocr-dashboard");
}

HBITMAP CaptureScreenRect(RECT rect) {
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return nullptr;

    HDC hScreen = GetDC(nullptr);
    if (!hScreen) return nullptr;

    HDC hMemDC = CreateCompatibleDC(hScreen);
    if (!hMemDC) {
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
    if (!hBitmap) {
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap);
    BOOL copied = BitBlt(hMemDC, 0, 0, width, height, hScreen, rect.left, rect.top, SRCCOPY);
    if (hOldBmp) SelectObject(hMemDC, hOldBmp);

    DeleteDC(hMemDC);
    ReleaseDC(nullptr, hScreen);

    if (!copied) {
        DeleteObject(hBitmap);
        return nullptr;
    }

    return hBitmap;
}

void RemoveViewportForTarget(HWND target) {
    g_viewports.erase(
        std::remove_if(g_viewports.begin(), g_viewports.end(),
            [target](const std::shared_ptr<ViewportWindow>& vw) {
                return !vw || vw->GetTargetWindow() == target;
            }),
        g_viewports.end());
}

void StartCrop(CropMode mode, const std::wstring& ocrRoute = L"current") {
    HWND target = GetForegroundWindow();
    if (!target || target == g_mainHwnd) return;

    wchar_t className[64] = {};
    GetClassNameW(target, className, 64);
    // OWN-114: pure substring contains (WideStringUtils).
    if (WideContains(className, L"ZenCrop.")) return;

    // OCR sessions enable Adjust-state Shift+C silent copy; other crop modes do not.
    const bool enableSilentOcrCopy = (mode == CropMode::Ocr);
    g_overlay = std::make_shared<OverlayWindow>(
        target,
        [mode, ocrRoute](HWND t, RECT r, HBITMAP frozenCrop, bool copyOnly) {
        if (r.right - r.left > 10 && r.bottom - r.top > 10) {
            bool cropOnTop = LoadOverlaySettings().cropOnTop;
            if (mode == CropMode::Reparent) {
                if (IsXamlOrDCompWindow(t)) {
                    // Modern apps (UWP/WinUI/XAML/DComp) don't reparent well.
                    // Fall back to Viewport mode which crops the original window.
                    RemoveViewportForTarget(t);
                    auto vw = std::make_shared<ViewportWindow>(t, r, cropOnTop);
                    if (vw->IsValid()) {
                        g_viewports.push_back(vw);
                    }
                } else {
                    auto rw = std::make_shared<ReparentWindow>(t, r, g_showTitlebar);
                    if (cropOnTop) AlwaysOnTopManager::Instance().PinWindow(rw->GetHostWindow());
                    g_reparents.push_back(rw);
                }
            } else if (mode == CropMode::Thumbnail) {
                auto tw = std::make_shared<ThumbnailWindow>(t, r, g_showTitlebar);
                if (cropOnTop) AlwaysOnTopManager::Instance().PinWindow(tw->GetHostWindow());
                g_thumbnails.push_back(tw);
            } else if (mode == CropMode::Viewport) {
                RemoveViewportForTarget(t);
                auto vw = std::make_shared<ViewportWindow>(t, r, cropOnTop);
                if (vw->IsValid()) {
                    g_viewports.push_back(vw);
                }
            } else if (mode == CropMode::Ocr) {
                std::wstring route = NormalizeOcrRoute(ocrRoute);
                PostMessage(GetAppMainHwnd(), WM_APP_OVERLAY_RESET, 0, 0);
                HBITMAP hBmp = frozenCrop ? (HBITMAP)CopyImage(frozenCrop, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION) : CaptureScreenRect(r);
                if (hBmp) {
                    // Shift+C silent copy: same session route, clipboard + toast only.
                    // Reuses screenshot Copy OCR completion (no result window / history / image cache).
                    if (copyOnly) {
                        OutputDebugStringW((L"[OCR] Silent copy route=" + route + L"\n").c_str());
                        ScreenshotSession::Instance().StartCopyOcrText(
                            GetAppMainHwnd(), r, hBmp, route);
                        // StartCopyOcrText duplicates the bitmap; we still own hBmp.
                        DeleteObject(hBmp);
                        return;
                    }

                    CleanOcrImageDir();
                    OcrSettings ocrSettings = LoadOcrSettings();

                    // Save to local file cache, grouped by day.
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    // OWN-109: pure OCR crop name format (WideStringUtils).
                    std::wstring name = WideFormatOcrCropFileName(st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
                    std::wstring destPath = GetOcrImageDateDir(st) + name;

                    {
                        Gdiplus::Bitmap bmp(hBmp, nullptr);
                        CLSID pngClsid;
                        UINT num = 0, size = 0;
                        Gdiplus::GetImageEncodersSize(&num, &size);
                        if (size > 0) {
                            std::vector<Gdiplus::ImageCodecInfo> encoders(size / sizeof(Gdiplus::ImageCodecInfo));
                            Gdiplus::GetImageEncoders(num, size, encoders.data());
                            for (UINT i = 0; i < num; i++) {
                                if (WideEquals(encoders[i].MimeType, L"image/png")) {
                                    pngClsid = encoders[i].Clsid;
                                    break;
                                }
                            }
                            bmp.Save(destPath.c_str(), &pngClsid, nullptr);
                        }
                    }

                    RECT cropRect = r;
                    OutputDebugStringW((L"[OCR] Starting route=" + route + L", mode=" + ocrSettings.mode + L"\n").c_str());
                    // H5 硬约束：用 SelectOcrEngineForRoute 替换原有 fallback 逻辑，拿 fallback 后的真实引擎显示名。
                    auto selection = SelectOcrEngineForRoute(ocrSettings, route);
                    auto engine = selection.engine;
                    // 引擎有效性检查（必须在 Show 之前，避免浮层已显示但 engine 为空导致崩溃）
                    if (!engine || (!engine->IsAvailable() && selection.displayLabel != L"ppocrv6_onnx")) {
                        DeleteObject(hBmp);  // engine 不可用时手动释放，避免 GDI 句柄泄漏
                        MessageBoxW(g_mainHwnd, L"OCR engine is not available.", L"OCR Error", MB_OK | MB_ICONERROR);
                        return;
                    }
                    // Stage3 3-F: composition-root OCR progress facade.
                    // Dashboard open → ActiveWorkStrip; else floating progress; fast engines skip UI.
                    // When Dashboard open + imagePath, always allocate progressId for source association.
                    const uint64_t progressId = ShowAppOcrProgress(
                        selection.displayLabel,
                        &r,
                        ocrSettings.ocrFontSize,
                        destPath,
                        true);
                    // H3 硬约束：hBmp 所有权归 engine（engine 内部 DeleteObject），调用方不要再释放。
                    // 生命周期：必须捕获 engine 保持 shared_ptr 引用，直到 Recognize 回调完成。
                    const std::wstring historyEngineMode = WideEqualsNoCase(
                        selection.displayLabel, L"paddle_local_doc")
                        ? L"paddle_local"
                        : DashboardNormalizeOcrMode(selection.displayLabel);
                    engine->Recognize(hBmp, [engine, cropRect, destPath, progressId, historyEngineMode](OcrOutput result) {
                        result.cropRect = cropRect;
                        result.imagePath = destPath;
                        result.engineMode = historyEngineMode;
                        OcrOutput* heapResult = new OcrOutput(result);
                        // H2 硬约束：progressId 放进 wParam，主线程按 id 关闭浮层。
                        if (!PostMessage(GetAppMainHwnd(), WM_APP_OCR_RESULT, (WPARAM)progressId, (LPARAM)heapResult)) {
                            delete heapResult;
                        }
                    });
                }
                return;
            }
        }
        PostMessage(GetAppMainHwnd(), WM_APP_OVERLAY_RESET, 0, 0);
    },
        enableSilentOcrCopy);

    g_overlay->Show();
}

bool RegisterOneHotkey(HWND hwnd, int id, const HotkeyConfig& hotkey) {
    if (hotkey.IsEmpty()) return true;
    if (!RegisterHotKey(hwnd, id, hotkey.Modifiers(), hotkey.key)) {
        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatHotkeyRegisterFailed(id).c_str());
        return false;
    }
    return true;
}

void UnregisterAppHotkeys(HWND hwnd) {
    for (int id = 1; id <= 9; id++) {
        UnregisterHotKey(hwnd, id);
    }
}

void RegisterAppHotkeys(HWND hwnd) {
    g_hotkeys = LoadHotkeySettings();
    RegisterOneHotkey(hwnd, 1, g_hotkeys.reparent);
    RegisterOneHotkey(hwnd, 2, g_hotkeys.thumbnail);
    RegisterOneHotkey(hwnd, 3, g_hotkeys.closeReparent);
    RegisterOneHotkey(hwnd, 4, g_hotkeys.alwaysOnTop);
    RegisterOneHotkey(hwnd, 5, g_hotkeys.viewport);
    RegisterOneHotkey(hwnd, 6, g_hotkeys.screenshot);
    RegisterOneHotkey(hwnd, 7, g_hotkeys.ocr);
    RegisterOneHotkey(hwnd, 8, g_hotkeys.ocrAlt);
    if (!RegisterOneHotkey(hwnd, 9, g_hotkeys.selectionTranslate) &&
        g_selectionTranslation) {
        g_selectionTranslation->NotifyHotkeyRegistrationFailed(
            g_hotkeys.selectionTranslate);
    }
}

static void StartAltOcrCrop() {
    StartCrop(CropMode::Ocr, LoadOcrSettings().altHotkeyRoute);
}

static void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    wcscpy_s(nid.szTip, S::TrayTip());
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

LRESULT CALLBACK TaskbarObserverWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static const UINT taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreatedMessage != 0 && msg == taskbarCreatedMessage) {
        HWND mainHwnd = GetAppMainHwnd();
        if (mainHwnd && IsWindow(mainHwnd)) {
            AddTrayIcon(mainHwnd);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        SetAppMainHwnd(hwnd);

        AddTrayIcon(hwnd);

        g_selectionTranslation =
            std::make_unique<selection::SelectionTranslationController>(hwnd);
        RegisterAppHotkeys(hwnd);

        return 0;
    }
    case WM_HOTKEY: {
        if (wParam == 1) {
            StartCrop(CropMode::Reparent);
        } else if (wParam == 2) {
            StartCrop(CropMode::Thumbnail);
        } else if (wParam == 3) {
            g_reparents.clear();
            g_viewports.clear();
        } else if (wParam == 4) {
            HWND target = GetForegroundWindow();
            if (target && target != g_mainHwnd) {
                HWND root = GetAncestor(target, GA_ROOT);
                if (root) {
                    wchar_t rootCn[64] = {};
                    GetClassNameW(root, rootCn, 64);
                    if (WideEquals(rootCn, L"ZenCrop.ReparentHost") ||
                    WideEquals(rootCn, L"ZenCrop.ThumbnailHost")) {
                        target = root;
                    }
                }
                wchar_t cn[64] = {};
                GetClassNameW(target, cn, 64);
                if (!WideEquals(cn, L"ZenCrop.Main") &&
                    !WideEquals(cn, L"ZenCrop.Overlay") &&
                    !WideEquals(cn, L"ZenCrop.AlwaysOnTopBorder")) {
                    AlwaysOnTopManager::Instance().TogglePin(target);
                }
            }
        } else if (wParam == 5) {
            StartCrop(CropMode::Viewport);
        } else if (wParam == 6) {
            ScreenshotSession::Instance().StartInteractive();
        } else if (wParam == 7) {
            StartCrop(CropMode::Ocr);
        } else if (wParam == 8) {
            StartAltOcrCrop();
        } else if (wParam == 9 && g_selectionTranslation) {
            g_selectionTranslation->Start(g_hotkeys.selectionTranslate);
        }
        return 0;
    }
    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            UINT titlebarFlag = g_showTitlebar ? MF_CHECKED : MF_UNCHECKED;
            AppendMenuW(hMenu, MF_STRING | titlebarFlag, ID_TRAY_TITLEBAR, S::MenuShowTitlebar());
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_OCR_DASHBOARD, S::IsChinese() ? L"OCR 工作台" : L"OCR Dashboard");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_AOT_SETTINGS, S::MenuSettings());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_RELEASE, S::MenuOpenRelease());
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"ZenCrop v" ZENCROP_PRODUCT_VERSION_W);
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, S::MenuExit());

            SetForegroundWindow(hwnd);

            HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);
            int taskbarTop = mi.rcWork.bottom;

            UINT flags = TPM_LEFTALIGN;
            if (pt.y > taskbarTop - 10) {
                flags = TPM_BOTTOMALIGN;
                pt.y = taskbarTop;
            }

            TrackPopupMenu(hMenu, flags, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        } else if (LOWORD(wParam) == ID_TRAY_TITLEBAR) {
            g_showTitlebar = !g_showTitlebar;
            GeneralSettings gs = LoadGeneralSettings();
            gs.showTitlebar = g_showTitlebar;
            SaveGeneralSettings(gs);
        } else if (LOWORD(wParam) == ID_TRAY_RELEASE) {
            ShellExecuteW(nullptr, L"open", ZENCROP_RELEASE_URL, nullptr, nullptr, SW_SHOWNORMAL);
        } else if (LOWORD(wParam) == ID_TRAY_OCR_DASHBOARD) {
            OcrDashboardWindow::ShowInstance();
        } else if (LOWORD(wParam) == ID_TRAY_AOT_SETTINGS) {
            ShowSettingsDialog(hwnd);
            AlwaysOnTopManager::Instance().UpdateSettings();
            UnregisterAppHotkeys(hwnd);
            RegisterAppHotkeys(hwnd);
        }
        return 0;
    }
    case WM_APP_OCR_RESULT: {
        // Stage3 3-F: composition-root close (Progress + Dashboard hide by id).
        // Dashboard Complete/Fail also hide; double-hide is id-safe.
        const uint64_t progressId = (uint64_t)wParam;
        CloseAppOcrProgress(progressId);

        OcrOutput* result = (OcrOutput*)lParam;

        std::vector<std::wstring> transientOwnedFiles;
        if (result->success && !result->embeddedAssets.empty()) {
            BatchOcrImageLinkRewriteResult materialized =
                MaterializeTransientOcrEmbeddedAssets(
                    result->text, result->imagePath, result->embeddedAssets);
            if (!materialized.error.empty()) {
                result->success = false;
                result->error = materialized.error;
            } else {
                result->text = std::move(materialized.markdown);
                transientOwnedFiles = std::move(materialized.ownedFiles);
            }
        }

        // OWN-116: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatOcrResultReceived(
            result->success ? 1 : 0, result->text.length(), result->error.length()).c_str());

        if (result->success && !result->text.empty()) {
            bool autoCopied = Screenshot::CopyTextToClipboard(hwnd, result->text);
            if (autoCopied) {
                OutputDebugStringA("[OCR] Copied to clipboard\n");
            }

            SYSTEMTIME st;
            GetLocalTime(&st);
            // OWN-109: pure date/time format (WideStringUtils).
            std::wstring timeBuf = WideFormatDateTimeParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            OcrDashboardHistoryItem item =
                DashboardBuildCaptureHistoryItem(*result, timeBuf);
            item.recordKind = L"TransientPayload";
            item.ownedCacheFiles = std::move(transientOwnedFiles);
            item.ownedCacheFiles.push_back(result->imagePath);

            // If Dashboard is open, add to it; otherwise show floating result window
            if (OcrDashboardWindow::IsOpen()) {
                OutputDebugStringA("[OCR] Adding to dashboard...\n");
                OcrDashboardWindow::CompleteExternalOcr(progressId, item);
            } else {
                // Save silently for Dashboard to load later.
                OcrDashboardWindow::SaveToHistoryFile(item);
                OutputDebugStringA("[OCR] Creating original single result window...\n");
                OcrSettings ocrSettings = LoadOcrSettings();
                auto ow = std::make_shared<OcrResultWindow>(result->text, result->cropRect, autoCopied, g_showTitlebar, ocrSettings.ocrFontSize, result->elapsedMs, ocrSettings.resultOnTop);
                if (ow->IsValid()) {
                    g_ocrResults.push_back(ow);
                    OutputDebugStringA("[OCR] Window created successfully\n");
                } else {
                    OutputDebugStringA("[OCR] Window creation failed!\n");
                }
            }
        } else if (!result->success) {
            if (OcrDashboardWindow::IsOpen()) {
                OcrDashboardWindow::FailExternalOcr(
                    progressId,
                    result->error,
                    result->elapsedMs);
            }
            OutputDebugStringA("[OCR] OCR failed, showing error\n");
            MessageBoxW(hwnd, result->error.c_str(), L"OCR Error", MB_OK | MB_ICONERROR);
        } else {
            if (OcrDashboardWindow::IsOpen()) {
                OcrDashboardWindow::FailExternalOcr(
                    progressId,
                    L"No text recognized",
                    result->elapsedMs);
            }
            OutputDebugStringA("[OCR] Empty result\n");
            MessageBoxW(hwnd, L"No text recognized", L"OCR Result", MB_OK | MB_ICONINFORMATION);
        }
        delete result;
        return 0;
    }
    case WM_APP_SCREENSHOT_OCR_DONE: {
        // Stage3 3-F: composition-root close (Progress + Dashboard hide by id).
        CloseAppOcrProgress((uint64_t)wParam);

        OcrOutput* result = (OcrOutput*)lParam;
        if (!result) return 0;

        if (result->success && !result->embeddedAssets.empty()) {
            result->text = StripOcrEmbeddedAssetMarkup(
                result->text,
                result->embeddedAssets);
        }

        if (result->success && !result->text.empty()) {
            if (!Screenshot::CopyTextToClipboard(hwnd, result->text)) {
                MessageBoxW(hwnd, L"OCR succeeded, but copying text failed.", L"Copy OCR Text", MB_OK | MB_ICONERROR);
            } else {
                OcrCopyToastWindow::Instance().Show();
            }
        } else if (!result->success) {
            MessageBoxW(hwnd, result->error.c_str(), L"Copy OCR Text", MB_OK | MB_ICONERROR);
        } else {
            MessageBoxW(hwnd, L"No text recognized.", L"Copy OCR Text", MB_OK | MB_ICONINFORMATION);
        }
        delete result;
        return 0;
    }
    case WM_APP_SCREENSHOT_TRANSLATION_OCR_DONE: {
        ScreenshotSession::Instance().HandleTranslationOcrDone(
            static_cast<uint64_t>(wParam), reinterpret_cast<OcrOutput*>(lParam));
        return 0;
    }
    case WM_APP_SCREENSHOT_TRANSLATION_DONE: {
        ScreenshotSession::Instance().HandleTranslationDone(
            static_cast<uint64_t>(wParam),
            reinterpret_cast<translation::TranslationResult*>(lParam));
        return 0;
    }
    case WM_APP_DASHBOARD_TRANSLATION_DONE: {
        OcrDashboardWindow::HandleTranslationDone(
            static_cast<uint64_t>(wParam),
            reinterpret_cast<translation::TranslationResult*>(lParam));
        return 0;
    }
    case WM_APP_SELECTION_TEXT_ACQUIRED: {
        if (g_selectionTranslation) {
            g_selectionTranslation->HandleAcquisitionResult(
                static_cast<uint64_t>(wParam),
                reinterpret_cast<selection::SelectionAcquisitionResult*>(
                    lParam));
        } else {
            delete reinterpret_cast<selection::SelectionAcquisitionResult*>(
                lParam);
        }
        return 0;
    }
    case WM_APP_SELECTION_TRANSLATION_DONE: {
        if (g_selectionTranslation) {
            g_selectionTranslation->HandleTranslationResult(
                static_cast<uint64_t>(wParam),
                reinterpret_cast<translation::TranslationResult*>(lParam));
        } else {
            delete reinterpret_cast<translation::TranslationResult*>(lParam);
        }
        return 0;
    }
    case WM_APP_OVERLAY_RESET: {
        // Deferred overlay destruction: the crop callback (m_onCropped) used to call
        // g_overlay.reset() inline, which destroyed the OverlayWindow while its
        // MessageHandler was still on the call stack (WM_APP → m_onCropped → reset
        // → ~OverlayWindow → DestroyWindow). Postponing to the next message loop
        // iteration ensures MessageHandler has fully returned before the object
        // is destroyed.
        g_overlay.reset();
        return 0;
    }
    case WM_CLOSE: {
        RemoveTrayIcon(hwnd);
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY: {
        UnregisterAppHotkeys(hwnd);
        if (g_selectionTranslation) {
            g_selectionTranslation->Shutdown();
            g_selectionTranslation.reset();
        }
        ScreenshotSession::Instance().Shutdown();
        // Shutdown drains the translation WM_APP payloads while this HWND is
        // still valid. Publish null immediately afterwards so a late worker
        // cannot target a destroyed composition-root window.
        SetAppMainHwnd(nullptr);
        AlwaysOnTopManager::Instance().UnpinAll();
        MiniHttpServer::Instance().Stop();
        LlamaServerManager::Instance().GlobalShutdown();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    std::vector<std::wstring> args = GetCommandLineArgs();
    if (HasArg(args, L"--model-dry-run")) {
        return RunModelRegistryDryRun(args);
    }
    if (HasArg(args, L"--ocr-test")) {
        return RunOcrCliTest(args);
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, ZENCROP_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, S::AlreadyRunning(), S::AppName(), MB_OK | MB_ICONINFORMATION);
        CloseHandle(hMutex);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    S::InitLanguage();
    g_showTitlebar = LoadGeneralSettings().showTitlebar;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // OWN-105: publish dual-write service path seed (legacy loaders remain authority).
    ConfigureOcrEngineServices();

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    MiniHttpServer::Instance().Start(28080);

    const wchar_t* className = L"ZenCrop.Main";
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = MainWndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = className;
    RegisterClassExW(&wcex);

    g_mainHwnd = CreateWindowExW(0, className, L"ZenCrop", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    // TaskbarCreated is broadcast only to top-level windows. Keep the main
    // message-only window for internal routing and use this invisible popup
    // solely to observe Explorer/taskbar recreation.
    const wchar_t* taskbarObserverClassName = L"ZenCrop.TaskbarObserver";
    WNDCLASSEXW taskbarObserverClass = { sizeof(taskbarObserverClass) };
    taskbarObserverClass.lpfnWndProc = TaskbarObserverWndProc;
    taskbarObserverClass.hInstance = hInstance;
    taskbarObserverClass.lpszClassName = taskbarObserverClassName;
    RegisterClassExW(&taskbarObserverClass);
    HWND taskbarObserverHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        taskbarObserverClassName,
        L"",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (ShouldOpenOcrDashboardOnStartup()) {
        OcrDashboardWindow::ShowInstance();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        g_reparents.erase(
            std::remove_if(g_reparents.begin(), g_reparents.end(),
                [](const std::shared_ptr<ReparentWindow>& rw) { return !rw->IsValid(); }),
            g_reparents.end());

        g_thumbnails.erase(
            std::remove_if(g_thumbnails.begin(), g_thumbnails.end(),
                [](const std::shared_ptr<ThumbnailWindow>& tw) { return !tw->IsValid(); }),
            g_thumbnails.end());

        g_viewports.erase(
            std::remove_if(g_viewports.begin(), g_viewports.end(),
                [](const std::shared_ptr<ViewportWindow>& vw) { return !vw->IsValid(); }),
            g_viewports.end());

        g_ocrResults.erase(
            std::remove_if(g_ocrResults.begin(), g_ocrResults.end(),
                [](const std::shared_ptr<OcrResultWindow>& ow) { return !ow->IsValid(); }),
            g_ocrResults.end());

        ScreenshotSession::Instance().CleanupInvalid();
        if (g_selectionTranslation) {
            g_selectionTranslation->CleanupInvalid();
        }
        AlwaysOnTopManager::Instance().CleanupInvalid();
    }

    if (taskbarObserverHwnd) {
        DestroyWindow(taskbarObserverHwnd);
    }
    OcrEnginePaddleDoc::GlobalCleanup();
    WSACleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    CloseHandle(hMutex);
    return 0;
}
