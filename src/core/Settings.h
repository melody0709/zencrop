#pragma once
#include <windows.h>
// Stage3 3-B: Settings repository must not include ocr/batch.
#include "core/RasterBoundOptions.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define IDD_AOT_SETTINGS        2010
#define IDC_AOT_SHOW_BORDER     2001
#define IDC_AOT_COLOR_MODE      2002
#define IDC_AOT_COLOR_PREVIEW   2003
#define IDC_AOT_CHOOSE_COLOR    2004
#define IDC_AOT_OPACITY_SLIDER  2005
#define IDC_AOT_OPACITY_LABEL   2006
#define IDC_AOT_THICK_SLIDER    2007
#define IDC_AOT_THICK_LABEL     2008
#define IDC_AOT_ROUNDED         2009
#define IDC_AOT_INSET_SLIDER    2011
#define IDC_AOT_INSET_LABEL     2012
#define IDC_AOT_INSET_VALUE     2013

#define IDD_SETTINGS            2020
#define IDD_SETTINGS_ZENCROP    2021
#define IDD_SETTINGS_AOT        2022
#define IDC_SETTINGS_TAB        2030

#define IDC_ZC_COLOR_PREVIEW    2031
#define IDC_ZC_CHOOSE_COLOR     2032
#define IDC_ZC_THICK_SLIDER     2033
#define IDC_ZC_THICK_LABEL      2034
#define IDC_ZC_CROP_ON_TOP      2035

#define IDC_HK_REPARENT_EDIT    2040
#define IDC_HK_REPARENT_CLEAR   2041
#define IDC_HK_THUMBNAIL_EDIT   2042
#define IDC_HK_THUMBNAIL_CLEAR  2043
#define IDC_HK_VIEWPORT_EDIT    2048
#define IDC_HK_VIEWPORT_CLEAR   2049
#define IDC_HK_CLOSE_EDIT       2044
#define IDC_HK_CLOSE_CLEAR      2045
#define IDC_HK_AOT_EDIT         2046
#define IDC_HK_AOT_CLEAR        2047

#define IDD_SETTINGS_GENERAL   2023
#define IDC_GEN_LANGUAGE       2050
#define IDC_GEN_START_WITH_WINDOWS 2051

#define IDC_ZC_COLOR_LABEL     2060
#define IDC_ZC_THICK_LABEL2    2061
#define IDC_ZC_REPARENT_LABEL  2062
#define IDC_ZC_THUMBNAIL_LABEL 2063
#define IDC_ZC_VIEWPORT_LABEL  2064
#define IDC_ZC_CLOSE_LABEL     2065

#define IDC_AOT_COLOR_LABEL    2070
#define IDC_AOT_OPACITY_LABEL2 2071
#define IDC_AOT_THICK_LABEL2   2072
#define IDC_AOT_HOTKEY_LABEL   2073

#define IDD_SETTINGS_OCR       2024
#define IDD_OCR_DOC_OPTIONS    2025
#define IDD_SETTINGS_SCREENSHOT 2026
#define IDD_OCR_PPOCRV6_OPTIONS 2027
#define IDD_OCR_MODEL_DOWNLOAD 2028
#define IDD_SETTINGS_TRANSLATE 2029
#define IDD_SETTINGS_TRANSLATE_PROVIDERS 2036
#define IDD_SETTINGS_TRANSLATE_PROMPT 2037
#define IDC_OCR_LANGUAGE       2080
#define IDC_OCR_MODE           2081
#define IDC_PADDLE_URL         2082
#define IDC_PADDLE_TOKEN       2083
#define IDC_PADDLE_TIMEOUT     2084
#define IDC_PADDLE_TIMEOUT_VAL 2085
#define IDC_PADDLE_TEST        2086
#define IDC_PADDLE_URL_LABEL   2090
#define IDC_PADDLE_TOKEN_LABEL 2091
#define IDC_PADDLE_TIMEOUT_LABEL 2092

