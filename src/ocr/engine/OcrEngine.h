#pragma once
#include "OcrUtils.h"
#include <windows.h>
#include <string>
#include <functional>
#include <memory>

struct OcrSettings;
class PaddleVlServerService;

class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;
    virtual void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) = 0;
    virtual bool IsAvailable() = 0;
    virtual std::wstring Name() = 0;
};

class OcrEngineFactory {
public:
    // Application bootstrap supplies the net-owned capability before creating
    // Paddle engines; individual engine instances receive the typed pointer.
    static void ConfigurePaddleVlServer(PaddleVlServerService* service);
    static std::shared_ptr<IOcrEngine> Create(const std::wstring& mode);
    static std::shared_ptr<IOcrEngine> CreateForRoute(const OcrSettings& settings, const std::wstring& route);
};

// 引擎选择结果（H5 硬约束）：engine 是 fallback 后的真实引擎，displayLabel 是用于 UI 显示的引擎名。
// 把 main.cpp 原有的 "current -> local" fallback 逻辑封装在这里，让进度浮层能显示真实引擎。
struct OcrEngineSelection {
    std::shared_ptr<IOcrEngine> engine;
    std::wstring displayLabel;  // fallback 后的真实引擎显示名（如 "local" / "paddle_local" / "ppocrv6_onnx"）
};

// 按route 选择引擎，必要时 fallback 到 local。
// - route == "current" 时用 settings.mode；若该引擎不可用且非 ppocrv6_onnx，fallback 到 local。
// - 显式 route（paddle_local / paddle_local_doc 等）不 fallback。
// 实现合并进 OcrEngine.cpp（H6 方案 A，不新增 .cpp）。
OcrEngineSelection SelectOcrEngineForRoute(const OcrSettings& settings, const std::wstring& route);

// 判断该引擎是否速度足够快，不需要进度反馈（浮层 / Dashboard ActiveWorkStrip 都跳过）。
// 目前包含：ppocrv6_onnx（PP-OCRv6 Local）、local（Local / Windows OCR）。
// 调用方在 Show 进度反馈前用此函数判断；跳过时 progressId 保持 0，
// 回调到达时 Close(0) / HideExternalOcrProgress(0) 会因 id 不匹配被忽略，安全。
bool IsFastOcrEngine(const std::wstring& displayLabel);
