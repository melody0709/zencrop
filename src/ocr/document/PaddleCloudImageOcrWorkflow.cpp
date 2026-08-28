#include "PaddleCloudImageOcrWorkflow.h"

#include "PaddleCloudDocumentTransport.h"
#include "HttpTransport.h"
#include "OcrPaddleVlJson.h"

#include <algorithm>

namespace {

const char* JsonBool(bool value) {
    return value ? "true" : "false";
}

std::wstring Utf8ToWide(const std::string& text) {
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.length(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.length(), &result[0], len);
    return result;
}

std::string BuildOptionalPayload(bool chartRecognition) {
    std::string payload = "{\"useDocOrientationClassify\":false,"
        "\"useDocUnwarping\":false,"
        "\"useChartRecognition\":";
    payload += JsonBool(chartRecognition);
    payload += "}";
    return payload;
}

} // namespace

PaddleCloudRequest::UploadImage PaddleCloudRequest::EncodeUploadImage(
    HBITMAP hBitmap,
    long jpegQuality)
{
    UploadImage image;
    image.bytes = HBitmapToJpeg(hBitmap, jpegQuality);
    if (!image.bytes.empty()) {
        image.filename = "zencrop.jpg";
        image.contentType = "image/jpeg";
        return image;
    }

    image.bytes = HBitmapToPng(hBitmap);
    if (!image.bytes.empty()) {
        image.filename = "zencrop.png";
        image.contentType = "image/png";
        image.usedPngFallback = true;
    }
    return image;
}

std::string PaddleCloudRequest::BuildMultipartBody(
    const UploadImage& image,
    const std::string& modelName,
    const std::string& optionalPayload,
    const std::string& boundary)
{
    std::string body;
    body.reserve(image.bytes.size() + modelName.size() + optionalPayload.size() + 512);

    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
    body += modelName + "\r\n";

    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"optionalPayload\"\r\n\r\n";
    body += optionalPayload + "\r\n";

    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + image.filename + "\"\r\n";
    body += "Content-Type: " + image.contentType + "\r\n\r\n";
    body.append((const char*)image.bytes.data(), image.bytes.size());
    body += "\r\n--" + boundary + "--\r\n";

    return body;
}

bool IsPaddleCloudImageOcrJobsEndpoint(
    const std::wstring& url,
    std::wstring& error)
{
    return IsOfficialPaddleCloudJobsEndpoint(url, error);
}

