#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <exception>
#include "JsonUtils.h"
#include "BitmapUtils.h"
#include "OcrTableUtils.h"
#include "OcrBlock.h"

enum class OcrEmbeddedAssetSourceKind {
    CanonicalCrop,
    ProviderEncodedBytes,
};

enum class OcrEmbeddedAssetEncodedFormat {
    Unknown,
    Png,
    Jpeg,
    WebP,
};

// Engine-owned description of a user-visible Markdown image. The engine does
// not choose a storage root or Output format; the owning job materializes this
// spec after recognition. This keeps recognition transport encoding separate
// from exported assets and avoids an intermediate lossy cache file.
struct OcrEmbeddedAssetSpec {
    std::wstring id;
    OcrEmbeddedAssetSourceKind sourceKind = OcrEmbeddedAssetSourceKind::CanonicalCrop;
    std::wstring semanticClass;
    RECT cropRect = {};
    int localOrder = 0;
    std::wstring placeholderUri;
    std::wstring altText;
    int widthPercent = 100;
    std::vector<unsigned char> providerBytes;
    OcrEmbeddedAssetEncodedFormat providerFormat = OcrEmbeddedAssetEncodedFormat::Unknown;
};

struct OcrOutput {
    bool success = false;
    std::wstring text;
    std::wstring error;
    RECT cropRect = {};
    DWORD elapsedMs = 0;
    std::wstring imagePath;
    std::vector<RECT> bboxes;
    std::vector<std::wstring> bboxClasses;
    std::vector<OcrLayoutBlock> blocks;
    std::vector<OcrEmbeddedAssetSpec> embeddedAssets;
    std::wstring rawOcrJson;
    std::wstring debugOutputImagesJson;
    // Diagnostic routing metadata; not part of OCR document content.
    std::wstring engineMode;
    // Ephemeral engine telemetry used by integration tests and diagnostics;
    // it is not user OCR content and is not persisted as a document artifact.
    std::wstring diagnosticsJson;
};

struct OcrParams {
    HBITMAP hBitmap;
    std::function<void(OcrOutput)> callback;
};

// OCR engines invoke user-owned callbacks from worker threads (and sometimes
// synchronously when a worker cannot be created). Keep exceptions at that
// engine boundary so a coordinator/UI callback cannot tear down the worker
// before the engine releases its bitmap and parameter state.
inline void InvokeOcrCallbackSafely(
    const std::function<void(OcrOutput)>& callback,
    const OcrOutput& result) noexcept
{
    if (!callback) return;
    try {
        callback(result);
    } catch (const std::exception&) {
        OutputDebugStringW(L"[OCR] OCR callback threw a standard exception.\n");
    } catch (...) {
        OutputDebugStringW(L"[OCR] OCR callback threw an unknown exception.\n");
    }
}

struct VlmResponse {
    bool success = false;
    std::wstring content;
    std::wstring error;
    std::wstring finishReason;
    int promptTokens = -1;
    int completionTokens = -1;
    int totalTokens = -1;
};

VlmResponse ParseVlmResponse(const std::string& jsonBody);

std::wstring GetOcrImageDir();
std::wstring GetOcrImageDateDir(const SYSTEMTIME& st);
std::wstring GetOcrImageDateDir();
std::wstring UrlEncode(const std::wstring& s);
std::wstring NormalizeEditText(const std::wstring& text);
// Copy-text routes have no asset owner. Remove engine-only embedded image
// markup so an unresolved zencrop-asset URI can never reach the clipboard.
std::wstring StripOcrEmbeddedAssetMarkup(
    const std::wstring& text,
    const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets);
std::wstring ParsePaddleTextResponse(const std::string& json);
std::wstring ParsePaddleVlResponse(const std::string& json);
std::wstring ParsePaddleResponse(const std::string& json);

// Optional injectible HTTP downloader for ProcessImagesInResponse tests.
// Production uses a safe HTTPS client with per-image deadline and retries.
using ProviderImageHttpGetFn = bool (*)(
    const std::wstring& url,
    std::vector<unsigned char>& bytes,
    std::wstring& contentType,
    int& statusCode,
    std::wstring& error);

std::wstring ProcessImagesInResponse(
    const std::string& json,
    std::wstring& processedText,
    std::vector<OcrEmbeddedAssetSpec>* embeddedAssets,
    ProviderImageHttpGetFn httpGetOverride = nullptr);
bool HttpDownloadFile(const std::wstring& url, const std::wstring& savePath);
void CleanOcrImageDir();

struct LayoutClassInfo {
    int id;
    const wchar_t* name;
    const wchar_t* vlmPrompt;
    int headingLevel;
    bool skipRecognition;
    bool cropImage;
    bool ignoreInMarkdown;
};
const LayoutClassInfo* GetLayoutClassInfo(int classId);
constexpr int LAYOUT_NUM_CLASSES = 25;
