#include "PaddleVlLlamaClient.h"

#include "Base64.h"
#include "BitmapUtils.h"
#include "JsonUtils.h"
#include "HttpTransport.h"
#include "OcrUtils.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <cwctype>
#include <initializer_list>
#include <map>
#include <utility>
#include <vector>

namespace {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int length = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

std::string EscapeUtf8Json(const std::wstring& value) {
    return WideToUtf8(EscapeJsonString(value));
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int length = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length);
    return result;
}

// OWN-78: pure strict non-negative int parse (WideStringUtils).
int ReadNonNegativeJsonInt(
    const std::wstring& json,
    std::initializer_list<const wchar_t*> keys)
{
    for (const wchar_t* key : keys) {
        const std::wstring raw = ExtractJsonField(json, key);
        int value = 0;
        if (WideTryParseJsonIntToken(raw, value) && value >= 0) {
            return value;
        }
    }
    return -1;
}

// OWN-78: thin wrapper over pure WideTrim.
std::wstring TrimRepeatWhitespace(const std::wstring& value) {
    return WideTrim(value);
}

bool IsSingleLine(const std::wstring& value) {
    return value.find_first_of(L"\r\n") == std::wstring::npos;
}

std::vector<std::wstring> NonEmptyLines(const std::wstring& value) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find_first_of(L"\r\n", start);
        std::wstring line = TrimRepeatWhitespace(value.substr(
            start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!line.empty()) lines.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        if (value[end] == L'\r' && end + 1 < value.size() && value[end + 1] == L'\n') {
            ++end;
        }
        start = end + 1;
    }
    return lines;
}

PaddleVlRepetitionResult DetectSuffixRepeat(const std::wstring& content) {
    PaddleVlRepetitionResult result{content, L"none", false};
    std::wstring text = TrimRepeatWhitespace(content);
    if (!IsSingleLine(text) || text.size() <= 100) return result;

    // PaddleX selects the longest repeated suffix unit. A candidate must be
    // at least eight characters, repeat consecutively at least five times,
    // and occupy strictly more than half of the stripped output.
    for (size_t unitLength = text.size() / 5; unitLength >= 8; --unitLength) {
        const std::wstring unit = text.substr(text.size() - unitLength);
        size_t repetitions = 1;
        size_t suffixLength = unitLength;
        while (suffixLength + unitLength <= text.size()) {
            size_t candidateStart = text.size() - suffixLength - unitLength;
            if (text.compare(candidateStart, unitLength, unit) != 0) break;
            ++repetitions;
            suffixLength += unitLength;
        }
        if (repetitions >= 5 && suffixLength * 2 > text.size()) {
            result.content = TrimRepeatWhitespace(text.substr(0, text.size() - suffixLength));
            result.reason = L"suffix_repeat";
            result.changed = true;
            return result;
        }
        if (unitLength == 8) break;
    }
    return result;
}

PaddleVlRepetitionResult DetectFullRepeat(const std::wstring& content) {
    PaddleVlRepetitionResult result{content, L"none", false};
    std::wstring text = TrimRepeatWhitespace(content);
    if (!IsSingleLine(text) || text.size() <= 10) return result;

    for (size_t unitLength = 1; unitLength * 10 <= text.size(); ++unitLength) {
        if (text.size() % unitLength != 0) continue;
        size_t repetitions = text.size() / unitLength;
        if (repetitions < 10) continue;
        const std::wstring unit = text.substr(0, unitLength);
        bool matches = true;
        for (size_t offset = unitLength; offset < text.size(); offset += unitLength) {
            if (text.compare(offset, unitLength, unit) != 0) {
                matches = false;
                break;
            }
        }
        if (matches) {
            result.content = unit;
            result.reason = L"full_repeat";
            result.changed = true;
            return result;
        }
    }
    return result;
}

PaddleVlRepetitionResult DetectLineRepeat(const std::wstring& content) {
    PaddleVlRepetitionResult result{content, L"none", false};
    std::vector<std::wstring> lines = NonEmptyLines(content);
    if (lines.size() < 10) return result;

    std::map<std::wstring, size_t> counts;
    for (const auto& line : lines) ++counts[line];
    for (const auto& line : lines) {
        const size_t count = counts[line];
        if (count >= 10 && count * 10 >= lines.size() * 8) {
            result.content = line;
            result.reason = L"line_repeat";
            result.changed = true;
            return result;
        }
    }
    return result;
}

} // namespace

PaddleVlLlamaRequest BuildPaddleVlLlamaRequestFromPng(
    const std::vector<unsigned char>& pngBytes,
    const std::string& modelName,
    const std::wstring& prompt,
    int maxTokens)
{
    return BuildPaddleVlLlamaRequestFromImageBytes(
        pngBytes, "image/png", modelName, prompt, maxTokens);
}