OcrOutput RecognizePaddleCloudImage(const PaddleCloudImageOcrRequest& request) {
    OcrOutput result;

    std::wstring endpointError;
    if (!IsPaddleCloudImageOcrJobsEndpoint(request.jobsEndpoint, endpointError)) {
        result.error = endpointError.empty()
            ? L"PaddleOCR Cloud requires the official async jobs API URL."
            : endpointError;
        return result;
    }

    const std::string modelName = "PaddleOCR-VL-1.6";
    const std::string optionalPayload = BuildOptionalPayload(request.chartRecognition);
    const std::string boundary =
        "----ZenCropPaddleOCR" + std::to_string(GetTickCount64());
    const std::string multipartBody = PaddleCloudRequest::BuildMultipartBody(
        request.image, modelName, optionalPayload, boundary);

    WinHttpPaddleCloudDocumentClient documentHttp;
    PaddleCloudAsyncSubmitResult submitted = SubmitPaddleCloudAsyncJob(
        request.jobsEndpoint,
        request.bearerToken,
        multipartBody,
        L"multipart/form-data; boundary=" + Utf8ToWide(boundary),
        request.timeoutMs,
        documentHttp);
    if (!submitted.success) {
        result.error = submitted.error.empty()
            ? L"Async API submit response was rejected."
            : L"Async API submit failed: " + submitted.error;
        if (submitted.ambiguous) {
            result.error += L" The server may have accepted the job; automatic replay is disabled.";
        }
        return result;
    }
    const std::wstring jobId = submitted.envelope.jobId;

    DWORD maxWaitMs = request.timeoutMs;
    if (maxWaitMs < 120000) maxWaitMs = 120000;

    ULONGLONG pollStart = GetTickCount64();
    std::wstring jsonUrl;
    std::wstring markdownUrl;
    DWORD pollDelayMs = 3000;
    while (GetTickCount64() - pollStart < maxWaitMs) {
        PaddleCloudDocumentPollResult poll = PollPaddleCloudDocument(
            request.jobsEndpoint,
            request.bearerToken,
            jobId,
            15000,
            documentHttp);
        if (!poll.success) {
            if (poll.retrySameJob) {
                Sleep(pollDelayMs);
                pollDelayMs = (std::min<DWORD>)(15000, pollDelayMs + pollDelayMs / 2);
                continue;
            }
            result.error = L"Async API polling failed: " + poll.error;
            return result;
        }
        if (poll.state == DocumentOcrTransportState::Downloading) {
            jsonUrl = poll.jsonUrl;
            markdownUrl = poll.markdownUrl;
            break;
        }
        if (poll.state == DocumentOcrTransportState::Failed ||
            poll.state == DocumentOcrTransportState::Expired) {
            result.error = poll.error.empty()
                ? L"Async API job failed or expired."
                : L"Async API job failed: " + poll.error;
            return result;
        }

        Sleep(pollDelayMs);
        pollDelayMs = (std::min<DWORD>)(15000, pollDelayMs + pollDelayMs / 2);
    }

    if (jsonUrl.empty() && markdownUrl.empty()) {
        result.error = L"Async API job timed out.";
        return result;
    }

    std::string resultJson;
    if (!jsonUrl.empty()) {
        std::wstring urlError;
        if (!IsSafePaddleCloudResourceUrl(jsonUrl, urlError)) {
            result.error = urlError;
            return result;
        }
        HttpRequestOptions options;
        options.timeoutMs = 30000;
        options.maxResponseBytes = 128ull * 1024ull * 1024ull;
        options.allowRedirects = false;
        HttpResponse jsonRes = HttpGet(jsonUrl, {}, options);
        if (!jsonRes.error.empty()) {
            result.error = L"Cloud result download failed: " +
                RedactDocumentOcrSensitiveText(jsonRes.error);
            return result;
        }
        if (!jsonRes.finalUrl.empty() &&
            !IsSamePaddleCloudUrlTarget(jsonUrl, jsonRes.finalUrl)) {
            result.error = L"Cloud result download unexpectedly redirected.";
            return result;
        }
        if (jsonRes.statusCode == 200 && !jsonRes.body.empty()) {
            resultJson = jsonRes.body;
        }
    }

    std::wstring text;
    if (!resultJson.empty()) {
        text = ParsePaddleVlResponse(resultJson);
        result.rawOcrJson = Utf8ToWide(resultJson);
        result.debugOutputImagesJson =
            ExtractPaddleVlOutputImagesJson(result.rawOcrJson);
        result.blocks = ParsePaddleVlLayoutBlocks(result.rawOcrJson);
        result.bboxes.reserve(result.blocks.size());
        result.bboxClasses.reserve(result.blocks.size());
        for (const auto& block : result.blocks) {
            result.bboxes.push_back(block.bbox);
            result.bboxClasses.push_back(block.label);
        }
        if (text.empty()) text = ParsePaddleResponse(resultJson);
        if (!text.empty()) {
            ProcessImagesInResponse(resultJson, text, &result.embeddedAssets);
        }
    }

    if (text.empty() && !markdownUrl.empty()) {
        std::wstring urlError;
        if (!IsSafePaddleCloudResourceUrl(markdownUrl, urlError)) {
            result.error = urlError;
            return result;
        }
        HttpRequestOptions options;
        options.timeoutMs = 30000;
        options.maxResponseBytes = 32ull * 1024ull * 1024ull;
        options.allowRedirects = false;
        HttpResponse mdRes = HttpGet(markdownUrl, {}, options);
        if (!mdRes.error.empty()) {
            result.error = L"Cloud Markdown download failed: " +
                RedactDocumentOcrSensitiveText(mdRes.error);
            return result;
        }
        if (!mdRes.finalUrl.empty() &&
            !IsSamePaddleCloudUrlTarget(markdownUrl, mdRes.finalUrl)) {
            result.error = L"Cloud Markdown download unexpectedly redirected.";
            return result;
        }
        if (mdRes.statusCode == 200 && !mdRes.body.empty()) {
            text = Utf8ToWide(mdRes.body);
        }
    }

    if (text.empty()) {
        result.error = L"Async API completed but returned no recognized text.";
        return result;
    }

    result.success = true;
    result.text = text;
    return result;
}
