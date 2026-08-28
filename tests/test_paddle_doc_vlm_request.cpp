#include "ocr/engine/PaddleVlLlamaClient.h"
#include "ocr/OcrUtils.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

void TestOfficialWireRequest() {
    const std::vector<unsigned char> png = {0x89, 0x50};
    auto request = BuildPaddleVlLlamaRequestFromPng(
        png, "PaddleOCR-VL-1.6", L"OCR:\n\"quoted\"", 4096);
    Expect(request.error.empty(), "request builder rejected valid PNG bytes");
    Expect(request.pngBytes == 2, "PNG byte count was not preserved");
    Expect(request.jsonBody.find("data:image/png;base64,iVA=") != std::string::npos,
        "request does not use a padded PNG data URI");
    Expect(request.jsonBody.find("\"text\":\"OCR:\\n\\\"quoted\\\"\"") != std::string::npos,
        "business prompt is not JSON escaped exactly");
    Expect(request.jsonBody.find("<__media__>") == std::string::npos,
        "legacy fixed media marker leaked into request");
    Expect(request.jsonBody.find("\"temperature\":0") != std::string::npos,
        "temperature must be zero");
    Expect(request.jsonBody.find("\"max_tokens\":4096") != std::string::npos,
        "request must use 4096 max tokens");
    Expect(request.jsonBody.find("\"skip_special_tokens\":true") != std::string::npos,
        "non-spotting request must skip special tokens");

    auto longRequest = BuildPaddleVlLlamaRequestFromPng(
        png, "PaddleOCR-VL-1.6", L"Table Recognition:", 8192);
    Expect(longRequest.error.empty() &&
        longRequest.jsonBody.find("\"max_tokens\":8192") != std::string::npos,
        "explicit long-content experiment must be selectable as 8192");

    auto jpegRequest = BuildPaddleVlLlamaRequestFromImageBytes(
        png, "image/jpeg", "PaddleOCR-VL-1.6", L"OCR:", 4096);
    Expect(jpegRequest.error.empty() && jpegRequest.pngBytes == 0 &&
        jpegRequest.imageBytes == png.size() &&
        jpegRequest.jsonBody.find("data:image/jpeg;base64,iVA=") != std::string::npos,
        "explicit legacy JPEG A/B request must keep an accurate MIME/byte contract");
}

