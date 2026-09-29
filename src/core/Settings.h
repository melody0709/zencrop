#pragma once
#include <windows.h>
#include <mutex>
// Stage3 3-B: Settings repository must not include ocr/batch.
#include "core/RasterBoundOptions.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/ResourceIds.h"

struct AppLanguage {
    enum Value { Auto, English, Chinese };
    Value value = Auto;

    bool operator==(const AppLanguage&) const = default;
};

struct GeneralSettings {
    AppLanguage language;
    bool showTitlebar = false;

    bool operator==(const GeneralSettings&) const = default;
};

struct AotSettings {
    bool showBorder = true;
    bool customColor = true;
    COLORREF color = RGB(0, 120, 215);
    int opacity = 100;
    int thickness = 4;
    bool roundedCorners = true;
    int inset = 1;

    bool operator==(const AotSettings&) const = default;
};

struct OverlaySettings {
    COLORREF color = RGB(255, 0, 0);
    int thickness = 3;
    bool cropOnTop = true;

    bool operator==(const OverlaySettings&) const = default;
};

enum class ScreenshotFormat {
    Png,
    Jpeg,
    Bmp,
    WebP,
    Avif,
};

struct ScreenshotSettings {
    ScreenshotFormat format = ScreenshotFormat::Png;
    int jpegQuality = 95;
    bool includeCursor = false;
    std::wstring quickSaveDir;
    std::wstring fileNameTemplate = L"ZenCrop_{yyyyMMdd}_{HHmmss}_{fff}";
    bool warnAlphaLossForJpegBmp = true;
    int annotationActiveTool = 0;
    int annotationGeometryTool = 1;
    int annotationMarkerTool = 4;
    int annotationArrowTool = 5;
    int annotationTextTool = 8;
    int annotationMosaicTool = 11;
    int annotationColorIndex = 0;
    int annotationGeometryColorIndex = 2;
    int annotationMarkerColorIndex = 2;
    bool annotationUsesCustomColor = false;
    COLORREF annotationCustomColor = RGB(227, 195, 99);
    int annotationColorAlpha = 100;
    int annotationColorPickerMode = 0;
    int annotationLineStyle = 1;
    int annotationGeometryPenWidth = 4;
    int annotationGeometryRoundedRadius = 21;
    int annotationPencilPenWidth = 4;
    int annotationMarkerPenWidth = 12;
    int annotationArrowPenWidth = 24;
    int annotationArrowShape = 4;
    int annotationBrokenLineMode = 0;
    bool annotationBrokenLineArrow = true;
    int annotationBrokenLineStartArrowType = 0;
    int annotationBrokenLineEndArrowType = 1;
    int annotationMagnifierPenWidth = 4;
    int annotationMagnifierRoundedRadius = 18;
    bool annotationMagnifierEllipse = false;
    bool annotationMagnifierEraseMark = false;
    bool annotationMagnifierAntiAlias = true;
    bool annotationMagnifierShadow = false;
    int annotationMagnifierLinkType = 0;
    int annotationMagnifierMagnification = 150;
    int annotationMosaicPenWidth = 12;
    int annotationEraserPenWidth = 12;
    int annotationSerialPenWidth = 16;
    int annotationMosaicStrength = 14;
    int annotationMarkerBlendMode = 0;
    int annotationMosaicMode = 0;
    int annotationSerialType = 0;
    bool annotationHighLightStroke = false;
    int annotationHighLightOpacity = 68;
    COLORREF annotationHighLightStrokeColor = RGB(255, 15, 0);
    bool annotationAutoMosaicSync = true;
    bool annotationTextOutline = false;
    int annotationTextOutlineSize = 1;
    COLORREF annotationTextOutlineColor = RGB(255, 255, 255);
    bool annotationTextBackground = false;
    COLORREF annotationTextBackgroundColor = RGB(0, 0, 0);
    int annotationTextBackgroundOpacity = 100;
    int annotationTextBackgroundRounded = 0;
    int annotationTextBackgroundPadding = 0;
    bool annotationTextBold = false;
    bool annotationTextItalics = false;
    std::wstring annotationTextFontFamily = L"Microsoft YaHei";
    int annotationTextFontSize = 27;
    std::wstring annotationWatermarkText = L"Watermark";
    COLORREF annotationWatermarkColor = RGB(250, 3, 15);
    bool annotationWatermarkBold = false;
    bool annotationWatermarkItalics = false;
    int annotationWatermarkOpacity = 50;
    int annotationWatermarkFontSize = 27;
    int annotationWatermarkGap = 20;
    int annotationWatermarkAngle = 0;
    std::wstring annotationWatermarkFontFamily = L"Microsoft YaHei";
    int annotationWatermarkPosition = 1;
    bool postProcessEnabledEveryScreenshot = false;
    int postProcessMode = 1;
    int roundedCornerRadius = 18;
    int postProcessShadowSize = 10;
    COLORREF postProcessShadowColor = RGB(0, 0, 0);
    int postProcessBorderSize = 2;
    COLORREF postProcessBorderColor = RGB(255, 255, 255);
    std::wstring functionAreaAlwaysShow = L"LongShot,GifShot,CopyOcr,Translate,Pin,Save,Close,Copy";
    std::wstring functionAreaMorePanel = L"OcrTable,QuickSave,LatexRecognition,WinRoi";
    std::wstring functionAreaAlwaysHide = L"Print";