#define IDC_PADDLE_LOCAL_DIR_LABEL   2101
#define IDC_PADDLE_LOCAL_DIR         2102
#define IDC_PADDLE_LOCAL_DIR_BROWSE  2103
#define IDC_PADDLE_LOCAL_PORT_LABEL  2104
#define IDC_PADDLE_LOCAL_PORT        2105
#define IDC_PADDLE_LOCAL_TEST        2106
#define IDC_PADDLE_LOCAL_PROMPT_LABEL 2108
#define IDC_PADDLE_LOCAL_PROMPT      2109

#define IDC_PADDLE_LOCAL_DOC         2110
#define IDC_PADDLE_LOCAL_LAYOUT_DIR  2111
#define IDC_PADDLE_LOCAL_LAYOUT_BROWSE 2112
#define IDC_PADDLE_LOCAL_LAYOUT_LABEL 2113

#define IDC_OCR_FONT_SIZE_LABEL      2114
#define IDC_OCR_FONT_SIZE            2115

#define IDC_HK_OCR_EDIT           2120
#define IDC_HK_OCR_CLEAR          2121
#define IDC_HK_SCREENSHOT_EDIT    2122
#define IDC_HK_SCREENSHOT_CLEAR   2123
#define IDC_OCR_HOTKEY_LABEL      2124
#define IDC_SCREENSHOT_HOTKEY_LABEL 2125
#define IDC_OCR_LANGUAGE_LABEL    2126
#define IDC_PADDLE_LOCAL_PORT_AUTO 2127
#define IDC_PADDLE_LOCAL_IMAGE_CROP 2128
#define IDC_OCR_RESULT_ON_TOP       2129
#define IDC_PADDLE_TASK_LABEL       2130
#define IDC_PADDLE_TASK             2131
#define IDC_PADDLE_CHART_RECOGNITION 2132
#define IDC_LAYOUT_THRESHOLD_LABEL  2133
#define IDC_LAYOUT_THRESHOLD_PROFILE 2134
#define IDC_PADDLE_LOCAL_DOC_OPTIONS 2135
#define IDC_DOC_RECOGNIZE_CHARTS    2136
#define IDC_DOC_RECOGNIZE_IMAGES    2137
#define IDC_DOC_RECOGNIZE_SEALS     2138
#define IDC_DOC_IGNORE_PAGE_DECORATIONS 2139
#define IDC_DOC_KEEP_FOOTNOTES      2140
#define IDC_PADDLE_LOCAL_IDLE_LABEL 2141
#define IDC_PADDLE_LOCAL_IDLE_TIMEOUT 2142
#define IDC_PADDLE_LOCAL_IDLE_UNIT  2143
#define IDC_DOC_USE_PHYSICAL_SORT   2144
#define IDC_LAYOUT_FAMILY_LABEL     2194
#define IDC_LAYOUT_MODEL_FAMILY     2195
#define IDC_LAYOUT_FAMILY_STATUS    2196
#define IDC_DOC_GROUPING_LABEL      2197
#define IDC_DOC_GROUPING_MODE       2198
#define IDC_DOC_MAX_TOKENS_LABEL    2199
#define IDC_DOC_MAX_TOKENS          2200

#define IDC_SS_FORMAT_LABEL         2150
#define IDC_SS_FORMAT               2151
#define IDC_SS_QUALITY_LABEL        2152
#define IDC_SS_QUALITY              2153
#define IDC_SS_QUALITY_VALUE        2154
#define IDC_SS_QUICK_SAVE_LABEL     2155
#define IDC_SS_QUICK_SAVE_DIR       2156
#define IDC_SS_QUICK_SAVE_BROWSE    2157
#define IDC_SS_HOTKEY_LABEL         2158
#define IDC_SS_INCLUDE_CURSOR       2159
#define IDC_SS_ENABLE_COLOR_PICKER  2160
// LongShot startup and auto-crop configuration.
#define IDC_SS_LONGSHOT_INIT_LABEL  2161
#define IDC_SS_LONGSHOT_INIT        2162
#define IDC_SS_LONGSHOT_AUTOCROP    2163

