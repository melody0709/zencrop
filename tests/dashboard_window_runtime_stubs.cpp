#include "Settings.h"
#include "Strings.h"
#include "OcrEngine.h"
#include "window/AlwaysOnTop.h"
#include "screenshot/ScreenshotUtils.h"
#include "MiniHttpServer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace {

std::wstring GetEnvWide(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return L"";
    std::wstring value(needed, L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0) return L"";
    value.resize(written);
    return value;
}

int GetEnvInt(const wchar_t* name, int fallback) {
    std::wstring value = GetEnvWide(name);
    if (value.empty()) return fallback;
    int parsed = _wtoi(value.c_str());
    return parsed == 0 && value != L"0" ? fallback : parsed;
}

} // namespace

namespace S {
void InitLanguage() {}
void SetLanguage(bool) {}
bool IsChinese() { return false; }
const wchar_t* AppName() { return L"ZenCrop"; }
const wchar_t* TrayTip() { return L"ZenCrop"; }
const wchar_t* AlreadyRunning() { return L"Already running"; }
const wchar_t* MenuShowTitlebar() { return L"Show Titlebar"; }
const wchar_t* MenuSettings() { return L"Settings"; }
const wchar_t* MenuOpenRelease() { return L"Open Release"; }
const wchar_t* MenuExit() { return L"Exit"; }
const wchar_t* SettingsTitle() { return L"Settings"; }
const wchar_t* GeneralCaption() { return L"General"; }
const wchar_t* ZenCropCaption() { return L"ZenCrop"; }
const wchar_t* AotCaption() { return L"Always On Top"; }
const wchar_t* LanguageLabel() { return L"Language"; }
const wchar_t* LangAuto() { return L"Auto"; }
const wchar_t* LangEnglish() { return L"English"; }
const wchar_t* LangChinese() { return L"Chinese"; }
const wchar_t* ColorLabel() { return L"Color"; }
const wchar_t* ChooseButton() { return L"Choose"; }
const wchar_t* ThicknessLabel() { return L"Thickness"; }
const wchar_t* CropOnTop() { return L"Crop On Top"; }
const wchar_t* ReparentLabel() { return L"Reparent"; }
const wchar_t* ThumbnailLabel() { return L"Thumbnail"; }
const wchar_t* ViewportLabel() { return L"Viewport"; }
const wchar_t* CloseAllLabel() { return L"Close All"; }
const wchar_t* HotkeyLabel() { return L"Hotkey"; }
const wchar_t* AotShowBorder() { return L"Show Border"; }
const wchar_t* AotCustomColor() { return L"Custom Color"; }
const wchar_t* OpacityLabel() { return L"Opacity"; }
const wchar_t* AotRounded() { return L"Rounded"; }
const wchar_t* InsetLabel() { return L"Inset"; }
const wchar_t* HotkeyNone() { return L"None"; }
const wchar_t* HotkeyPrompt() { return L"Hotkey"; }
const wchar_t* KeySpace() { return L"Space"; }
const wchar_t* KeyEnter() { return L"Enter"; }
const wchar_t* KeyBackspace() { return L"Backspace"; }
const wchar_t* KeyPageUp() { return L"Page Up"; }
const wchar_t* KeyPageDown() { return L"Page Down"; }
const wchar_t* KeyLeft() { return L"Left"; }
const wchar_t* KeyUp() { return L"Up"; }
const wchar_t* KeyRight() { return L"Right"; }
const wchar_t* KeyDown() { return L"Down"; }
const wchar_t* HotkeyConflictMsg() { return L"Hotkey conflict"; }
const wchar_t* HotkeyConflictTitle() { return L"Hotkey"; }
const wchar_t* CropLabelFormat() { return L"%dx%d"; }
const wchar_t* OverlayHoverHint() { return L""; }
const wchar_t* OverlayAdjustHint() { return L""; }
const wchar_t* ThumbnailTitle() { return L"Thumbnail"; }
}