    // Hover magnifier color picker.
    // Active in screenshot Hover/Adjust states: shows a zoomed pixel grid +
    // color text near the cursor; press C to copy, M to toggle,
    // Ctrl+Shift+C to switch format.
    //
    // sampleWindow = (15, 9) * (11.0 / Power).
    // Power=11 is the visual/default calibration point: one source pixel per
    // Power 11 maps one source pixel to each cell in the 15x9 grid.
    bool hoverMagnifierEnabled = false;
    int hoverMagnifierPower = 11;          // Range 1..100; 11 gives 1:1 sampling.
    int hoverMagnifierColorFormat = 3;     // 0=RGB,1=BGR,2=HEX,3=#HEX,4=HSV,5=HSL
    bool hoverMagnifierShowCoord = true;   // MagnifierContent bit 0x80

    // LongShot warning suppression and startup behavior.
    bool longShotSuperLongWarningNoAsk = false;
    bool longShotMaxLengthWarningNoAsk = false;
    bool longShotMatchFailWarningNoAsk = false;
    bool longShotStopClearConfirmNoAsk = false; // LongShot.StopClearConfirmNoAsk
    int longShotAfterInitAction = 1; // 0 do-not-start, 1 vert auto, 2 horiz auto, 3 show start/stop
    bool longShotAutoCrop = false;

    bool operator==(const ScreenshotSettings&) const = default;
};

struct HotkeyConfig {
    bool win = false;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    unsigned char key = 0;

    bool IsEmpty() const { return key == 0; }
    bool operator==(const HotkeyConfig&) const = default;
    UINT Modifiers() const {
        UINT mod = 0;
        if (win) mod |= MOD_WIN;
        if (ctrl) mod |= MOD_CONTROL;
        if (shift) mod |= MOD_SHIFT;
        if (alt) mod |= MOD_ALT;
        mod |= MOD_NOREPEAT;
        return mod;
    }
    std::wstring ToString() const;
};

struct HotkeySettings {
    HotkeyConfig reparent;
    HotkeyConfig thumbnail;
    HotkeyConfig viewport;
    HotkeyConfig closeReparent;
    HotkeyConfig alwaysOnTop;
    HotkeyConfig screenshot;
    HotkeyConfig ocr;
    HotkeyConfig ocrAlt;
    HotkeyConfig selectionTranslate;

