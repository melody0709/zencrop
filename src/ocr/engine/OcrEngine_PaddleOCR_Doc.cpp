#include "OcrEngine_PaddleOCR_Doc.h"
#include "LayoutEngine.h"
#include "PaddleDocRecognitionImage.h"
#include "PaddleDocRegionGrouping.h"
#include "PaddleDocEnginePolicy.h"
#include "PaddleVlLlamaClient.h"
#include "Settings.h"
#include "OcrUtils.h"
#include "OcrLayoutBlocksFromRegions.h"
#include "core/PaddleVlServerService.h"
#include "core/OcrModelRegistry.h"
#include <algorithm>
#include <shlwapi.h>
#include <mutex>
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"
#include "core/NarrowStringUtils.h"

#pragma comment(lib, "shlwapi.lib")

namespace {
struct LlamaRequestScope {
    explicit LlamaRequestScope(PaddleVlServerService* service) : service(service) {
        if (service) service->BeginRequest();
    }
    ~LlamaRequestScope() { if (service) service->EndRequest(); }
    PaddleVlServerService* service = nullptr;
};

struct PaddleDocParams {
    HBITMAP hBitmap = nullptr;
    std::function<void(OcrOutput)> callback;
    PaddleVlServerService* server = nullptr;
};

struct CriticalSectionScope {
    explicit CriticalSectionScope(CRITICAL_SECTION& section) : section(section) {
        EnterCriticalSection(&section);
    }
    ~CriticalSectionScope() { LeaveCriticalSection(&section); }
    CRITICAL_SECTION& section;
};

#ifdef ZENCROP_PADDLE_DOC_CROP_ENCODING_BENCHMARK
PaddleVlImageEncoding s_benchmarkCropEncoding = PaddleVlImageEncoding::Png;
#endif

}

#ifdef ZENCROP_PADDLE_DOC_CROP_ENCODING_BENCHMARK
void SetPaddleDocBenchmarkCropEncoding(PaddleVlImageEncoding encoding) {
    s_benchmarkCropEncoding = encoding;
}
#endif

LayoutEngine OcrEnginePaddleDoc::s_layoutEngine;
std::wstring OcrEnginePaddleDoc::s_layoutModelPath;
std::wstring OcrEnginePaddleDoc::s_layoutModelFamilySetting;
CRITICAL_SECTION OcrEnginePaddleDoc::s_layoutCs;

OcrEnginePaddleDoc::OcrEnginePaddleDoc(PaddleVlServerService* server)
    : m_server(server) {
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        InitializeCriticalSection(&s_layoutCs);
    });
    if (m_server) m_server->SetShutdownHook(&OcrEnginePaddleDoc::CleanupLayoutEngine);
}

OcrEnginePaddleDoc::~OcrEnginePaddleDoc() {
}

void OcrEnginePaddleDoc::CleanupLayoutEngine() {
    s_layoutEngine.Reset();
    s_layoutModelPath.clear();
    s_layoutModelFamilySetting.clear();
}

void OcrEnginePaddleDoc::GlobalCleanup() {
    CleanupLayoutEngine();
    static bool cleaned = false;
    if (!cleaned) {
        DeleteCriticalSection(&s_layoutCs);
        cleaned = true;
    }
}

bool OcrEnginePaddleDoc::FindLayoutModel(std::wstring& layoutPath) {
    OcrSettings settings = LoadOcrSettings();
    return OcrModelRegistryFindDocLayoutModel(
        OcrModelRegistryBuildPlan(settings, OcrModelRegistryProcessDir()), layoutPath);
}

bool OcrEnginePaddleDoc::IsAvailable() {
    auto* server = m_server;
    std::wstring serverExe, modelPath, mmprojPath;
    std::wstring layoutPath;
    return server && server->FindServerExe(serverExe) &&
           server->FindModelFiles(modelPath, mmprojPath) &&
           FindLayoutModel(layoutPath);
}

PaddleVlLlamaResult OcrEnginePaddleDoc::RecognizeRegionDetailedStatic(
    PaddleVlServerService* server, HBITMAP hRegion, const std::wstring& prompt, int port, int maxTokens,
    PaddleVlImageEncoding encoding)
{
    if (!server) {
        PaddleVlLlamaResult result;
        result.error = L"PaddleVL server service is unavailable.";
        return result;
    }
    // OWN-121: pure localhost chat URL (WideStringUtils).
    std::wstring url = WideFormatLocalhostChatCompletions(port);
    const std::wstring effectivePrompt = prompt.empty() ? L"OCR:" : prompt;
    return SendPaddleVlLlamaRequest(
        hRegion,
        url,
        server->GetModelName(),
        effectivePrompt,
        effectivePrompt == L"Table Recognition:",
        120000,
        maxTokens == 8192 ? 8192 : 4096,
        encoding);
}

static bool AreRegionsInSameHorizontalBand(const RECT& a, const RECT& b) {
    LONG centerA = (a.top + a.bottom) / 2;
    LONG centerB = (b.top + b.bottom) / 2;
    if (centerA >= b.top && centerA <= b.bottom) return true;
    if (centerB >= a.top && centerB <= a.bottom) return true;
    
    LONG overlapTop = (std::max)(a.top, b.top);
    LONG overlapBottom = (std::min)(a.bottom, b.bottom);
    if (overlapTop < overlapBottom) {
        LONG overlapHeight = overlapBottom - overlapTop;
        LONG minHeight = (std::min)(a.bottom - a.top, b.bottom - b.top);
        if (minHeight > 0 && (float)overlapHeight / (float)minHeight > 0.4f) {
            return true;
        }
    }
    return false;
}

