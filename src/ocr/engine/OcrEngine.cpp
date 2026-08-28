#include "OcrEngine.h"
#include "OcrEngine_Local.h"
#include "OcrEngine_PaddleOCR_Cloud.h"
#include "OcrEngine_PaddleOCR_Local.h"
#include "OcrEngine_PaddleOCR_Doc.h"
#include "OcrEngine_PPOCRv6_ONNX.h"
#include "core/PaddleVlServerService.h"
#include "core/OcrModelRegistry.h"
#include "PPOcrV6RecBatchPlan.h"
#include "Settings.h"
#include "core/WideStringUtils.h"
#include <algorithm>
#include <memory>

namespace {

PaddleVlServerService* g_paddleVlServer = nullptr;

std::wstring NormalizePPOcrV6LimitType(std::wstring limitType) {
    limitType = WideToLower(std::move(limitType));
    return limitType == L"max" ? L"max" : L"min";
}

PPOcrV6Config BuildPPOcrV6FactoryConfig(const OcrSettings& settings) {
    const OcrModelRegistryPPOcrV6Paths paths = OcrModelRegistryBuildPlan(
        settings, OcrModelRegistryProcessDir()).ppocrv6;
    PPOcrV6Config cfg;
    cfg.modelDir = paths.modelDir;
    cfg.variant = paths.variant;
    cfg.provider = L"cpu";
    cfg.cpuThreads = (std::max)(1, (std::min)(settings.ppocrv6CpuThreads, 16));
    cfg.recBatchSize = PPOcrV6RecBatch::ResolveRecBatchSize(settings.ppocrv6RecBatchSize);
    cfg.detLimitSideLen = (std::max)(64, (std::min)(settings.ppocrv6DetLimitSideLen, 4096));
    cfg.detLimitType = NormalizePPOcrV6LimitType(settings.ppocrv6DetLimitType);
    cfg.detMaxSideLimit = (std::max)(1024, (std::min)(settings.ppocrv6DetMaxSideLimit, 8000));
    cfg.detThresh = (std::max)(0.0f, (std::min)(settings.ppocrv6DetThreshPct / 100.0f, 1.0f));
    cfg.detBoxThresh = (std::max)(0.0f, (std::min)(settings.ppocrv6DetBoxThreshPct / 100.0f, 1.0f));
    cfg.detUnclipRatio = (std::max)(1.0f, (std::min)(settings.ppocrv6DetUnclipRatioPct / 100.0f, 3.0f));
    cfg.recScoreThresh = (std::max)(0.0f, (std::min)(settings.ppocrv6RecScoreThreshPct / 100.0f, 1.0f));

    cfg.detModelPath = paths.detModelPath;
    cfg.recModelPath = paths.recModelPath;
    cfg.dictPath = paths.dictPath;
    return cfg;
}

std::shared_ptr<IOcrEngine> CreateForMode(const OcrSettings& settings, const std::wstring& mode) {
    if (mode == L"ppocrv6_onnx") {
        return std::make_shared<OcrEnginePPOcrV6Onnx>(
            BuildPPOcrV6Config(settings));
    }
    if (mode == L"paddle_cloud") {
        return std::make_shared<OcrEnginePaddleCloud>();
    }
    if (mode == L"paddle_local") {
        if (settings.enableDocParsing) {
            return std::make_shared<OcrEnginePaddleDoc>(g_paddleVlServer);
        }
        return std::make_shared<OcrEnginePaddleLocal>(g_paddleVlServer);
    }
    return std::make_shared<OcrEngineLocal>();
}

} // namespace

void OcrEngineFactory::ConfigurePaddleVlServer(PaddleVlServerService* service) {
    g_paddleVlServer = service;
}

PPOcrV6Config BuildPPOcrV6Config(const OcrSettings& settings) {
    return BuildPPOcrV6FactoryConfig(settings);
}

std::shared_ptr<IOcrEngine> OcrEngineFactory::Create(const std::wstring& mode) {
    OcrSettings settings = LoadOcrSettings();
    return CreateForMode(settings, mode);
}

std::shared_ptr<IOcrEngine> OcrEngineFactory::CreateForRoute(
    const OcrSettings& settings,
    const std::wstring& route)
{
    std::wstring normalized = NormalizeOcrRoute(route);
    if (normalized == L"current") {
        return CreateForMode(settings, settings.mode);
    }
    if (normalized == L"paddle_local_doc") {
        return std::make_shared<OcrEnginePaddleDoc>(g_paddleVlServer);
    }
    if (normalized == L"paddle_local") {
        return std::make_shared<OcrEnginePaddleLocal>(g_paddleVlServer);
    }
    return CreateForMode(settings, normalized);
}

// H5 硬约束：把 main.cpp 原有的 "current -> local" fallback 逻辑封装在这里，
// 让进度浮层能拿到 fallback 后的真实引擎显示名。
OcrEngineSelection SelectOcrEngineForRoute(const OcrSettings& settings, const std::wstring& route) {
    std::wstring normalized = NormalizeOcrRoute(route);
    bool explicitRoute = (normalized != L"current");

    std::shared_ptr<IOcrEngine> engine;
    std::wstring label;

    if (normalized == L"current") {
        engine = CreateForMode(settings, settings.mode);
        // label 反映 CreateForMode 的实际选择（paddle_local + enableDocParsing → paddle_local_doc）
        if (settings.mode == L"paddle_local" && settings.enableDocParsing) {
            label = L"paddle_local_doc";
        } else {
            label = settings.mode;
        }
    } else if (normalized == L"paddle_local_doc") {
        engine = std::make_shared<OcrEnginePaddleDoc>(g_paddleVlServer);
        label = L"paddle_local_doc";
    } else if (normalized == L"paddle_local") {
        engine = std::make_shared<OcrEnginePaddleLocal>(g_paddleVlServer);
        label = L"paddle_local";
    } else {
        engine = CreateForMode(settings, normalized);
        label = normalized;
    }

    // 仅 current 路由允许 fallback 到 local；显式 route 不 fallback。
    if (!explicitRoute && engine && !engine->IsAvailable() && settings.mode != L"ppocrv6_onnx") {
        engine = CreateForMode(settings, L"local");
        label = L"local";
    }

    return OcrEngineSelection{ engine, label };
}

bool IsFastOcrEngine(const std::wstring& displayLabel) {
    // 这些引擎速度足够快，OCR 期间不需要进度反馈
    return displayLabel == L"ppocrv6_onnx" || displayLabel == L"local";
}
