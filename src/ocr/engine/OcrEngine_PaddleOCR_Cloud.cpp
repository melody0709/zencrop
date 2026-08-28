#include "OcrEngine_PaddleOCR_Cloud.h"

#include "Settings.h"
#include "JsonUtils.h"
#include "core/NarrowStringUtils.h"

#include <exception>

namespace {

bool IsAsyncJobsUrl(const std::wstring& url) {
    std::wstring error;
    return IsPaddleCloudImageOcrJobsEndpoint(url, error);
}

DWORD WINAPI CloudOcrWorkerThread(LPVOID param) {
    auto* pParams = static_cast<OcrParams*>(param);
    OcrOutput result;
    ULONGLONG startTick = GetTickCount64();

    OutputDebugStringA("[OCR] Cloud worker thread started\n");

    try {
        OcrSettings settings = LoadOcrSettings();
        settings.paddleApiUrl = NormalizePaddleOcrJobsUrl(settings.paddleApiUrl);
        settings.paddleToken = TrimString(settings.paddleToken);

        if (settings.paddleApiUrl.empty() || settings.paddleToken.empty()) {
            result.error = L"PaddleOCR API URL or Token not configured. Please check settings.";
            DeleteObject(pParams->hBitmap);
            InvokeOcrCallbackSafely(pParams->callback, result);
            delete pParams;
            return 1;
        }

        constexpr long kCloudJpegQuality = 95;
        auto uploadImage = PaddleCloudRequest::EncodeUploadImage(
            pParams->hBitmap, kCloudJpegQuality);
        if (uploadImage.bytes.empty()) {
            result.error = L"Failed to convert bitmap to JPEG or PNG.";
            DeleteObject(pParams->hBitmap);
            InvokeOcrCallbackSafely(pParams->callback, result);
            delete pParams;
            return 1;
        }

        OutputDebugStringA(NarrowFormatOcrCloudUploadImage(
            uploadImage.contentType.c_str(),
            uploadImage.bytes.size(),
            uploadImage.usedPngFallback ? 1 : 0).c_str());

        if (!IsAsyncJobsUrl(settings.paddleApiUrl)) {
            result.error = L"PaddleOCR Cloud now requires the official async jobs API URL:\n"
                L"https://paddleocr.aistudio-app.com/api/v2/ocr/jobs";
            DeleteObject(pParams->hBitmap);
            InvokeOcrCallbackSafely(pParams->callback, result);
            delete pParams;
            return 1;
        }

        OutputDebugStringA("[OCR] Using PaddleOCR async jobs API\n");
        PaddleCloudImageOcrRequest request;
        request.jobsEndpoint = settings.paddleApiUrl;
        request.bearerToken = settings.paddleToken;
        request.image = std::move(uploadImage);
        request.chartRecognition = settings.paddleCloudUseChartRecognition;
        request.timeoutMs = settings.timeoutMs;
        result = RecognizePaddleCloudImage(request);
        OutputDebugStringA(result.success ? "[OCR] Cloud OCR success!\n" : "[OCR] Cloud OCR failed\n");
    }
    catch (const std::exception& ex) {
        OutputDebugStringA(NarrowFormatOcrException(ex.what()).c_str());
        result.error = L"Exception during cloud OCR";
    }
    catch (...) {
        OutputDebugStringA("[OCR] Unknown exception\n");
        result.error = L"Unknown error during cloud OCR";
    }

    result.elapsedMs = static_cast<DWORD>(GetTickCount64() - startTick);
    DeleteObject(pParams->hBitmap);
    InvokeOcrCallbackSafely(pParams->callback, result);
    delete pParams;
    return 0;
}

} // namespace

void OcrEnginePaddleCloud::Recognize(
    HBITMAP hBitmap,
    std::function<void(OcrOutput)> callback)
{
    auto* params = new OcrParams{ hBitmap, std::move(callback) };
    HANDLE h = CreateThread(nullptr, 0, CloudOcrWorkerThread, params, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        OcrOutput result;
        result.error = L"Failed to start the cloud OCR worker thread.";
        DeleteObject(params->hBitmap);
        InvokeOcrCallbackSafely(params->callback, result);
        delete params;
    }
}

bool OcrEnginePaddleCloud::IsAvailable() {
    OcrSettings settings = LoadOcrSettings();
    return !settings.paddleApiUrl.empty() &&
           !settings.paddleToken.empty() &&
           IsAsyncJobsUrl(settings.paddleApiUrl);
}