static std::wstring CleanFormulaNumber(const std::wstring& s) {
    std::wstring r = s;
    while (!r.empty() && (r.front() == L'\n' || r.front() == L'\r' || r.front() == L' ' || r.front() == L'\t')) r.erase(0, 1);
    while (!r.empty() && (r.back() == L'\n' || r.back() == L'\r' || r.back() == L' ' || r.back() == L'\t')) r.pop_back();
    if (r.size() >= 2) {
        if ((r.front() == L'(' && r.back() == L')') ||
            (r.front() == L'\uff08' && r.back() == L'\uff09') ||
            (r.front() == L'[' && r.back() == L']') ||
            (r.front() == L'\u3010' && r.back() == L'\u3011')) {
            r = r.substr(1, r.size() - 2);
        }
    }
    return r;
}

std::wstring OcrEnginePaddleDoc::AssembleMarkdown(
    const std::vector<LayoutRegion>& regions,
    const std::vector<std::wstring>& texts,
    HBITMAP hOriginalBitmap,
    std::vector<OcrEmbeddedAssetSpec>* embeddedAssets)
{
    std::wstring result;

    if (embeddedAssets) embeddedAssets->clear();

    BITMAP sourceBitmap = {};
    GetObject(hOriginalBitmap, sizeof(sourceBitmap), &sourceBitmap);
    auto appendEmbeddedAsset = [&](size_t regionIndex, const LayoutRegion& region) {
        if (!embeddedAssets || sourceBitmap.bmWidth <= 0) return;

        OcrEmbeddedAssetSpec asset;
        asset.localOrder = static_cast<int>(embeddedAssets->size()) + 1;
        // OWN-124: pure page/layout asset id + int labels (WideStringUtils).
        asset.id = WideFormatPageLayoutAssetId(regionIndex + 1);
        asset.semanticClass = region.className.empty() ? L"image" : region.className;
        asset.cropRect = region.bbox;
        asset.placeholderUri = L"zencrop-asset://page-local/asset_" +
            WideFormatIntLabel(asset.localOrder);
        asset.altText = asset.semanticClass;
        if (!asset.altText.empty()) {
            asset.altText[0] = static_cast<wchar_t>(towupper(asset.altText[0]));
        }
        const int cropWidth = region.bbox.right - region.bbox.left;
        asset.widthPercent = static_cast<int>(
            static_cast<float>(cropWidth) * 100.0f /
            static_cast<float>(sourceBitmap.bmWidth) + 0.5f);
        asset.widthPercent = (std::max)(5, (std::min)(100, asset.widthPercent));

        result += L"<div style=\"text-align: center;\"><img src=\"" +
            asset.placeholderUri + L"\" alt=\"" + asset.altText +
            L"\" width=\"" + WideFormatIntLabel(asset.widthPercent) +
            L"%\" /></div>";
        embeddedAssets->push_back(std::move(asset));
    };

    OcrSettings ocrSettings = LoadOcrSettings();
    bool imageCropEnabled = ocrSettings.enableImageCrop;

    std::vector<std::wstring> formulaTags(regions.size(), L"");
    std::vector<bool> skipped(regions.size(), false);

    // Pre-pass: associate formula_number (classId 11) with any formula (vlmPrompt == "Formula Recognition:") in the immediate sorting neighborhood (i-1 or i+1)
    for (size_t i = 0; i < regions.size() && i < texts.size(); i++) {
        if (regions[i].classId == 11) { // formula_number
            std::wstring rawNum = texts[i];
            std::wstring cleanNum = CleanFormulaNumber(rawNum);
            if (cleanNum.empty()) continue;

            size_t matchIdx = (size_t)-1;

            // 1. Try to match with the preceding region (i-1)
            if (i > 0 && regions[i - 1].vlmPrompt == L"Formula Recognition:") {
                if (AreRegionsInSameHorizontalBand(regions[i].bbox, regions[i - 1].bbox)) {
                    matchIdx = i - 1;
                }
            }
            // 2. Try to match with the succeeding region (i+1)
            if (matchIdx == (size_t)-1 && i + 1 < regions.size() && regions[i + 1].vlmPrompt == L"Formula Recognition:") {
                if (AreRegionsInSameHorizontalBand(regions[i].bbox, regions[i + 1].bbox)) {
                    matchIdx = i + 1;
                }
            }

            if (matchIdx != (size_t)-1) {
                formulaTags[matchIdx] = cleanNum;
                skipped[i] = true;
            }
        }
    }

    // Count how many valid text-yielding regions we have to decide whether to demote headings
    size_t validTextRegionsCount = 0;
    for (size_t i = 0; i < regions.size() && i < texts.size(); i++) {
        if (skipped[i]) continue;
        const auto& region = regions[i];
        if (PaddleDocShouldIgnoreRegionInMarkdown(region, ocrSettings)) continue;

        auto text = texts[i];
        bool canEmitImageOnly = imageCropEnabled &&
            (region.className == L"image" || region.className == L"chart" || region.className == L"seal");
        if (text.empty() && !canEmitImageOnly) continue;

        if (region.className == L"image" || region.className == L"chart" || region.className == L"seal") {
            continue;
        }
        validTextRegionsCount++;
    }
    bool demoteHeadings = (validTextRegionsCount <= 1);

    for (size_t i = 0; i < regions.size() && i < texts.size(); i++) {
        if (skipped[i]) continue;

        const auto& region = regions[i];
        if (PaddleDocShouldIgnoreRegionInMarkdown(region, ocrSettings)) continue;

        auto text = texts[i];
        bool canEmitImageOnly = imageCropEnabled &&
            (region.className == L"image" || region.className == L"chart" || region.className == L"seal");
        if (text.empty() && !canEmitImageOnly) continue;

        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
            text.pop_back();
        while (!text.empty() && (text.front() == L'\n' || text.front() == L'\r' || text.front() == L' '))
            text.erase(0, 1);

        if (region.className == L"image" || region.className == L"chart") {
            if (imageCropEnabled) {
                appendEmbeddedAsset(i, region);
                result += L"\n\n";
            }
            if (!text.empty()) {
                result += text + L"\n\n";
            }
            continue;
        }

        if (region.className == L"seal") {
            if (imageCropEnabled) {
                appendEmbeddedAsset(i, region);
                result += L"\n\n";
            }
            if (!text.empty()) {
                result += text + L"\n\n";
            }
            continue;
        }

        if (text.empty()) continue;

        if (region.className == L"algorithm") {
            result += L"```\n" + text + L"\n```\n\n";
            continue;
        }

        if (region.vlmPrompt == L"Table Recognition:") {
            text = ConvertOTSLToHTML(text);
            result += text + L"\n\n";
            continue;
        }

        if (region.vlmPrompt == L"Formula Recognition:") {
            auto stripMathDelimiters = [](const std::wstring& s) -> std::wstring {
                std::wstring r = s;
                auto stripPair = [&r](const std::wstring& open, const std::wstring& close) {
                    if (r.size() < open.size() + close.size()) return;
                    if (r.substr(0, open.size()) == open &&
                        r.substr(r.size() - close.size()) == close) {
                        r = r.substr(open.size(), r.size() - open.size() - close.size());
                    }
                };
                stripPair(L"$$", L"$$");
                stripPair(L"\\[", L"\\]");
                stripPair(L"\\(", L"\\)");
                stripPair(L"$", L"$");
                while (!r.empty() && (r.front() == L'\n' || r.front() == L'\r' || r.front() == L' '))
                    r.erase(0, 1);
                while (!r.empty() && (r.back() == L'\n' || r.back() == L'\r' || r.back() == L' '))
                    r.pop_back();
                return r;
            };
            text = stripMathDelimiters(text);
            if (!formulaTags[i].empty()) {
                text += L" \\tag{" + formulaTags[i] + L"}";
            }
            result += L"$$\n" + text + L"\n$$\n\n";
            continue;
        }

        if (region.headingLevel >= 1 && region.headingLevel <= 6) {
            if (demoteHeadings) {
                text = SplitParagraphs(text);
                result += text + L"\n\n";
            } else {
                std::wstring prefix(region.headingLevel, L'#');
                result += prefix + L" " + text + L"\n\n";
            }
        } else {
            text = SplitParagraphs(text);
            result += text + L"\n\n";
        }
    }

    return result;
}