#define IDC_PPOCRV6_ADV_LABEL       2170
#define IDC_PPOCRV6_LIMIT_SIDE_LABEL 2171
#define IDC_PPOCRV6_LIMIT_SIDE      2172
#define IDC_PPOCRV6_LIMIT_TYPE_LABEL 2173
#define IDC_PPOCRV6_LIMIT_TYPE      2174
#define IDC_PPOCRV6_DET_THRESH_LABEL 2175
#define IDC_PPOCRV6_DET_THRESH      2176
#define IDC_PPOCRV6_BOX_THRESH_LABEL 2177
#define IDC_PPOCRV6_BOX_THRESH      2178
#define IDC_PPOCRV6_UNCLIP_LABEL    2179
#define IDC_PPOCRV6_UNCLIP          2180
#define IDC_PPOCRV6_REC_SCORE_LABEL 2181
#define IDC_PPOCRV6_REC_SCORE       2182
#define IDC_PPOCRV6_REC_BATCH_LABEL 2183
#define IDC_PPOCRV6_REC_BATCH       2184
#define IDC_PPOCRV6_OPTIONS         2185
#define IDC_PPOCRV6_PRESET_LABEL    2210
#define IDC_PPOCRV6_PRESET          2211
#define IDC_PPOCRV6_PRESET_APPLY    2212
#define IDC_PPOCRV6_HINT_LABEL      2213
#define IDC_PADDLE_LOCAL_DIR_DOWNLOAD 2214
#define IDC_MODEL_DOWNLOAD_BUNDLE    2220
#define IDC_MODEL_DOWNLOAD_LOCATION  2221
#define IDC_MODEL_DOWNLOAD_BROWSE    2222
#define IDC_MODEL_DOWNLOAD_SIZE      2223
#define IDC_MODEL_DOWNLOAD_INSTALLED 2224
#define IDC_MODEL_DOWNLOAD_PROGRESS  2225
#define IDC_MODEL_DOWNLOAD_STATUS    2226
#define IDC_MODEL_DOWNLOAD_DETAILS   2227
#define IDC_MODEL_DOWNLOAD_START     2228
#define IDC_MODEL_DOWNLOAD_CANCEL    2229
#define IDC_MODEL_DOWNLOAD_OPEN      2230
#define IDC_MODEL_DOWNLOAD_MIRROR    2231
#define IDC_TRANSLATE_ENABLED       2240
#define IDC_TRANSLATE_OCR_ROUTE     2241
#define IDC_TRANSLATE_SOURCE       2242
#define IDC_TRANSLATE_TARGET       2243
#define IDC_TRANSLATE_MODEL        2244
#define IDC_TRANSLATE_KEY          2245
#define IDC_TRANSLATE_KEY_STATUS   2246
#define IDC_TRANSLATE_KEY_REPLACE  2247
#define IDC_TRANSLATE_KEY_CLEAR    2248
#define IDC_TRANSLATE_TEST         2249
#define IDC_TRANSLATE_SHOW_SOURCE  2250
#define IDC_TRANSLATE_PARAGRAPHS   2251
#define IDC_TRANSLATE_ON_TOP       2252
#define IDC_TRANSLATE_WINDOW_BORDER 2253
#define IDC_TRANSLATE_LANGUAGES_LABEL 2256
#define IDC_TRANSLATE_SOURCE_LABEL    2257
#define IDC_TRANSLATE_TARGET_LABEL    2258
#define IDC_TRANSLATE_OCR_ROUTE_LABEL 2259
#define IDC_TRANSLATE_BACKEND_LABEL   2260
#define IDC_TRANSLATE_MODEL_LABEL     2261
#define IDC_TRANSLATE_KEY_LABEL       2262
#define IDC_TRANSLATE_NOTICE_TEXT     2263

