#include "OcrEngine_PaddleOCR_Local.h"
#include "core/PaddleVlServerService.h"
#include "PaddleVlLlamaClient.h"
#include "Settings.h"
#include "OcrUtils.h"
#include "OcrPaddleVlJson.h"
#include "core/NarrowStringUtils.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"

namespace {
struct LlamaRequestScope {
    explicit LlamaRequestScope(PaddleVlServerService* service) : service(service) {
        if (service) service->BeginRequest();
    }
    ~LlamaRequestScope() { if (service) service->EndRequest(); }
    PaddleVlServerService* service = nullptr;
};

struct PaddleLocalParams {
    HBITMAP hBitmap = nullptr;
    std::function<void(OcrOutput)> callback;
    PaddleVlServerService* server = nullptr;
};
}

OcrEnginePaddleLocal::OcrEnginePaddleLocal(PaddleVlServerService* server)
    : m_server(server) {}
OcrEnginePaddleLocal::~OcrEnginePaddleLocal() {}

bool OcrEnginePaddleLocal::IsAvailable() {
    auto* server = m_server;
    std::wstring serverExe, modelPath, mmprojPath;
    return server && server->FindServerExe(serverExe) &&
           server->FindModelFiles(modelPath, mmprojPath);
}

OcrOutput OcrEnginePaddleLocal::DoRecognize(HBITMAP hBitmap) {
    OcrOutput result;
    auto* server = m_server;
    if (!server) {
        result.error = L"PaddleVL server service is unavailable.";
        return result;
    }
    LlamaRequestScope llamaScope(server);

    if (!server->EnsureServerStarted()) {
        result.error = L"Failed to start llama-server. Check model directory in Settings.";
        return result;
    }

    int port = server->GetPort();
    const std::string modelName = server->GetModelName();
    // OWN-121: pure localhost base URL (WideStringUtils).
    const std::wstring baseUrl = WideFormatLocalhostBase(port);
    const PaddleVlLlamaServerInfo serverInfo = ProbePaddleVlLlamaServer(
        baseUrl, modelName, 5000);

    std::wstring capabilityError;
    if (!ValidatePaddleVlLlamaServerCapability(serverInfo, capabilityError)) {
        result.error = capabilityError;
        return result;
    }

    OcrSettings settings = LoadOcrSettings();
    std::wstring promptW = settings.paddleLocalPrompt;
    if (promptW.empty()) promptW = L"OCR:";

    // OWN-121: pure localhost chat URL (WideStringUtils).
    std::wstring url = WideFormatLocalhostChatCompletions(port);
    auto vlmResult = SendPaddleVlLlamaRequest(
        hBitmap,
        url,
        modelName,
        promptW,
        promptW.find(L"Table") != std::wstring::npos,
        120000,
        settings.paddleVlMaxTokens == 8192 ? 8192 : 4096);

    // OWN-118: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPaddleLocalMetrics(
        vlmResult.metrics.httpStatus,
        static_cast<unsigned long>(vlmResult.metrics.elapsedMs),
        vlmResult.metrics.timeoutMs,
        vlmResult.metrics.pngBytes,
        vlmResult.metrics.requestBytes,
        vlmResult.metrics.responseBytes,
        vlmResult.metrics.promptTokens,
        vlmResult.metrics.completionTokens,
        vlmResult.metrics.totalTokens,
        vlmResult.metrics.finishReason.c_str(),
        vlmResult.metrics.repetitionReason.c_str(),
        vlmResult.metrics.errorCategory.c_str()).c_str());
    // OWN-118: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatPaddleServerProbe(
        "PaddleLocal",
        serverInfo.modelsReachable ? 1 : 0, serverInfo.modelsHttpStatus,
        serverInfo.propsReachable ? 1 : 0, serverInfo.propsHttpStatus,
        serverInfo.modelListed ? 1 : 0, serverInfo.multimodal ? 1 : 0,
        serverInfo.totalSlots, serverInfo.slotContext,
        serverInfo.warning.c_str()).c_str());

    if (!vlmResult.success) {
        result.error = vlmResult.error;
        return result;
    }

    result.text = vlmResult.content;
    result.blocks = ParsePaddleVlLayoutBlocks(result.text);
    if (!result.blocks.empty()) {
        result.rawOcrJson = result.text;
        result.debugOutputImagesJson = ExtractPaddleVlOutputImagesJson(result.rawOcrJson);
        result.bboxes.reserve(result.blocks.size());
        result.bboxClasses.reserve(result.blocks.size());
        for (const auto& block : result.blocks) {
            result.bboxes.push_back(block.bbox);
            result.bboxClasses.push_back(block.label);
        }
    }
    if (promptW.find(L"Table") != std::wstring::npos) {
        result.text = ConvertOTSLToHTML(result.text);
    }
    result.success = true;

    OutputDebugStringA("[PaddleLocal] OCR success!\n");
    return result;
}

DWORD WINAPI OcrEnginePaddleLocal::WorkerThread(LPVOID param) {
    auto* p = static_cast<PaddleLocalParams*>(param);
    OcrOutput result;
    ULONGLONG startTick = GetTickCount64();

    OutputDebugStringA("[PaddleLocal] Worker thread started\n");

    try {
        OcrEnginePaddleLocal engine(p->server);
        result = engine.DoRecognize(p->hBitmap);
    }
    catch (std::exception const& ex) {
        // OWN-117: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatPaddleLocalException(ex.what()).c_str());
        result.error = L"Exception during local PaddleOCR";
    }
    catch (...) {
        OutputDebugStringA("[PaddleLocal] Unknown exception\n");
        result.error = L"Unknown error during local PaddleOCR";
    }

    result.elapsedMs = (DWORD)(GetTickCount64() - startTick);
    DeleteObject(p->hBitmap);
    InvokeOcrCallbackSafely(p->callback, result);
    delete p;
    return 0;
}

void OcrEnginePaddleLocal::Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) {
    auto* params = new PaddleLocalParams{ hBitmap, std::move(callback), m_server };
    HANDLE h = CreateThread(nullptr, 0, WorkerThread, params, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        OcrOutput result;
        result.error = L"Failed to start the local PaddleOCR worker thread.";
        DeleteObject(params->hBitmap);
        InvokeOcrCallbackSafely(params->callback, result);
        delete params;
    }
}