OcrOutput OcrEnginePaddleDoc::DoRecognizeOfficial(HBITMAP hBitmap) {
    OcrOutput result;
    auto* server = m_server;
    if (!server) {
        result.error = L"PaddleVL server service is unavailable.";
        return result;
    }
    LlamaRequestScope llamaScope(server);
    const ULONGLONG pipelineStart = GetTickCount64();

    if (!server->EnsureServerStarted()) {
        result.error = L"Failed to start llama-server.";
        return result;
    }
    const int port = server->GetPort();
    const OcrSettings settings = LoadOcrSettings();
    // OWN-121: pure localhost base URL (WideStringUtils).
    const std::wstring serverBaseUrl = WideFormatLocalhostBase(port);
    const PaddleVlLlamaServerInfo serverInfo = ProbePaddleVlLlamaServer(
        serverBaseUrl, server->GetModelName(), 5000);
    // OWN-118: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPaddleServerProbe(
        "PaddleDoc",
        serverInfo.modelsReachable ? 1 : 0, serverInfo.modelsHttpStatus,
        serverInfo.propsReachable ? 1 : 0, serverInfo.propsHttpStatus,
        serverInfo.modelListed ? 1 : 0, serverInfo.multimodal ? 1 : 0,
        serverInfo.totalSlots, serverInfo.slotContext,
        serverInfo.warning.c_str()).c_str());

    std::wstring capabilityError;
    if (!ValidatePaddleVlLlamaServerCapability(serverInfo, capabilityError)) {
        result.error = capabilityError;
        return result;
    }
    const DWORD serverSetupMs = static_cast<DWORD>(
        GetTickCount64() - pipelineStart);

    std::wstring layoutPath;
    if (!FindLayoutModel(layoutPath)) {
        result.error = L"Layout model not found. Place PP-DocLayoutV3.onnx in the model directory.";
        return result;
    }

    const ULONGLONG layoutStart = GetTickCount64();
    LayoutDetectionDiagnostics layoutDiagnostics;
    std::vector<LayoutRegion> regions;
    {
        CriticalSectionScope layoutLock(s_layoutCs);
        if (PaddleDocLayoutCacheNeedsReload(
                s_layoutEngine.IsAvailable(), s_layoutModelPath,
                s_layoutModelFamilySetting, layoutPath, settings.layoutModelFamily)) {
            s_layoutEngine.Reset();
            if (!s_layoutEngine.Initialize(layoutPath)) {
                result.error = L"Failed to load layout model. Check ONNX Runtime installation.";
                return result;
            }
            s_layoutModelPath = layoutPath;
            s_layoutModelFamilySetting = settings.layoutModelFamily;
        }
        regions = s_layoutEngine.Detect(hBitmap, &layoutDiagnostics);
    }
    const DWORD layoutMs = static_cast<DWORD>(GetTickCount64() - layoutStart);
    if (!layoutDiagnostics.error.empty()) {
        result.error = L"Layout inference failed: " + std::wstring(
            layoutDiagnostics.error.begin(), layoutDiagnostics.error.end());
        return result;
    }
    // OWN-117: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPaddleDocLayoutDetected(regions.size()).c_str());

    if (regions.empty()) {
        auto wholePage = RecognizeRegionDetailedStatic(
            server, hBitmap, L"OCR:", port, settings.paddleVlMaxTokens);
        if (!wholePage.success || TrimString(wholePage.content).empty()) {
            result.error = wholePage.error.empty() ? L"No text recognized." : wholePage.error;
            return result;
        }
        result.text = wholePage.content;
        result.success = true;
        return result;
    }

    BITMAP bitmap = {};
    GetObject(hBitmap, sizeof(bitmap), &bitmap);

    // Heading demotion is presentation metadata only. Detection geometry and
    // region cardinality stay identical for overlay/history serialization.
    auto markdownRegions = regions;
    for (auto& region : markdownRegions) {
        if (region.headingLevel <= 0) continue;
        const float heightRatio = bitmap.bmHeight > 0
            ? static_cast<float>(region.bbox.bottom - region.bbox.top) / bitmap.bmHeight
            : 0.0f;
        if (region.headingLevel == 1) {
            region.headingLevel = heightRatio > 0.05f ? 1 : 2;
        } else if (region.headingLevel == 2) {
            region.headingLevel = heightRatio > 0.03f ? 2 : 3;
        }
    }

    PaddleDocGroupingOptions groupingOptions;
    groupingOptions.mode = settings.paddleDocGroupingMode;
    groupingOptions.recognizeCharts = settings.docRecognizeCharts;
    groupingOptions.recognizeImages = settings.docRecognizeImages;
    groupingOptions.recognizeSeals = settings.docRecognizeSeals;
    groupingOptions.legacyVerticalThreshold = bitmap.bmHeight / 20;
    PaddleDocGroupingStats groupingStats;
    const ULONGLONG groupingStart = GetTickCount64();
    const PaddleDocRecognitionPlan plan = BuildPaddleDocRecognitionPlan(
        regions, groupingOptions, &groupingStats);
    const DWORD groupingMs = static_cast<DWORD>(
        GetTickCount64() - groupingStart);

    struct GroupRuntime {
        HBITMAP crop = nullptr;
        PaddleDocRecognitionImageStats imageStats;
        PaddleVlLlamaResult response;
        int attempts = 0;
        bool skipped = false;
    };
    std::vector<GroupRuntime> runtime(plan.groups.size());
    std::vector<size_t> pending;
    pending.reserve(plan.groups.size());
    const ULONGLONG cropStart = GetTickCount64();
    for (size_t index = 0; index < plan.groups.size(); ++index) {
        const auto& group = plan.groups[index];
        if (group.contentOwnerIndex >= regions.size() ||
            PaddleDocShouldSkipVlmForRegion(regions[group.contentOwnerIndex], settings)) {
            runtime[index].skipped = true;
            continue;
        }
        runtime[index].crop = ComposePaddleDocRecognitionGroup(
            hBitmap, regions, group, &runtime[index].imageStats);
        if (runtime[index].crop) {
            pending.push_back(index);
        } else {
            runtime[index].response.error = L"Failed to compose recognition crop.";
            runtime[index].response.metrics.errorCategory = L"crop_compose";
        }
    }
    const DWORD cropMs = static_cast<DWORD>(GetTickCount64() - cropStart);

    auto dispatch = [&](const std::vector<size_t>& groupIndices) {
        constexpr size_t kMaxConcurrent = 3;
        for (size_t start = 0; start < groupIndices.size(); start += kMaxConcurrent) {
            const size_t end = (std::min)(groupIndices.size(), start + kMaxConcurrent);
            struct GroupTask {
                HBITMAP crop;
                std::wstring prompt;
                int port;
                int maxTokens;
                PaddleVlImageEncoding encoding;
                PaddleVlServerService* server;
                PaddleVlLlamaResult* response;
                int* attempts;
            };
            std::vector<GroupTask> tasks;
            tasks.reserve(end - start);
            for (size_t position = start; position < end; ++position) {
                const size_t groupIndex = groupIndices[position];
                PaddleVlImageEncoding encoding =
                    plan.groups[groupIndex].useLegacyUnionCrop &&
                        plan.groups[groupIndex].prompt != L"Formula Recognition:" &&
                        plan.groups[groupIndex].prompt != L"Table Recognition:"
                        ? PaddleVlImageEncoding::LegacyJpeg95
                        : PaddleVlImageEncoding::Png;
#ifdef ZENCROP_PADDLE_DOC_CROP_ENCODING_BENCHMARK
                encoding = s_benchmarkCropEncoding;
#endif
                tasks.push_back({
                    runtime[groupIndex].crop,
                    plan.groups[groupIndex].prompt,
                    port,
                    settings.paddleVlMaxTokens,
                    encoding,
                    server,
                    &runtime[groupIndex].response,
                    &runtime[groupIndex].attempts,
                });
            }
            auto worker = [](LPVOID parameter) -> DWORD {
                auto* task = static_cast<GroupTask*>(parameter);
                ++*task->attempts;
                *task->response = RecognizeRegionDetailedStatic(
                    task->server, task->crop, task->prompt, task->port, task->maxTokens,
                    task->encoding);
                return 0;
            };
            std::vector<HANDLE> handles;
            handles.reserve(tasks.size());
            for (auto& task : tasks) {
                HANDLE thread = CreateThread(nullptr, 0, worker, &task, 0, nullptr);
                if (thread) handles.push_back(thread);
                else worker(&task);
            }
            if (!handles.empty()) {
                WaitForMultipleObjects(
                    static_cast<DWORD>(handles.size()), handles.data(), TRUE, INFINITE);
                for (HANDLE thread : handles) CloseHandle(thread);
            }
        }
    };

    const ULONGLONG vlmStart = GetTickCount64();
    dispatch(pending);
    std::vector<size_t> retry;
    for (size_t groupIndex : pending) {
        if (ShouldRetryPaddleVlLlamaFailure(runtime[groupIndex].response)) {
            retry.push_back(groupIndex);
        }
    }
    dispatch(retry);
    const DWORD vlmWallMs = static_cast<DWORD>(GetTickCount64() - vlmStart);

    std::vector<std::wstring> texts(regions.size());
    size_t recognizedGroups = 0;
    size_t failedGroups = 0;
    size_t skippedGroups = 0;
    size_t totalPngBytes = 0;
    size_t totalImageBytes = 0;
    std::wstring firstFailure;
    std::wstring groupsDiagnostics = L"[";
    for (size_t index = 0; index < plan.groups.size(); ++index) {
        const auto& group = plan.groups[index];
        auto& item = runtime[index];
        if (item.response.success && group.contentOwnerIndex < texts.size()) {
            texts[group.contentOwnerIndex] = item.response.content;
            ++recognizedGroups;
        } else if (item.skipped) {
            ++skippedGroups;
        } else {
            ++failedGroups;
            if (firstFailure.empty()) {
                firstFailure = item.response.error.empty()
                    ? item.response.metrics.errorCategory
                    : item.response.error;
            }
        }
        totalPngBytes += item.response.metrics.pngBytes;
        totalImageBytes += item.response.metrics.imageBytes;
        if (index > 0) groupsDiagnostics += L",";
        // OWN-124: pure compact JSON int/ull fields (WideStringUtils).
        groupsDiagnostics += L"{\"id\":\"" + EscapeJsonString(group.id) +
            L"\"," + WideJsonFieldUllCompact(L"members", (unsigned long long)group.regionIndices.size()) +
            L"," + WideJsonFieldIntCompact(L"ownerIndex", group.contentOwnerIndex) +
            L"," + WideJsonFieldIntCompact(L"attempts", item.attempts) +
            L",\"success\":" + (WideJsonBoolLiteral(item.response.success)) +
            L",\"skipped\":" + (WideJsonBoolLiteral(item.skipped)) +
            L"," + WideJsonFieldUllCompact(L"elapsedMs", (unsigned long long)item.response.metrics.elapsedMs) +
            L"," + WideJsonFieldUllCompact(L"imageBytes", (unsigned long long)item.response.metrics.imageBytes) +
            L"," + WideJsonFieldUllCompact(L"pngBytes", (unsigned long long)item.response.metrics.pngBytes) +
            L",\"imageMime\":\"" + EscapeJsonString(item.response.metrics.imageMime) +
            L"\"" +
            L"," + WideJsonFieldUllCompact(L"requestBuildUs", (unsigned long long)item.response.metrics.requestBuildUs) +
            L"," + WideJsonFieldUllCompact(L"requestBytes", (unsigned long long)item.response.metrics.requestBytes) +
            L"," + WideJsonFieldUllCompact(L"responseBytes", (unsigned long long)item.response.metrics.responseBytes) +
            L"," + WideJsonFieldIntCompact(L"promptTokens", item.response.metrics.promptTokens) +
            L"," + WideJsonFieldIntCompact(L"completionTokens", item.response.metrics.completionTokens) +
            L"," + WideJsonFieldIntCompact(L"totalTokens", item.response.metrics.totalTokens) +
            L",\"finishReason\":\"" + EscapeJsonString(item.response.metrics.finishReason) +
            L"\",\"repetitionReason\":\"" + EscapeJsonString(item.response.metrics.repetitionReason) +
            L"\",\"errorCategory\":\"" + EscapeJsonString(item.response.metrics.errorCategory) +
            L"\"}";

        // OWN-118: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatPaddleDocGroupMetrics(
            group.id.c_str(), group.regionIndices.size(), group.contentOwnerIndex,
            item.attempts, item.skipped ? 1 : 0,
            item.imageStats.width, item.imageStats.height,
            item.imageStats.polygonApplied ? 1 : 0,
            item.imageStats.formulaMarginApplied ? 1 : 0,
            item.response.metrics.httpStatus,
            static_cast<unsigned long>(item.response.metrics.elapsedMs),
            item.response.metrics.timeoutMs,
            item.response.metrics.imageBytes,
            item.response.metrics.imageMime.c_str(),
            item.response.metrics.pngBytes,
            static_cast<unsigned long>(item.response.metrics.requestBuildUs),
            item.response.metrics.requestBytes,
            item.response.metrics.responseBytes,
            item.response.metrics.promptTokens,
            item.response.metrics.completionTokens,
            item.response.metrics.totalTokens,
            item.response.metrics.finishReason.c_str(),
            item.response.metrics.repetitionReason.c_str(),
            item.response.metrics.errorCategory.c_str()).c_str());
        if (item.crop) {
            DeleteObject(item.crop);
            item.crop = nullptr;
        }
    }

    // OWN-118: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPaddleDocPipelineSummary(
        regions.size(), plan.groups.size(), recognizedGroups,
        groupingStats.secondaryRegions, skippedGroups, failedGroups,
        groupingStats.maxGroupMembers, groupingStats.maxComposedAspectRatio,
        groupingStats.aspectSplitGroups, groupingStats.limitSplitGroups,
        totalImageBytes, totalPngBytes, groupingOptions.mode.c_str(),
        groupingStats.singletonFallback ? 1 : 0).c_str());

    groupsDiagnostics += L"]";

    const ULONGLONG markdownStart = GetTickCount64();
    result.text = AssembleMarkdown(
        markdownRegions, texts, hBitmap, &result.embeddedAssets);
    std::vector<std::wstring> trimmedTexts(texts.size());
    for (size_t index = 0; index < texts.size(); ++index) {
        trimmedTexts[index] = TrimString(texts[index]);
    }
    PopulateLayoutOverlayFromRegions(
        result, regions, &trimmedTexts, 0, L"paddle_doc_layout",
        &plan.groupIdByRegion);
    const DWORD markdownMs = static_cast<DWORD>(
        GetTickCount64() - markdownStart);
    const DWORD pipelineMs = static_cast<DWORD>(
        GetTickCount64() - pipelineStart);

    const auto& launchDiagnostics =
        server->GetLaunchDiagnostics();
    // OWN-124: pure compact JSON int/ull fields (WideStringUtils).
    result.diagnosticsJson =
        L"{\"pipeline\":\"paddle_doc_grouped\"," +
        WideJsonFieldUllCompact(L"blockCount", (unsigned long long)regions.size()) +
        L"," + WideJsonFieldUllCompact(L"groupCount", (unsigned long long)plan.groups.size()) +
        L"," + WideJsonFieldIntCompact(L"recognizedGroups", recognizedGroups) +
        L"," + WideJsonFieldIntCompact(L"secondaryRegions", groupingStats.secondaryRegions) +
        L"," + WideJsonFieldIntCompact(L"skippedGroups", skippedGroups) +
        L"," + WideJsonFieldIntCompact(L"failedGroups", failedGroups) +
        L"," + WideJsonFieldIntCompact(L"maxGroupMembers", groupingStats.maxGroupMembers) +
        L"," + WideJsonFieldIntCompact(L"aspectSplitGroups", groupingStats.aspectSplitGroups) +
        L"," + WideJsonFieldIntCompact(L"limitSplitGroups", groupingStats.limitSplitGroups) +
        L"," + WideJsonFieldIntCompact(L"maxTokens",
            settings.paddleVlMaxTokens == 8192 ? 8192 : 4096) +
        L"," + WideJsonFieldUllCompact(L"serverSetupMs", (unsigned long long)serverSetupMs) +
        L"," + WideJsonFieldUllCompact(L"layoutMs", (unsigned long long)layoutMs) +
        L"," + WideJsonFieldUllCompact(L"groupingMs", (unsigned long long)groupingMs) +
        L"," + WideJsonFieldUllCompact(L"cropMs", (unsigned long long)cropMs) +
        L"," + WideJsonFieldUllCompact(L"vlmWallMs", (unsigned long long)vlmWallMs) +
        L"," + WideJsonFieldUllCompact(L"markdownMs", (unsigned long long)markdownMs) +
        L"," + WideJsonFieldUllCompact(L"pipelineMs", (unsigned long long)pipelineMs) +
        L",\"layoutFamily\":\"" +
            EscapeJsonString(LayoutModelFamilyName(layoutDiagnostics.family)) + L"\"" +
        L",\"layoutModelPath\":\"" +
            EscapeJsonString(layoutDiagnostics.modelPath) + L"\"" +
        L"," + WideJsonFieldUllCompact(L"layoutModelBytes",
            (unsigned long long)layoutDiagnostics.modelBytes) +
        L",\"layoutModelSha256\":\"" +
            EscapeJsonString(layoutDiagnostics.modelSha256) + L"\"" +
        L",\"layoutModelSha256Error\":\"" +
            EscapeJsonString(layoutDiagnostics.modelSha256Error) + L"\"" +
        L",\"layoutTiledTriggered\":" +
            (WideJsonBoolLiteral(layoutDiagnostics.tiledTriggered)) +
        L",\"layoutUsedTiled\":" +
            (WideJsonBoolLiteral(layoutDiagnostics.usedTiled)) +
        L"," + WideJsonFieldIntCompact(L"layoutReturned", layoutDiagnostics.returnedRegions) +
        L"," + WideJsonFieldIntCompact(L"layoutFullRaw", layoutDiagnostics.full.raw) +
        L"," + WideJsonFieldIntCompact(L"layoutFullScorePassed",
            layoutDiagnostics.full.scorePassed) +
        L"," + WideJsonFieldIntCompact(L"layoutFullNmsKept", layoutDiagnostics.full.nmsKept) +
        L"," + WideJsonFieldIntCompact(L"layoutFullImageAreaKept",
            layoutDiagnostics.full.imageAreaKept) +
        L"," + WideJsonFieldIntCompact(L"layoutFullClassModeKept",
            layoutDiagnostics.full.classModeKept) +
        L"," + WideJsonFieldIntCompact(L"layoutFullPolygonFallbacks",
            layoutDiagnostics.full.polygonFallbacks) +
        L"," + WideJsonFieldIntCompact(L"layoutFullOverlapKept",
            layoutDiagnostics.full.overlapKept) +
        L"," + WideJsonFieldIntCompact(L"layoutFullFinal",
            layoutDiagnostics.full.finalCount) +
        L"," + WideJsonFieldIntCompact(L"layoutTileRaw", layoutDiagnostics.tiled.raw) +
        L"," + WideJsonFieldIntCompact(L"layoutTileScorePassed",
            layoutDiagnostics.tiled.scorePassed) +
        L"," + WideJsonFieldIntCompact(L"layoutTileNmsKept", layoutDiagnostics.tiled.nmsKept) +
        L"," + WideJsonFieldIntCompact(L"layoutTileClassModeKept",
            layoutDiagnostics.tiled.classModeKept) +
        L"," + WideJsonFieldIntCompact(L"layoutTilePolygonFallbacks",
            layoutDiagnostics.tiled.polygonFallbacks) +
        L"," + WideJsonFieldIntCompact(L"layoutTileOverlapKept",
            layoutDiagnostics.tiled.overlapKept) +
        L"," + WideJsonFieldIntCompact(L"layoutTileFinal",
            layoutDiagnostics.tiled.finalCount) +
        L"," + WideJsonFieldUllCompact(L"totalImageBytes", (unsigned long long)totalImageBytes) +
        L"," + WideJsonFieldUllCompact(L"totalPngBytes", (unsigned long long)totalPngBytes) +
        L",\"serverModelsReachable\":" + (WideJsonBoolLiteral(serverInfo.modelsReachable)) +
        L",\"serverPropsReachable\":" + (WideJsonBoolLiteral(serverInfo.propsReachable)) +
        L",\"serverModelListed\":" + (WideJsonBoolLiteral(serverInfo.modelListed)) +
        L",\"serverMultimodal\":" + (WideJsonBoolLiteral(serverInfo.multimodal)) +
        L"," + WideJsonFieldIntCompact(L"serverTotalSlots", serverInfo.totalSlots) +
        L"," + WideJsonFieldIntCompact(L"serverSlotContext", serverInfo.slotContext) +
        L",\"serverVersion\":\"" + EscapeJsonString(launchDiagnostics.serverVersion) +
        L"\",\"serverBackend\":\"" + EscapeJsonString(launchDiagnostics.backend) +
        L"\",\"modelSha256\":\"" + EscapeJsonString(launchDiagnostics.modelSha256) +
        L"\",\"mmprojSha256\":\"" + EscapeJsonString(launchDiagnostics.mmprojSha256) +
        L"\",\"modelSha256CacheHit\":" +
            (WideJsonBoolLiteral(launchDiagnostics.modelSha256CacheHit)) +
        L",\"mmprojSha256CacheHit\":" +
            (WideJsonBoolLiteral(launchDiagnostics.mmprojSha256CacheHit)) +
        L"," + WideJsonFieldUllCompact(L"sha256Ms",
            (unsigned long long)launchDiagnostics.sha256Ms) +
        L"," + WideJsonFieldIntCompact(L"mmprojMinPixels", launchDiagnostics.imageMinPixels) +
        L"," + WideJsonFieldIntCompact(L"mmprojMaxPixels", launchDiagnostics.imageMaxPixels) +
        L",\"chatTemplatePath\":\"" + EscapeJsonString(launchDiagnostics.chatTemplatePath) +
        L"\"" +
        L",\"groups\":" + groupsDiagnostics + L"}";
    result.success = PaddleDocRecognitionPageSucceeded(failedGroups);
    if (!result.success) {
        result.error = PaddleDocRecognitionFailureError(failedGroups);
        if (!firstFailure.empty()) {
            result.error += L" First error: " + firstFailure;
        }
    }
    return result;
}