#define IDC_TRANSLATE_PROVIDER        2264
#define IDC_TRANSLATE_PROMPT          2265
#define IDC_TRANSLATE_PROVIDER_MANAGE 2266
#define IDC_TRANSLATE_PROMPT_MANAGE   2267
#define IDC_TRANSLATE_SOURCE_FONT_SIZE 2268
#define IDC_TRANSLATE_SOURCE_FONT_SIZE_LABEL 2269

#define IDC_PROVIDER_PROFILE          2270
#define IDC_PROVIDER_ADD              2271
#define IDC_PROVIDER_DUPLICATE       2272
#define IDC_PROVIDER_DELETE          2273
#define IDC_PROVIDER_ENDPOINT         2275
#define IDC_PROVIDER_AUTH_MODE        2276
#define IDC_PROVIDER_KEY              2277
#define IDC_PROVIDER_KEY_STATUS       2278
#define IDC_PROVIDER_KEY_ACTION       2279
#define IDC_PROVIDER_KEY_CLEAR        2280
#define IDC_PROVIDER_MODEL            2281
#define IDC_PROVIDER_CUSTOM_MODEL     2282
#define IDC_PROVIDER_REASONING        2283
#define IDC_PROVIDER_TEMPERATURE      2284
#define IDC_PROVIDER_TEST             2285
#define IDC_PROVIDER_ADVANCED         2286
#define IDC_PROVIDER_DATA_ROUTE       2287
#define IDC_PROVIDER_RESET            2291
#define IDC_PROVIDER_NAME             2292
#define IDC_PROVIDER_TEST_STATUS      2294
#define IDC_PROVIDER_ENABLED          2295

#define IDC_PROMPT_PROFILE            2300
#define IDC_PROMPT_ADD                2301
#define IDC_PROMPT_COPY               2302
#define IDC_PROMPT_DELETE             2303
#define IDC_PROMPT_NAME               2304
#define IDC_PROMPT_STYLE              2305
#define IDC_PROMPT_PREVIEW            2306
#define IDC_PROMPT_RESET              2307

#define IDC_OCR_ALT_ROUTE_LABEL     2186
#define IDC_OCR_ALT_ROUTE           2187
#define IDC_OCR_ALT_HOTKEY_LABEL    2188
#define IDC_HK_OCR_ALT_EDIT         2189
#define IDC_HK_OCR_ALT_CLEAR        2190
#define IDC_OCR_ALT_IDLE_LABEL      2191
#define IDC_OCR_ALT_IDLE_TIMEOUT    2192
#define IDC_OCR_ALT_IDLE_UNIT       2193

#define IDR_CURSOR_ROTATE_0         3001
#define IDR_CURSOR_ROTATE_45        3002
#define IDR_CURSOR_ROTATE_90        3003
#define IDR_CURSOR_ROTATE_135       3004
#define IDR_CURSOR_ROTATE_180       3005
#define IDR_CURSOR_ROTATE_225       3006
#define IDR_CURSOR_ROTATE_270       3007
#define IDR_CURSOR_ROTATE_315       3008
#define IDR_CURSOR_RECT_ROUND       3009

struct AppLanguage {
    enum Value { Auto, English, Chinese };
    Value value = Auto;
};

struct GeneralSettings {
    AppLanguage language;
    bool showTitlebar = false;
};

struct AotSettings {
    bool showBorder = true;
    bool customColor = true;
    COLORREF color = RGB(0, 120, 215);
    int opacity = 100;
    int thickness = 4;
    bool roundedCorners = true;
    int inset = 1;
};

struct OverlaySettings {
    COLORREF color = RGB(255, 0, 0);
    int thickness = 3;
    bool cropOnTop = true;
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
};

struct HotkeyConfig {
    bool win = false;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    unsigned char key = 0;

    bool IsEmpty() const { return key == 0; }
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
};