    bool operator==(const HotkeySettings&) const = default;
};

enum class TranslationAdapterKind {
    DeepSeekChat,
    OpenAIResponses,
    OpenAIChatCompletions,
    GeminiGenerateContent,
    XaiResponses,
    OllamaChat,
    MachineTranslation,
};

enum class TranslationReasoningMode {
    ProviderDefault,
    Off,
    Minimal,
    Low,
    Medium,
    High,
    XHigh,
    Max,
};

enum class TranslationAuthMode {
    BearerApiKey,
    ApiKey,
    None,
};

inline constexpr bool TranslationAuthUsesCredential(
    TranslationAuthMode mode) {
    return mode != TranslationAuthMode::None;
}

inline constexpr int kTranslationSettingsSchemaVersion = 7;
inline constexpr double kTranslationPreviewZoomMin = 0.25;
inline constexpr double kTranslationPreviewZoomMax = 5.0;
inline constexpr int kTranslationSourceFontSizeMin = 8;
inline constexpr int kTranslationSourceFontSizeMax = 32;
inline constexpr wchar_t kLegacyTranslationCredentialTarget[] =
    L"ZenCrop/Translation/deepseek";
inline constexpr wchar_t kLegacyDeepSeekTranslationProviderId[] =
    L"builtin.deepseek.default";
inline constexpr wchar_t kDefaultTranslationProviderId[] =
    L"builtin.google-translate-community.default";
inline constexpr wchar_t kDefaultTranslationPromptId[] =
    L"builtin.accurate.v1";
inline constexpr size_t kMaxTranslationCustomModels = 50;
inline constexpr size_t kMaxTranslationModelLength = 256;

struct TranslationProviderProfile {
    std::wstring id;
    std::wstring displayName;
    std::wstring presetKind;
    TranslationAdapterKind adapterKind = TranslationAdapterKind::DeepSeekChat;
    bool enabled = true;
    TranslationAuthMode authMode = TranslationAuthMode::BearerApiKey;
    std::wstring baseUrlOverride;
    std::wstring region;
    std::wstring model = L"deepseek-v4-flash";
    bool customModel = false;
    std::vector<std::wstring> customModels;
    std::wstring credentialRef = kLegacyTranslationCredentialTarget;
    TranslationReasoningMode reasoningMode = TranslationReasoningMode::Off;
    std::optional<double> temperature;
    std::wstring advancedOptionsJson = L"{}";

    bool operator==(const TranslationProviderProfile&) const = default;
};

struct TranslationPromptProfile {
    std::wstring id;
    std::wstring name;
    std::wstring styleInstruction;

    bool operator==(const TranslationPromptProfile&) const = default;
};

struct BuiltInOpenAiCompatibleProviderDefault {
    const wchar_t* id;
    const wchar_t* displayName;
    const wchar_t* presetKind;
    const wchar_t* model;
};

inline constexpr BuiltInOpenAiCompatibleProviderDefault
    kBuiltInOpenAiCompatibleProviderDefaults[] = {
        {L"builtin.openai.default", L"OpenAI", L"openai", L"gpt-5.4-mini"},
        {L"builtin.gemini.default", L"Gemini", L"gemini",
            L"gemini-2.5-flash-lite"},
        {L"builtin.minimax.default", L"MiniMax", L"minimax", L"MiniMax-M2.7"},
        {L"builtin.grok.default", L"Grok (xAI)", L"grok",
            L"grok-4.20-0309-non-reasoning"},
        {L"builtin.alibaba-cloud.default", L"Alibaba Cloud",
            L"alibaba-cloud", L"qwen3.5-flash"},
        {L"builtin.siliconflow.default", L"SiliconFlow",
            L"siliconflow", L"Qwen/Qwen3.5-9B"},
        {L"builtin.xiaomi-mimo.default", L"Xiaomi MiMo",
            L"xiaomi-mimo", L"mimo-v2.6-flash"},
    };