OcrOutput OcrEnginePaddleDoc::DoRecognize(HBITMAP hBitmap) {
    return DoRecognizeOfficial(hBitmap);
}

std::wstring OcrEnginePaddleDoc::SplitParagraphs(const std::wstring& text) {
    if (text.empty()) return text;

    auto isCJKNumeral = [](const std::wstring& s, size_t pos) -> bool {
        static const wchar_t cnNums[] = L"\u4e00\u4e8c\u4e09\u56db\u4e94\u516d\u4e03\u516b\u4e5d\u5341";
        if (pos >= s.size()) return false;
        for (wchar_t n : cnNums) {
            if (s[pos] == n) {
                size_t next = pos + 1;
                if (next < s.size() && (s[next] == L'\u3001' || s[next] == L'.' || s[next] == L'\uff0e'))
                    return true;
            }
        }
        return false;
    };

    auto isCircledDigit = [](wchar_t c) -> bool {
        return (c >= L'\u2460' && c <= L'\u2473') ||
               (c >= L'\u3251' && c <= L'\u325f') ||
               (c >= L'\u32b1' && c <= L'\u32bf');
    };

    auto isCNBullet = [](wchar_t c) -> bool {
        return c == L'\u25c6' || c == L'\u25a0' || c == L'\u25ba' ||
               c == L'\u25b6' || c == L'\u25cf' || c == L'\u25cb';
    };

    std::wstring result;
    size_t pos = 0;

    while (pos < text.length()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) {
            result += text.substr(pos);
            break;
        }

        result += text.substr(pos, nl - pos + 1);

        if (nl + 1 < text.length()) {
            size_t nextNonSpace = nl + 1;
            while (nextNonSpace < text.length() &&
                   (text[nextNonSpace] == L' ' || text[nextNonSpace] == L'\t')) {
                nextNonSpace++;
            }

            if (nextNonSpace < text.length()) {
                wchar_t nextChar = text[nextNonSpace];

                bool isNumbered = (nextChar >= L'0' && nextChar <= L'9');
                if (isNumbered) {
                    size_t afterNum = nextNonSpace;
                    while (afterNum < text.length() &&
                           text[afterNum] >= L'0' && text[afterNum] <= L'9') {
                        afterNum++;
                    }
                    if (afterNum < text.length() &&
                        (text[afterNum] == L'.' || text[afterNum] == L')' ||
                         text[afterNum] == L'\uff0e' || text[afterNum] == L'\uff09')) {
                        result += L"\n";
                    }
                } else if (nextChar == L'(' || nextChar == L'\uff08') {
                    size_t afterParen = nextNonSpace + 1;
                    while (afterParen < text.length() &&
                           text[afterParen] >= L'0' && text[afterParen] <= L'9') {
                        afterParen++;
                    }
                    if (afterParen < text.length() &&
                        (text[afterParen] == L')' || text[afterParen] == L'\uff09')) {
                        result += L"\n";
                    }
                } else if (isCircledDigit(nextChar)) {
                    result += L"\n";
                } else if (isCJKNumeral(text, nextNonSpace)) {
                    result += L"\n";
                } else if (nextChar == L'-' || nextChar == L'*' || nextChar == L'\u2022') {
                    size_t afterBullet = nextNonSpace + 1;
                    if (afterBullet < text.length() && text[afterBullet] == L' ') {
                        result += L"\n";
                    }
                } else if (isCNBullet(nextChar)) {
                    size_t afterBullet = nextNonSpace + 1;
                    if (afterBullet < text.length() && text[afterBullet] == L' ') {
                        result += L"\n";
                    }
                }
            }
        }

        pos = nl + 1;
    }

    return result;
}