PaddleVlLlamaRequest BuildPaddleVlLlamaRequestFromImageBytes(
    const std::vector<unsigned char>& imageBytes,
    const std::string& mimeType,
    const std::string& modelName,
    const std::wstring& prompt,
    int maxTokens)
{
    PaddleVlLlamaRequest request;
    request.imageBytes = imageBytes.size();
    request.imageMime = Utf8ToWide(mimeType);
    request.pngBytes = mimeType == "image/png" ? imageBytes.size() : 0;
    if (imageBytes.empty()) {
        request.error = L"Encoded image is empty.";
        return request;
    }
    if (mimeType != "image/png" && mimeType != "image/jpeg") {
        request.error = L"Unsupported image MIME type.";
        return request;
    }
    if (modelName.empty()) {
        request.error = L"Model name is empty.";
        return request;
    }
    if (maxTokens <= 0) {
        request.error = L"max_tokens must be positive.";
        return request;
    }

    const std::string dataUri =
        "data:" + mimeType + ";base64," + Base64Encode(imageBytes);
    const std::string escapedModel = EscapeUtf8Json(Utf8ToWide(modelName));
    const std::wstring effectivePrompt = prompt.empty() ? L"OCR:" : prompt;
    const std::string escapedPrompt = EscapeUtf8Json(effectivePrompt);

    request.jsonBody =
        "{\"model\":\"" + escapedModel + "\","
        "\"messages\":[{\"role\":\"user\",\"content\":["
        "{\"type\":\"image_url\",\"image_url\":{\"url\":\"" + dataUri + "\"}},"
        "{\"type\":\"text\",\"text\":\"" + escapedPrompt + "\"}]}],"
        "\"temperature\":0,"
        "\"max_tokens\":" + std::to_string(maxTokens) + ","
        "\"skip_special_tokens\":true}";
    return request;
}

PaddleVlLlamaRequest BuildPaddleVlLlamaRequest(
    HBITMAP bitmap,
    const std::string& modelName,
    const std::wstring& prompt,
    int maxTokens,
    PaddleVlImageEncoding encoding)
{
    if (!bitmap) {
        PaddleVlLlamaRequest request;
        request.error = L"Bitmap is null.";
        return request;
    }
    if (encoding == PaddleVlImageEncoding::LegacyJpeg95) {
        return BuildPaddleVlLlamaRequestFromImageBytes(
            HBitmapToJpeg(bitmap, 95), "image/jpeg", modelName, prompt, maxTokens);
    }
    return BuildPaddleVlLlamaRequestFromPng(
        HBitmapToPng(bitmap), modelName, prompt, maxTokens);
}

PaddleVlRepetitionResult ApplyPaddleVlRepetitionGuard(
    const std::wstring& content,
    bool tableContent)
{
    const size_t activationLength = tableContent ? 5000 : 50;
    if (content.size() < activationLength) return {content, L"none", false};

    PaddleVlRepetitionResult result = DetectSuffixRepeat(content);
    if (result.changed) return result;
    result = DetectFullRepeat(content);
    if (result.changed) return result;
    result = DetectLineRepeat(content);
    if (result.changed) return result;
    return {content, L"none", false};
}

bool ShouldRetryPaddleVlLlamaFailure(const PaddleVlLlamaResult& result) {
    if (result.success) return false;
    const std::wstring& category = result.metrics.errorCategory;
    if (category == L"timeout" || category == L"http_transport" ||
        category == L"response_empty") {
        return true;
    }
    if (category != L"http_status") return false;
    const int status = result.metrics.httpStatus;
    return status == 408 || status == 425 || status == 429 || status >= 500;
}