struct TranslationSettings {
    // schemaVersion is persisted with the translation section. schemaSupported
    // is runtime-only: a future section remains untouched rather than being
    // silently downgraded by this build.
    int schemaVersion = kTranslationSettingsSchemaVersion;
    bool schemaSupported = true;
    bool enabled = false;
    bool selectionCopyFallbackEnabled = true;
    std::wstring ocrRoute = L"current";
    std::wstring sourceLanguage = L"auto";
    std::wstring targetLanguage = L"auto";
    std::wstring activeProviderId = kDefaultTranslationProviderId;
    std::vector<TranslationProviderProfile> providerProfiles;
    std::wstring activePromptId = kDefaultTranslationPromptId;
    std::vector<TranslationPromptProfile> customPromptProfiles;
    bool showSourceText = true;
    bool preserveParagraphs = true;
    bool resultOnTop = false;
    bool showWindowBorder = false;
    int sourceFontSize = 14;
    double sourcePreviewZoomFactor = 1.0;
    double translationPreviewZoomFactor = 1.0;

    TranslationSettings() {
        TranslationProviderProfile profile;
        profile.id = kDefaultTranslationProviderId;
        profile.displayName = L"Google Translate";
        profile.presetKind = L"google-translate-community";
        profile.adapterKind = TranslationAdapterKind::MachineTranslation;
        profile.authMode = TranslationAuthMode::None;
        profile.credentialRef.clear();
        profile.model.clear();
        profile.reasoningMode = TranslationReasoningMode::Off;
        providerProfiles.push_back(std::move(profile));
    }

    bool operator==(const TranslationSettings&) const = default;
};

struct OcrSettings {
    std::wstring language = L"zh-Hans-CN";

    std::wstring mode = L"local";
    std::wstring altHotkeyRoute = L"paddle_local_doc";
    int altHotkeyIdleTimeoutMin = 10;

    std::wstring paddleApiUrl = L"https://paddleocr.aistudio-app.com/api/v2/ocr/jobs";
    std::wstring paddleToken;
    bool paddleCloudUseChartRecognition = false;
    int timeoutMs = 120000;

    std::wstring paddleLocalModelDir;
    int paddleLocalPort = 0;
    int paddleLocalIdleTimeoutMin = 10;
    std::wstring paddleLocalPrompt = L"OCR:";
    int paddleVlMaxTokens = 4096;
    bool enableDocParsing = false;
    bool enableImageCrop = true;
    uint32_t localRasterMaxPixelEdge = kDefaultPdfMaxPixelEdge;
    uint32_t localRasterMaxMegapixels = kDefaultPdfMaxMegapixels;
    std::wstring docLayoutModelPath;
    std::wstring layoutModelFamily = L"auto";
    std::wstring layoutThresholdProfile = L"official";
    std::wstring paddleDocGroupingMode = L"official_group";
    bool docRecognizeCharts = false;
    bool docRecognizeImages = false;
    bool docRecognizeSeals = false;
    bool docIgnorePageDecorations = true;
    bool docKeepFootnotes = false;
    bool docIncludeIgnoredRegions = false;
    bool docUsePhysicalSorting = false;
    int ocrFontSize = 18;
    bool resultOnTop = false;

    std::wstring ppocrv6ModelDir;
    std::wstring ppocrv6Variant = L"small";
    std::wstring ppocrv6Provider = L"cpu";
    int ppocrv6CpuThreads = 4;
    // 0 = Auto (resolved to 6 at runtime); 1..8 fixed batch size.
    int ppocrv6RecBatchSize = 1;
    // Scheme 1 Balanced default: min/64 short-side floor (native res for normal shots).
    // Clamp range is 64..4096. Do NOT default to min/960 — force-upscales crops.
    int ppocrv6DetLimitSideLen = 64;
    std::wstring ppocrv6DetLimitType = L"min";
    int ppocrv6DetMaxSideLimit = 4000;
    int ppocrv6DetThreshPct = 20;
    int ppocrv6DetBoxThreshPct = 45;
    int ppocrv6DetUnclipRatioPct = 140;
    int ppocrv6RecScoreThreshPct = 0;
    // Named preset id (scheme 1): custom | balanced | quality | fast | official_37.
    // Legacy ids accepted on load. Presets do NOT change Variant (small/medium).
    // New installs default to balanced knobs above; id marks the pack name.
    std::wstring ppocrv6Preset = L"balanced";