GeneralSettings LoadGeneralSettings() { return GeneralSettings{}; }
void SaveGeneralSettings(const GeneralSettings&) {}
AotSettings LoadAotSettings() { return AotSettings{}; }
void SaveAotSettings(const AotSettings&) {}
OverlaySettings LoadOverlaySettings() { return OverlaySettings{}; }
void SaveOverlaySettings(const OverlaySettings&) {}
HotkeySettings LoadHotkeySettings() { return HotkeySettings{}; }
void SaveHotkeySettings(const HotkeySettings&) {}
OcrSettings LoadOcrSettings() {
    OcrSettings settings;
    settings.mode = L"paddle_local";
    settings.language = L"auto";
    settings.enableDocParsing = true;
    settings.ocrFontSize = 18;
    settings.paddleLocalModelDir = GetEnvWide(L"ZENCROP_PADDLE_LOCAL_MODEL_DIR");
    settings.docLayoutModelPath = GetEnvWide(L"ZENCROP_DOC_LAYOUT_MODEL_PATH");
    settings.paddleLocalPort = GetEnvInt(L"ZENCROP_PADDLE_LOCAL_PORT", 0);
    settings.paddleLocalIdleTimeoutMin = GetEnvInt(L"ZENCROP_PADDLE_LOCAL_IDLE_TIMEOUT_MIN", 1);
    settings.paddleLocalPrompt = L"OCR:";
    return settings;
}
void SaveOcrSettings(const OcrSettings&) {}
ScreenshotSettings LoadScreenshotSettings() { return ScreenshotSettings{}; }
void SaveScreenshotSettings(const ScreenshotSettings&) {}

SharedSettings& GetSharedSettings() {
    static SharedSettings settings;
    return settings;
}

std::wstring NormalizeOcrRoute(const std::wstring& route) {
    if (route == L"local" ||
        route == L"paddle_cloud" ||
        route == L"paddle_local" ||
        route == L"paddle_local_doc" ||
        route == L"ppocrv6_onnx") {
        return route;
    }
    if (route == L"paddle_doc" || route == L"doc_parsing") return L"paddle_local_doc";
    return L"current";
}

int ResolveOcrLlamaIdleTimeoutMin(const OcrSettings& settings, const HotkeySettings& hotkeys) {
    auto usesLlama = [](const std::wstring& route) {
        std::wstring normalized = NormalizeOcrRoute(route);
        return normalized == L"paddle_local" || normalized == L"paddle_local_doc";
    };

    bool mainUsesLlama = usesLlama(settings.mode);
    bool altUsesLlama = !hotkeys.ocrAlt.IsEmpty() && usesLlama(settings.altHotkeyRoute);
    int mainMinutes = (std::min)(240, (std::max)(0, settings.paddleLocalIdleTimeoutMin));
    int altMinutes = (std::min)(240, (std::max)(0, settings.altHotkeyIdleTimeoutMin));
    if (mainUsesLlama && altUsesLlama) {
        if (mainMinutes <= 0) return altMinutes;
        if (altMinutes <= 0) return mainMinutes;
        return (std::min)(mainMinutes, altMinutes);
    }
    if (altUsesLlama) return altMinutes;
    return mainMinutes;
}

uint64_t NextOcrProgressId() {
    static std::atomic<uint64_t> nextId{1};
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

AlwaysOnTopManager& AlwaysOnTopManager::Instance() {
    static AlwaysOnTopManager instance;
    return instance;
}
AlwaysOnTopManager::~AlwaysOnTopManager() = default;
void AlwaysOnTopManager::PinWindow(HWND) {}
void AlwaysOnTopManager::UnpinWindow(HWND) {}
void AlwaysOnTopManager::TogglePin(HWND) {}
void AlwaysOnTopManager::UnpinAll() {}
void AlwaysOnTopManager::CleanupInvalid() {}
void AlwaysOnTopManager::UpdateSettings() {}
bool AlwaysOnTopManager::IsPinned(HWND) const { return false; }
int AlwaysOnTopManager::GetPinnedCount() const { return 0; }

namespace Screenshot {
bool CopyTextToClipboard(HWND, const std::wstring&) { return true; }
bool CopyBitmapToClipboard(HWND, HBITMAP, bool) { return true; }
}

MiniHttpServer::MiniHttpServer() = default;
MiniHttpServer::~MiniHttpServer() = default;
MiniHttpServer& MiniHttpServer::Instance() {
    static MiniHttpServer instance;
    return instance;
}
bool MiniHttpServer::Start(unsigned short) { return false; }
void MiniHttpServer::Stop() {}
bool MiniHttpServer::IsRunning() const { return false; }
unsigned short MiniHttpServer::GetPort() const { return 0; }