PaddleVlLlamaResult SendPaddleVlLlamaRequest(
    HBITMAP bitmap,
    const std::wstring& endpoint,
    const std::string& modelName,
    const std::wstring& prompt,
    bool tableContent,
    int timeoutMs,
    int maxTokens,
    PaddleVlImageEncoding encoding)
{
    PaddleVlLlamaResult result;
    result.metrics.timeoutMs = timeoutMs;
    const ULONGLONG start = GetTickCount64();
    LARGE_INTEGER requestBuildStart = {};
    LARGE_INTEGER requestBuildEnd = {};
    LARGE_INTEGER performanceFrequency = {};
    QueryPerformanceFrequency(&performanceFrequency);
    QueryPerformanceCounter(&requestBuildStart);

    PaddleVlLlamaRequest request = BuildPaddleVlLlamaRequest(
        bitmap, modelName, prompt, maxTokens, encoding);
    QueryPerformanceCounter(&requestBuildEnd);
    if (performanceFrequency.QuadPart > 0) {
        result.metrics.requestBuildUs = static_cast<DWORD>(
            ((requestBuildEnd.QuadPart - requestBuildStart.QuadPart) * 1000000ull) /
            performanceFrequency.QuadPart);
    }
    result.metrics.imageBytes = request.imageBytes;
    result.metrics.pngBytes = request.pngBytes;
    result.metrics.imageMime = request.imageMime;
    result.metrics.requestBytes = request.jsonBody.size();
    if (!request.error.empty()) {
        result.error = request.error;
        result.metrics.errorCategory = request.imageBytes == 0
            ? L"image_encode" : L"request_build";
        result.metrics.elapsedMs = static_cast<DWORD>(GetTickCount64() - start);
        return result;
    }

    const std::vector<std::wstring> headers = {L"Content-Type: application/json"};
    HttpResponse response = HttpPost(endpoint, request.jsonBody, headers, timeoutMs);
    result.metrics.elapsedMs = static_cast<DWORD>(GetTickCount64() - start);
    result.metrics.httpStatus = response.statusCode;
    result.metrics.responseBytes = response.body.size();

    if (!response.error.empty()) {
        result.error = L"HTTP request failed: " + response.error;
        result.metrics.errorCategory = ContainsNoCase(response.error, L"timeout")
            ? L"timeout" : L"http_transport";
        return result;
    }
    if (response.statusCode != 200) {
        // OWN-127: pure int-label HTTP status (WideStringUtils).
        result.error = L"HTTP error: status code " + WideFormatIntLabel(response.statusCode);
        result.metrics.errorCategory = L"http_status";
        return result;
    }

    VlmResponse parsed = ParseVlmResponse(response.body);
    result.metrics.finishReason = parsed.finishReason;
    result.metrics.promptTokens = parsed.promptTokens;
    result.metrics.completionTokens = parsed.completionTokens;
    result.metrics.totalTokens = parsed.totalTokens;
    if (!parsed.success) {
        result.error = parsed.error;
        result.metrics.errorCategory = parsed.error == L"Empty content in response"
            ? L"response_empty" : L"response_parse";
        return result;
    }

    PaddleVlRepetitionResult guarded = ApplyPaddleVlRepetitionGuard(
        parsed.content, tableContent);
    result.content = std::move(guarded.content);
    result.metrics.repetitionReason = guarded.reason;
    result.metrics.errorCategory = L"none";
    result.success = true;
    return result;
}

PaddleVlLlamaServerInfo ProbePaddleVlLlamaServer(
    const std::wstring& baseUrl,
    const std::string& requestedModel,
    int timeoutMs)
{
    PaddleVlLlamaServerInfo info;
    std::wstring normalized = baseUrl;
    while (!normalized.empty() && normalized.back() == L'/') normalized.pop_back();
    if (normalized.empty()) {
        info.warning = L"llama-server base URL is empty";
        return info;
    }

    HttpResponse models = HttpGet(normalized + L"/v1/models", timeoutMs);
    info.modelsHttpStatus = models.statusCode;
    info.modelsResponseBytes = models.body.size();
    info.modelsReachable = models.error.empty() && models.statusCode == 200;
    const std::wstring modelsBody = Utf8ToWide(models.body);
    const std::wstring requestedModelWide = Utf8ToWide(requestedModel);
    info.modelListed = info.modelsReachable && !requestedModelWide.empty() &&
        ContainsNoCase(modelsBody, requestedModelWide);

    HttpResponse props = HttpGet(normalized + L"/props", timeoutMs);
    info.propsHttpStatus = props.statusCode;
    info.propsResponseBytes = props.body.size();
    info.propsReachable = props.error.empty() && props.statusCode == 200;
    const std::wstring propsBody = Utf8ToWide(props.body);
    if (info.propsReachable) {
        info.multimodal = ContainsNoCase(propsBody, L"multimodal") ||
            ContainsNoCase(propsBody, L"vision") ||
            ContainsNoCase(propsBody, L"mmproj") ||
            ContainsNoCase(propsBody, L"image");
        info.totalSlots = ReadNonNegativeJsonInt(
            propsBody, {L"total_slots", L"n_slots", L"parallel"});
        info.slotContext = ReadNonNegativeJsonInt(
            propsBody, {L"n_ctx_per_seq", L"slot_context", L"n_ctx", L"ctx_size"});
    }

    if (!info.modelsReachable || !info.propsReachable) {
        info.warning = L"llama-server capability probe incomplete";
    } else if (!info.modelListed) {
        info.warning = L"requested model name not present in /v1/models";
    } else if (!info.multimodal) {
        info.warning = L"/props did not expose an explicit multimodal marker";
    }
    return info;
}

bool ValidatePaddleVlLlamaServerCapability(
    const PaddleVlLlamaServerInfo& info,
    std::wstring& error)
{
    error.clear();
    if (!info.modelsReachable) {
        error = L"llama-server /v1/models capability probe failed";
    } else if (!info.propsReachable) {
        error = L"llama-server /props capability probe failed";
    } else if (!info.modelListed) {
        error = L"llama-server did not list the requested PaddleOCR-VL model";
    } else if (!info.multimodal) {
        error = L"llama-server did not report multimodal image support";
    }
    if (!error.empty()) {
        if (!info.warning.empty()) error += L": " + info.warning;
        return false;
    }
    return true;
}