    bool operator==(const OcrSettings&) const = default;
};

// PP-OCRv6 Options presets — same-model speed/quality axis (scheme 1).
// Enum order = Options dialog combo order (Custom=0).
enum class PPOcrV6PresetId {
    Custom = 0,
    Balanced,      // default daily: native res (min/64 floor)
    Quality,       // mild upscale small crops (min/320)
    Fast,          // mild downscale large images (max/1280); same model
    Official37,    // PaddleX 3.7 det+rec knobs (reference)
};

inline PPOcrV6PresetId ParsePPOcrV6PresetId(const std::wstring& name) {
    if (name == L"balanced" || name == L"screenshot_balanced") {
        return PPOcrV6PresetId::Balanced;
    }
    if (name == L"quality" || name == L"screenshot_small_text") {
        return PPOcrV6PresetId::Quality;
    }
    if (name == L"fast" || name == L"fast_cpu") {
        return PPOcrV6PresetId::Fast;
    }
    if (name == L"official_37" || name == L"document_official_37") {
        return PPOcrV6PresetId::Official37;
    }
    return PPOcrV6PresetId::Custom;
}

inline bool IsLegacyPPOcrV6PresetId(const std::wstring& name) {
    return name == L"screenshot_balanced"
        || name == L"screenshot_small_text"
        || name == L"fast_cpu"
        || name == L"document_official_37";
}

inline const wchar_t* PPOcrV6PresetIdName(PPOcrV6PresetId id) {
    switch (id) {
    case PPOcrV6PresetId::Balanced: return L"balanced";
    case PPOcrV6PresetId::Quality: return L"quality";
    case PPOcrV6PresetId::Fast: return L"fast";
    case PPOcrV6PresetId::Official37: return L"official_37";
    default: return L"custom";
    }
}

// Apply named preset det/rec knobs only.
// Does NOT change: modelDir, provider, threads, variant (small/medium stays on main page).
inline void ApplyPPOcrV6Preset(OcrSettings& settings, PPOcrV6PresetId id) {
    settings.ppocrv6Preset = PPOcrV6PresetIdName(id);
    switch (id) {
    case PPOcrV6PresetId::Balanced:
        // Default: short-side floor 64 → typical screenshots stay native resolution.
        settings.ppocrv6DetLimitType = L"min";
        settings.ppocrv6DetLimitSideLen = 64;
        settings.ppocrv6DetMaxSideLimit = 4000;
        settings.ppocrv6DetThreshPct = 20;
        settings.ppocrv6DetBoxThreshPct = 45;
        settings.ppocrv6DetUnclipRatioPct = 140;
        settings.ppocrv6RecScoreThreshPct = 0;
        settings.ppocrv6RecBatchSize = 1;
        break;
    case PPOcrV6PresetId::Quality:
        // Mild upscale only when short side < 320 (not min/960 crop blow-up).
        settings.ppocrv6DetLimitType = L"min";
        settings.ppocrv6DetLimitSideLen = 320;
        settings.ppocrv6DetMaxSideLimit = 4000;
        settings.ppocrv6DetThreshPct = 20;
        settings.ppocrv6DetBoxThreshPct = 45;
        settings.ppocrv6DetUnclipRatioPct = 140;
        settings.ppocrv6RecScoreThreshPct = 0;
        settings.ppocrv6RecBatchSize = 1;
        break;
    case PPOcrV6PresetId::Fast:
        // Mild downscale when long side > 1280. Same model as other daily presets.
        settings.ppocrv6DetLimitType = L"max";
        settings.ppocrv6DetLimitSideLen = 1280;
        settings.ppocrv6DetMaxSideLimit = 4000;
        settings.ppocrv6DetThreshPct = 20;
        settings.ppocrv6DetBoxThreshPct = 45;
        settings.ppocrv6DetUnclipRatioPct = 140;
        settings.ppocrv6RecScoreThreshPct = 0;
        settings.ppocrv6RecBatchSize = 1;
        break;
    case PPOcrV6PresetId::Official37:
        // PaddleX release/3.7 OCR.yaml core det+rec (reference; not daily default).
        settings.ppocrv6DetLimitType = L"min";
        settings.ppocrv6DetLimitSideLen = 64;
        settings.ppocrv6DetMaxSideLimit = 4000;
        settings.ppocrv6DetThreshPct = 30;
        settings.ppocrv6DetBoxThreshPct = 60;
        settings.ppocrv6DetUnclipRatioPct = 150;
        settings.ppocrv6RecScoreThreshPct = 0;
        settings.ppocrv6RecBatchSize = 6;
        break;
    case PPOcrV6PresetId::Custom:
    default:
        settings.ppocrv6Preset = L"custom";
        break;
    }
}