enum class TranslationAdapterKind {
    DeepSeekChat,
    OpenAIChatCompletions,
    OllamaChat,
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
    None,
};

inline constexpr int kTranslationSettingsSchemaVersion = 3;
inline constexpr double kTranslationPreviewZoomMin = 0.25;
inline constexpr double kTranslationPreviewZoomMax = 5.0;
inline constexpr int kTranslationSourceFontSizeMin = 8;
inline constexpr int kTranslationSourceFontSizeMax = 32;
inline constexpr wchar_t kLegacyTranslationCredentialTarget[] =
    L"ZenCrop/Translation/deepseek";
inline constexpr wchar_t kDefaultTranslationProviderId[] =
    L"builtin.deepseek.default";
inline constexpr wchar_t kDefaultTranslationPromptId[] =
    L"builtin.accurate.v1";

struct TranslationProviderProfile {
    std::wstring id;
    std::wstring displayName;
    std::wstring presetKind;
    TranslationAdapterKind adapterKind = TranslationAdapterKind::DeepSeekChat;
    bool enabled = true;
    TranslationAuthMode authMode = TranslationAuthMode::BearerApiKey;
    std::wstring baseUrlOverride;
    std::wstring model = L"deepseek-v4-flash";
    bool customModel = false;
    std::wstring credentialRef = kLegacyTranslationCredentialTarget;
    TranslationReasoningMode reasoningMode = TranslationReasoningMode::Off;
    std::optional<double> temperature;
    std::wstring advancedOptionsJson = L"{}";
};

struct TranslationPromptProfile {
    std::wstring id;
    std::wstring name;
    std::wstring styleInstruction;
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
    };

struct TranslationSettings {
    // schemaVersion is persisted with the translation section. schemaSupported
    // is runtime-only: a future section remains untouched rather than being
    // silently downgraded by this build.
    int schemaVersion = kTranslationSettingsSchemaVersion;
    bool schemaSupported = true;
    bool enabled = false;
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
        profile.displayName = L"DeepSeek - Default";
        profile.presetKind = L"deepseek";
        profile.adapterKind = TranslationAdapterKind::DeepSeekChat;
        profile.authMode = TranslationAuthMode::BearerApiKey;
        profile.credentialRef = kLegacyTranslationCredentialTarget;
        profile.model = L"deepseek-v4-flash";
        profile.reasoningMode = TranslationReasoningMode::Off;
        profile.temperature = 1.3;
        providerProfiles.push_back(std::move(profile));

        for (const auto& item : kBuiltInOpenAiCompatibleProviderDefaults) {
            TranslationProviderProfile provider;
            provider.id = item.id;
            provider.displayName = item.displayName;
            provider.presetKind = item.presetKind;
            provider.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
            provider.authMode = TranslationAuthMode::BearerApiKey;
            provider.credentialRef = L"ZenCrop/Translation/provider/" + provider.id;
            provider.model = item.model;
            provider.reasoningMode = TranslationReasoningMode::ProviderDefault;
            provider.temperature = 0.3;
            providerProfiles.push_back(std::move(provider));
        }
    }
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

GeneralSettings LoadGeneralSettings();
void SaveGeneralSettings(const GeneralSettings& settings);
AotSettings LoadAotSettings();
void SaveAotSettings(const AotSettings& settings);
OverlaySettings LoadOverlaySettings();
void SaveOverlaySettings(const OverlaySettings& settings);
HotkeySettings LoadHotkeySettings();
void SaveHotkeySettings(const HotkeySettings& settings);
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
void ShowSettingsDialog(HWND parent);

// Aggregate of all settings pages, shared across PropertySheet page procs.
// Moved to the header so SettingsDialog.cpp can access it without redefining.
struct SharedSettings {
    GeneralSettings general;
    AotSettings aot;
    OverlaySettings overlay;
    ScreenshotSettings screenshot;
    HotkeySettings hotkeys;
    TranslationSettings translation;
};

SharedSettings& GetSharedSettings();
