#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

enum class PaddleVlImageEncoding {
    Png,
    LegacyJpeg95,
};

struct PaddleVlLlamaRequest {
    std::string jsonBody;
    size_t imageBytes = 0;
    size_t pngBytes = 0;
    std::wstring imageMime;
    std::wstring error;
};

struct PaddleVlRepetitionResult {
    std::wstring content;
    std::wstring reason = L"none";
    bool changed = false;
};

struct PaddleVlLlamaMetrics {
    size_t imageBytes = 0;
    size_t pngBytes = 0;
    std::wstring imageMime;
    DWORD requestBuildUs = 0;
    size_t requestBytes = 0;
    size_t responseBytes = 0;
    DWORD elapsedMs = 0;
    int httpStatus = 0;
    int timeoutMs = 0;
    std::wstring errorCategory = L"none";
    std::wstring finishReason;
    int promptTokens = -1;
    int completionTokens = -1;
    int totalTokens = -1;
    std::wstring repetitionReason = L"none";
};

struct PaddleVlLlamaResult {
    bool success = false;
    std::wstring content;
    std::wstring error;
    PaddleVlLlamaMetrics metrics;
};

struct PaddleVlLlamaServerInfo {
    bool modelsReachable = false;
    bool propsReachable = false;
    bool modelListed = false;
    bool multimodal = false;
    int modelsHttpStatus = 0;
    int propsHttpStatus = 0;
    int totalSlots = -1;
    int slotContext = -1;
    size_t modelsResponseBytes = 0;
    size_t propsResponseBytes = 0;
    std::wstring warning;
};

// Pure wire-format builder used by tests and both local PaddleOCR-VL paths.
PaddleVlLlamaRequest BuildPaddleVlLlamaRequestFromPng(
    const std::vector<unsigned char>& pngBytes,
    const std::string& modelName,
    const std::wstring& prompt,
    int maxTokens = 4096);

PaddleVlLlamaRequest BuildPaddleVlLlamaRequestFromImageBytes(
    const std::vector<unsigned char>& imageBytes,
    const std::string& mimeType,
    const std::string& modelName,
    const std::wstring& prompt,
    int maxTokens = 4096);

PaddleVlLlamaRequest BuildPaddleVlLlamaRequest(
    HBITMAP bitmap,
    const std::string& modelName,
    const std::wstring& prompt,
    int maxTokens = 4096,
    PaddleVlImageEncoding encoding = PaddleVlImageEncoding::Png);

PaddleVlRepetitionResult ApplyPaddleVlRepetitionGuard(
    const std::wstring& content,
    bool tableContent);

// A single retry is reserved for failures that can plausibly recover without
// changing the request. Permanent request/schema/client errors must fail fast.
bool ShouldRetryPaddleVlLlamaFailure(const PaddleVlLlamaResult& result);

PaddleVlLlamaResult SendPaddleVlLlamaRequest(
    HBITMAP bitmap,
    const std::wstring& endpoint,
    const std::string& modelName,
    const std::wstring& prompt,
    bool tableContent,
    int timeoutMs = 120000,
    int maxTokens = 4096,
    PaddleVlImageEncoding encoding = PaddleVlImageEncoding::Png);

// Probes the exact model and multimodal capabilities required before an image
// request is sent. Callers must validate the returned snapshot and fail fast
// when either endpoint, the requested model, or multimodal support is missing.
PaddleVlLlamaServerInfo ProbePaddleVlLlamaServer(
    const std::wstring& baseUrl,
    const std::string& requestedModel,
    int timeoutMs = 5000);

bool ValidatePaddleVlLlamaServerCapability(
    const PaddleVlLlamaServerInfo& info,
    std::wstring& error);