void TestResponseMetadata() {
    const std::string body =
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{"
        "\"content\":\"hello\\nworld\"}}],\"usage\":{"
        "\"prompt_tokens\":101,\"completion_tokens\":17,\"total_tokens\":118}}";
    auto response = ParseVlmResponse(body);
    Expect(response.success && response.content == L"hello\nworld",
        "response content parsing regressed");
    Expect(response.finishReason == L"stop", "finish_reason was not parsed");
    Expect(response.promptTokens == 101 && response.completionTokens == 17 &&
        response.totalTokens == 118, "usage token counts were not parsed");

    response = ParseVlmResponse("{\"choices\":[]}");
    Expect(!response.success && !response.error.empty(),
        "missing content must remain an explicit parse error");

    response = ParseVlmResponse(
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{"
        "\"content\":\" \\n\\t\"}}],\"usage\":{\"prompt_tokens\":5,"
        "\"completion_tokens\":0,\"total_tokens\":5}}");
    Expect(!response.success && response.error == L"Empty content in response" &&
        response.finishReason == L"stop" && response.promptTokens == 5 &&
        response.completionTokens == 0 && response.totalTokens == 5,
        "empty OCR content must fail while preserving response metadata");
}

void TestRetryClassification() {
    PaddleVlLlamaResult result;
    result.metrics.errorCategory = L"image_encode";
    Expect(!ShouldRetryPaddleVlLlamaFailure(result),
        "image encoding failures are permanent and must not be retried");
    result.metrics.errorCategory = L"request_build";
    Expect(!ShouldRetryPaddleVlLlamaFailure(result),
        "request construction failures are permanent and must not be retried");
    result.metrics.errorCategory = L"response_parse";
    Expect(!ShouldRetryPaddleVlLlamaFailure(result),
        "response schema failures must not be retried blindly");

    result.metrics.errorCategory = L"http_status";
    result.metrics.httpStatus = 400;
    Expect(!ShouldRetryPaddleVlLlamaFailure(result),
        "HTTP 400 must fail without an identical retry");
    result.metrics.httpStatus = 429;
    Expect(ShouldRetryPaddleVlLlamaFailure(result),
        "HTTP 429 should receive the one transient retry");
    result.metrics.httpStatus = 503;
    Expect(ShouldRetryPaddleVlLlamaFailure(result),
        "HTTP 5xx should receive the one transient retry");

    result.metrics.errorCategory = L"timeout";
    Expect(ShouldRetryPaddleVlLlamaFailure(result),
        "timeouts should receive the one transient retry");
    result.metrics.errorCategory = L"http_transport";
    Expect(ShouldRetryPaddleVlLlamaFailure(result),
        "transport failures should receive the one transient retry");
    result.metrics.errorCategory = L"response_empty";
    Expect(ShouldRetryPaddleVlLlamaFailure(result),
        "an empty model generation should receive the one transient retry");

    result.success = true;
    Expect(!ShouldRetryPaddleVlLlamaFailure(result),
        "successful responses must never be retried");
}

void TestRepetitionGuardBoundariesAndPriority() {
    std::wstring suffixUnit = L"0123456789abcdefghij";
    std::wstring suffixText = L"HEAD";
    for (int i = 0; i < 5; ++i) suffixText += suffixUnit;
    auto result = ApplyPaddleVlRepetitionGuard(suffixText, false);
    Expect(result.changed && result.reason == L"suffix_repeat" &&
        result.content == L"HEAD", "suffix repetition branch did not win priority");

    std::wstring fullRepeat;
    for (int i = 0; i < 10; ++i) fullRepeat += L"abcde";
    result = ApplyPaddleVlRepetitionGuard(fullRepeat, false);
    Expect(result.changed && result.reason == L"full_repeat" &&
        result.content == L"abcde", "shortest full repetition unit was not returned");

    std::wstring lineRepeat;
    for (int i = 0; i < 10; ++i) lineRepeat += L"same line\n";
    result = ApplyPaddleVlRepetitionGuard(lineRepeat, false);
    Expect(result.changed && result.reason == L"line_repeat" &&
        result.content == L"same line", "repeated-line branch failed");

    result = ApplyPaddleVlRepetitionGuard(std::wstring(49, L'x'), false);
    Expect(!result.changed && result.reason == L"none" && result.content.size() == 49,
        "non-table guard activated below 50 characters");

    result = ApplyPaddleVlRepetitionGuard(std::wstring(4999, L'x'), true);
    Expect(!result.changed && result.reason == L"none",
        "table guard activated below 5000 characters");

    std::wstring equalTail = std::wstring(100, L'p');
    for (int i = 0; i < 5; ++i) equalTail += L"0123456789abcdefghij";
    result = ApplyPaddleVlRepetitionGuard(equalTail, false);
    Expect(!result.changed,
        "suffix occupying exactly 50 percent must not be removed");

    std::wstring ordinaryLongText;
    for (int index = 0; index < 80; ++index) {
        ordinaryLongText += L"Distinct paragraph line " +
            std::to_wstring(index) + L" with ordinary prose.\n";
    }
    result = ApplyPaddleVlRepetitionGuard(ordinaryLongText, false);
    Expect(!result.changed && result.content == ordinaryLongText,
        "ordinary long multi-line content must not be truncated");
}

void TestServerCapabilityValidation() {
    auto info = ProbePaddleVlLlamaServer(L"", "PaddleOCR-VL-1.6", 1);
    std::wstring error;
    Expect(!info.modelsReachable && !info.propsReachable && !info.warning.empty(),
        "empty server base URL must produce explicit probe diagnostics");
    Expect(!ValidatePaddleVlLlamaServerCapability(info, error) && !error.empty(),
        "unreachable capability endpoints must fail before image requests");

    info = {};
    info.modelsReachable = true;
    info.propsReachable = true;
    info.multimodal = true;
    Expect(!ValidatePaddleVlLlamaServerCapability(info, error) &&
        error.find(L"requested") != std::wstring::npos,
        "missing requested model must fail capability validation");

    info.modelListed = true;
    info.multimodal = false;
    Expect(!ValidatePaddleVlLlamaServerCapability(info, error) &&
        error.find(L"multimodal") != std::wstring::npos,
        "missing multimodal support must fail capability validation");

    info.multimodal = true;
    Expect(ValidatePaddleVlLlamaServerCapability(info, error) && error.empty(),
        "complete model and multimodal capability snapshot must pass");
}

} // namespace

int main() {
    TestOfficialWireRequest();
    TestResponseMetadata();
    TestRetryClassification();
    TestRepetitionGuardBoundariesAndPriority();
    TestServerCapabilityValidation();
    std::cout << "Paddle Doc VLM request/response contract passed.\n";
    return 0;
}
