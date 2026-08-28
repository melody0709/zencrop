#include "Settings.h"
#include "Strings.h"
#include "OcrEngine.h"
#include "ocr/ui/OcrMarkdownPreviewHost.h"
#include "window/AlwaysOnTop.h"
#include "screenshot/ScreenshotUtils.h"
#include "MiniHttpServer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
namespace {
OcrSettings& DashboardWindowTestOcrSettings() {
    static OcrSettings settings = [] {
        OcrSettings initial;
        initial.mode = L"local";
        initial.ocrFontSize = 18;
        return initial;
    }();
    return settings;
}
}
OcrSettings LoadOcrSettings() {
    return DashboardWindowTestOcrSettings();
}
void SaveOcrSettings(const OcrSettings& settings) {
    DashboardWindowTestOcrSettings() = settings;
}
ScreenshotSettings LoadScreenshotSettings() { return ScreenshotSettings{}; }
void SaveScreenshotSettings(const ScreenshotSettings&) {}

// P1.5 ToggleLanguage 读取共享设置中的 general.language
SharedSettings& GetSharedSettings() {
    static SharedSettings s;
    return s;
}

uint64_t NextOcrProgressId() {
    static std::atomic<uint64_t> nextId{1};
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

class DashboardWindowStubOcrEngine final : public IOcrEngine {
public:
    void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) override {
        OcrOutput output;
        output.success = true;
        output.text = L"stub";
        output.elapsedMs = 7;
        output.bboxes = {
            RECT{ 32, 28, 260, 86 },
            RECT{ 42, 112, 420, 188 }
        };
        output.bboxClasses = {
            L"doc_title",
            L"text"
        };
        output.rawOcrJson = L"{\"layoutParsingResults\":[{\"prunedResult\":{\"parsing_res_list\":[{\"block_label\":\"doc_title\",\"block_content\":\"stub title block\"}]},\"outputImages\":{\"layout\":\"stub_layout.png\"}}]}";
        output.debugOutputImagesJson = L"{\"pages\":[{\"pageIndex\":1,\"outputImages\":{\"layout\":\"stub_layout.png\"}}]}";
        OcrLayoutBlock title;
        title.id = L"stub:block:title";
        title.pageIndex = 0;
        title.order = 1;
        title.label = L"doc_title";
        title.content = L"stub title block with literal \"bbox\": {\"left\": 999, \"top\": 999}";
        title.bbox = output.bboxes[0];
        title.polygon = {
            { 32.0f, 28.0f },
            { 260.0f, 28.0f },
            { 260.0f, 86.0f },
            { 32.0f, 86.0f }
        };
        title.confidence = 0.98;
        title.source = L"stub_layout";
        OcrLayoutBlock body;
        body.id = L"stub:block:body";
        body.pageIndex = 0;
        body.order = 2;
        body.label = L"text";
        body.content = L"stub body block";
        body.bbox = output.bboxes[1];
        body.polygon = {
            { 42.0f, 112.0f },
            { 420.0f, 112.0f },
            { 420.0f, 188.0f },
            { 42.0f, 188.0f }
        };
        body.confidence = 0.91;
        body.source = L"stub_layout";
        output.blocks = { title, body };
        callback(output);
        if (hBitmap) DeleteObject(hBitmap);
    }
    bool IsAvailable() override { return true; }
    std::wstring Name() override { return L"stub"; }
};

std::shared_ptr<IOcrEngine> OcrEngineFactory::Create(const std::wstring&) {
    return std::make_shared<DashboardWindowStubOcrEngine>();
}

struct OcrMarkdownPreviewHost::Impl {
    bool available = false;
    bool visible = false;
    RECT bounds = {};
    std::wstring markdown;
    std::vector<OcrMarkdownPreviewHost::PreviewBlock> blocks;
    std::wstring hoveredBlockId;
    std::wstring selectedBlockId;
    std::wstring editingBlockId;
};

OcrMarkdownPreviewHost::OcrMarkdownPreviewHost()
    : m_impl(std::make_unique<Impl>()) {}
OcrMarkdownPreviewHost::~OcrMarkdownPreviewHost() = default;
bool OcrMarkdownPreviewHost::Create(HWND, const RECT& bounds, Callbacks callbacks) {
    m_impl->available = true;
    m_impl->bounds = bounds;
    if (callbacks.onReady) callbacks.onReady();
    return true;
}
void OcrMarkdownPreviewHost::Destroy() {
    if (m_impl) m_impl->available = false;
}
void OcrMarkdownPreviewHost::SetBounds(const RECT& bounds) {
    if (m_impl) m_impl->bounds = bounds;
}
void OcrMarkdownPreviewHost::SetZoomFactor(double) {}
void OcrMarkdownPreviewHost::SetTextFontSize(int) {}
void OcrMarkdownPreviewHost::Show(bool visible) {
    if (m_impl) m_impl->visible = visible;
}
void OcrMarkdownPreviewHost::SetLocalAssetRoot(const std::wstring&) {}
void OcrMarkdownPreviewHost::RenderMarkdown(int, const std::wstring& markdown) {
    if (!m_impl) return;
    m_impl->markdown = markdown;
    m_impl->blocks.clear();
}
void OcrMarkdownPreviewHost::RenderMarkdownBlocks(
    int,
    const std::wstring& markdown,
    const std::vector<PreviewBlock>& blocks,
    const std::wstring&)
{
    if (!m_impl) return;
    m_impl->markdown = markdown;
    m_impl->blocks = blocks;
}
void OcrMarkdownPreviewHost::SetHoveredBlock(const std::wstring& id) {
    if (m_impl) m_impl->hoveredBlockId = id;
}
void OcrMarkdownPreviewHost::SetSelectedBlock(const std::wstring& id, bool) {
    if (m_impl) m_impl->selectedBlockId = id;
}
void OcrMarkdownPreviewHost::SetEditingBlock(const std::wstring& id) {
    if (m_impl) m_impl->editingBlockId = id;
}
void OcrMarkdownPreviewHost::PostPreviewBlockSaveResult(
    const std::wstring&,
    const std::wstring&,
    bool,
    const std::wstring&) {}
void OcrMarkdownPreviewHost::PostPreviewBlockRestoreResult(
    const std::wstring&,
    const std::wstring&,
    bool,
    const std::wstring&) {}
bool OcrMarkdownPreviewHost::IsReady() const { return m_impl && m_impl->available; }
bool OcrMarkdownPreviewHost::IsAvailable() const { return m_impl && m_impl->available; }
bool OcrMarkdownPreviewHost::IsCreating() const { return false; }

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
// P0.2 CopyImageToClipboard / CopySelectedBlockImageToClipboard 调用
bool CopyBitmapToClipboard(HWND, HBITMAP, bool) { return true; }
}

std::vector<unsigned char> DecodeBase64Image(const std::string&) {
    return {};
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