DWORD WINAPI OcrEnginePaddleDoc::WorkerThread(LPVOID param) {
    auto* p = static_cast<PaddleDocParams*>(param);
    OcrOutput result;
    ULONGLONG startTick = GetTickCount64();
    try {
        OcrEnginePaddleDoc engine(p->server);
        result = engine.DoRecognize(p->hBitmap);
    } catch (std::exception const& ex) {
        // OWN-117: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatPaddleDocException(ex.what()).c_str());
        result.error = L"Exception during document parsing";
    } catch (...) {
        OutputDebugStringA("[PaddleDoc] Unknown exception\n");
        result.error = L"Unexpected error during document parsing.";
    }
    if (!result.success && result.error.empty()) {
        result.error = L"Document parsing failed";
    }
    result.elapsedMs = (DWORD)(GetTickCount64() - startTick);
    DeleteObject(p->hBitmap);
    InvokeOcrCallbackSafely(p->callback, result);
    delete p;
    return 0;
}

void OcrEnginePaddleDoc::Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) {
    auto* params = new PaddleDocParams{ hBitmap, std::move(callback), m_server };
    HANDLE h = CreateThread(nullptr, 0, WorkerThread, params, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        OcrOutput result;
        result.error = L"Failed to start the document OCR worker thread.";
        DeleteObject(params->hBitmap);
        InvokeOcrCallbackSafely(params->callback, result);
        delete params;
    }
}