// True when det/rec knobs still match a named preset (variant ignored — presets
// never own small/medium).
inline bool PPOcrV6KnobsMatchPreset(const OcrSettings& s, PPOcrV6PresetId id) {
    if (id == PPOcrV6PresetId::Custom) return true;
    OcrSettings pack;
    // Preserve variant so Apply does not need to touch it for comparison.
    pack.ppocrv6Variant = s.ppocrv6Variant;
    ApplyPPOcrV6Preset(pack, id);
    return s.ppocrv6DetLimitType == pack.ppocrv6DetLimitType
        && s.ppocrv6DetLimitSideLen == pack.ppocrv6DetLimitSideLen
        && s.ppocrv6DetMaxSideLimit == pack.ppocrv6DetMaxSideLimit
        && s.ppocrv6DetThreshPct == pack.ppocrv6DetThreshPct
        && s.ppocrv6DetBoxThreshPct == pack.ppocrv6DetBoxThreshPct
        && s.ppocrv6DetUnclipRatioPct == pack.ppocrv6DetUnclipRatioPct
        && s.ppocrv6RecScoreThreshPct == pack.ppocrv6RecScoreThreshPct
        && s.ppocrv6RecBatchSize == pack.ppocrv6RecBatchSize;
}

// If knobs no longer match the stored named preset id → force Custom.
// Variant alone never triggers this (scheme 1: model independent of preset).
inline void DowngradePPOcrV6PresetIfDiverged(OcrSettings& s) {
    const auto id = ParsePPOcrV6PresetId(s.ppocrv6Preset);
    if (id == PPOcrV6PresetId::Custom) return;
    if (!PPOcrV6KnobsMatchPreset(s, id)) {
        s.ppocrv6Preset = L"custom";
    }
}

// Upgrade policy for the pre-scheme-1 preset ids. Those ids owned Variant and
// represented different knob packs, so relabeling them as a new named preset
// would be false. Preserve every loaded runtime value and mark the pack Custom;
// the user can explicitly select a new scheme-1 preset later.
inline void NormalizeLoadedPPOcrV6Preset(
    OcrSettings& settings,
    const std::wstring& persistedName)
{
    if (IsLegacyPPOcrV6PresetId(persistedName)) {
        settings.ppocrv6Preset = L"custom";
        return;
    }
    settings.ppocrv6Preset = PPOcrV6PresetIdName(ParsePPOcrV6PresetId(persistedName));
    DowngradePPOcrV6PresetIfDiverged(settings);
}

