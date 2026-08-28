#pragma once

#include "OcrUtils.h"

#include <string>
#include <vector>

// Stage3 3-D-1: document owns the provider jobs workflow shared by cloud image
// OCR. The engine owns settings selection, HBITMAP lifetime, thread and callback.
namespace PaddleCloudRequest {

struct UploadImage {
    std::vector<unsigned char> bytes;
    std::string filename;
    std::string contentType;
    bool usedPngFallback = false;
};

// Paddle's async API accepts JPEG and PNG. Prefer a high-quality JPEG to keep
// document uploads substantially smaller, with PNG as an encoding fallback.
UploadImage EncodeUploadImage(HBITMAP hBitmap, long jpegQuality = 95);

std::string BuildMultipartBody(
    const UploadImage& image,
    const std::string& modelName,
    const std::string& optionalPayload,
    const std::string& boundary);

} // namespace PaddleCloudRequest

struct PaddleCloudImageOcrRequest {
    std::wstring jobsEndpoint;
    std::wstring bearerToken;
    PaddleCloudRequest::UploadImage image;
    bool chartRecognition = false;
    int timeoutMs = 120000;
};

// Executes the remote jobs API only: submit, poll, bounded downloads, provider
// parsing and provider-image materialization. It has no engine callback/thread
// dependency and cannot choose settings or engine fallback policy.
OcrOutput RecognizePaddleCloudImage(const PaddleCloudImageOcrRequest& request);

bool IsPaddleCloudImageOcrJobsEndpoint(
    const std::wstring& url,
    std::wstring& error);