std::wstring GetSettingsFilePath();
std::mutex& SettingsWriteMutex();
std::wstring ReadFileToString(const std::wstring& path);
// Preserve the exact on-disk bytes before replacing a damaged settings file.
bool BackupSettingsFile(const std::wstring& path, std::wstring* error = nullptr);
bool WriteStringToFile(
    const std::wstring& path,
    const std::wstring& content,
    std::wstring* error = nullptr);

// The canonical settings.json layout: seven known sections in a fixed order, followed
// by any unknown top-level fields preserved verbatim. Each non-empty field below is the
// complete `  "key": { ... }` entry text (the Build*SectionJson helpers already produce
// that); an empty field reuses the section text already present in the source JSON.
struct SettingsSections {
    std::wstring general;
    std::wstring alwaysOnTop;
    std::wstring overlay;
    std::wstring screenshot;
    std::wstring ocr;
    std::wstring hotkeys;
    std::wstring translation;
};

// Single entry point for writing settings.json: merges `overrides` into `sourceJson`,
// keeps every section the caller does not override, and never drops unknown top-level
// fields. All Save*Settings writers and CommitSettingsPatch go through this function.
std::wstring AssembleSettingsJson(
    const std::wstring& sourceJson,
    const SettingsSections& overrides);

GeneralSettings LoadGeneralSettings();
void SaveGeneralSettings(const GeneralSettings& settings);
AotSettings LoadAotSettings();
void SaveAotSettings(const AotSettings& settings);
OverlaySettings LoadOverlaySettings();
void SaveOverlaySettings(const OverlaySettings& settings);
HotkeySettings LoadHotkeySettings();
bool SaveHotkeySettings(
    const HotkeySettings& settings,
    std::wstring* error = nullptr);
OcrSettings LoadOcrSettings();
void SaveOcrSettings(const OcrSettings& settings);
TranslationSettings LoadTranslationSettings();
bool SaveTranslationSettings(
    const TranslationSettings& settings,
    std::wstring* error = nullptr);
ScreenshotSettings LoadScreenshotSettings();
void SaveScreenshotSettings(const ScreenshotSettings& settings);
std::wstring NormalizeOcrRoute(const std::wstring& route);
bool OcrRouteUsesLlama(const std::wstring& route);
bool OcrSettingsUsesLlama(const OcrSettings& settings, const HotkeySettings& hotkeys);
int ResolveOcrLlamaIdleTimeoutMin(const OcrSettings& settings, const HotkeySettings& hotkeys);
COLORREF GetSystemAccentColor();
// Settings snapshot shared by the owning domains and the Settings window.
struct SharedSettings {
    GeneralSettings general;
    AotSettings aot;
    OverlaySettings overlay;
    ScreenshotSettings screenshot;
    HotkeySettings hotkeys;
    TranslationSettings translation;
};

SharedSettings& GetSharedSettings();

enum class SettingsCommitStatus {
    Success = 0,
    Conflict,
    ValidationError,
    IoError,
    SchemaUnsupported,
};

struct SettingsCommitResult {
    SettingsCommitStatus status = SettingsCommitStatus::Success;
    std::wstring errorMessage;
    std::wstring conflictingField;
};

struct SettingsDraft {
    SharedSettings baseline;
    SharedSettings pending;
    OcrSettings ocrBaseline;
    OcrSettings ocrPending;
    bool startupBaseline = false;
    bool startupPending = false;
    bool appliedLanguageChinese = false;
};

// Applies the fields that differ between draft.baseline and draft.pending onto the
// latest on-disk settings. Returns Conflict (and leaves the file untouched) when the
// same field was changed externally; pass forceOverwrite=true only after the user has
// explicitly accepted overwriting that external change.
SettingsCommitResult CommitSettingsPatch(
    const SettingsDraft& draft,
    SettingsDraft* outUpdatedDraft = nullptr,
    bool forceOverwrite = false);
