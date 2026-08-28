#include "PaddleCloudDocumentProtocol.h"

#include "JsonUtils.h"
#include "OcrBlockJson.h"
#include "PaddleCloudDocumentNormalizer.h"
#include "Sha256.h"
#include "core/WideStringUtils.h"
#include "dashboard/DashboardFileTypes.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <limits>

namespace {

constexpr uint64_t kConservativeNativePdfBytes = 50'000'000ull;

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (size <= 0) return L"";
    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            size) != size) {
        return L"";
    }
    return wide;
}

std::string WideToUtf8(const std::wstring& text, std::wstring& error) {
    error.clear();
    if (text.empty()) return {};
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        error = L"Cloud request JSON is too large to encode as UTF-8.";
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        error = L"Failed to encode Cloud request JSON as UTF-8.";
        return {};
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            utf8.data(),
            size,
            nullptr,
            nullptr) != size) {
        error = L"Failed to encode complete Cloud request JSON as UTF-8.";
        return {};
    }
    return utf8;
}

// OWN-78: thin wrappers over pure WideStringUtils.
std::wstring Lower(std::wstring value) {
    return WideToLower(std::move(value));
}

bool IsExpectedCloudEngine(const NativePdfEligibilityInput& input) {
    const std::wstring engine = Lower(TrimString(input.engineMode));
    const std::wstring model = Lower(TrimString(input.model));
    return engine == L"paddle_cloud" && model == L"paddleocr-vl-1.6";
}

void AppendTextPart(
    std::string& body,
    const std::string& boundary,
    const char* name,
    const std::string& value)
{
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"";
    body += name;
    body += "\"\r\n\r\n";
    body += value;
    body += "\r\n";
}

std::wstring FirstMessage(const std::wstring& json) {
    const wchar_t* keys[] = {L"errorMsg", L"message", L"msg", L"error"};
    for (const wchar_t* key : keys) {
        std::wstring value = OcrBlockJsonText(json, key);
        if (!value.empty()) return value;
    }
    return L"";
}

bool IsTransientHttp(int status) {
    return status == 408 || status == 425 || status == 429 || status == 503 || status >= 500;
}

bool MessageSuggestsTransientQueue(const std::wstring& message) {
    const std::wstring lower = Lower(message);
    return lower.find(L"queue") != std::wstring::npos ||
        lower.find(L"frequency") != std::wstring::npos ||
        lower.find(L"too many") != std::wstring::npos ||
        lower.find(L"频率") != std::wstring::npos ||
        lower.find(L"排队") != std::wstring::npos;
}

bool IsValidMultipartBoundary(const std::string& boundary) {
    if (boundary.empty() || boundary.size() > 70) return false;
    for (unsigned char ch : boundary) {
        if (std::isalnum(ch)) continue;
        switch (ch) {
        case '\'': case '(': case ')': case '+': case '_': case ',': case '-':
        case '.': case '/': case ':': case '=': case '?':
            continue;
        default:
            return false;
        }
    }
    return true;
}

bool ContainsMultipartDelimiter(
    const std::string& value,
    const std::string& boundary)
{
    return value.find("--" + boundary) != std::string::npos;
}

bool LooksLikeJsonObject(const std::wstring& json) {
    std::wstring error;
    return ValidatePaddleCloudJsonObjectSyntax(json, error);
}

// OWN-73: pure HTTP scheme detect + product authority hygiene.
bool IsHttpOrHttpsUrl(const std::wstring& value) {
    if (!DashboardIsHttpUrlWide(value) ||
        value.find(L'\r') != std::wstring::npos ||
        value.find(L'\n') != std::wstring::npos ||
        value.find(L' ') != std::wstring::npos) {
        return false;
    }
    const size_t authority = value.find(L"://") + 3;
    const size_t path = value.find(L'/', authority);
    const size_t authorityEnd = path == std::wstring::npos ? value.size() : path;
    return authority < value.size() &&
        value.find(L'@', authority) >= authorityEnd;
}

// OWN-78: pure strict int parse (WideStringUtils).
bool TryParseEnvelopeCode(const std::wstring& raw, int& code) {
    return WideTryParseJsonIntToken(raw, code);
}

} // namespace

NativePdfEligibilityDecision EvaluatePaddleCloudNativePdfEligibility(
    const NativePdfEligibilityInput& input)
{
    NativePdfEligibilityDecision decision;
    auto reject = [&](const wchar_t* reason) {
        decision.reason = reason;
        return decision;
    };

    if (!input.featureFlagEnabled) return reject(L"Native PDF feature flag is disabled.");
    if (!input.gateProfileVerified) return reject(L"Provider/model native PDF profile is not Gate-0 verified.");
    if (!IsExpectedCloudEngine(input)) return reject(L"Selected engine/model does not have a native PDF capability profile.");
    if (!input.providerHealthy) return reject(L"Provider health/cooldown requires raster fallback.");
    if (input.localOnly) return reject(L"Task is local-only.");
    if (input.encrypted || input.requiresPassword) return reject(L"Encrypted/password PDF must use local raster rendering.");
    if (input.sourceBytes == 0) return reject(L"Source PDF size is unavailable.");
    if (input.sourceBytes > kConservativeNativePdfBytes) return reject(L"Source PDF exceeds the conservative 50 MB native limit.");
    if (input.sourcePageCount <= 0 || input.sourcePageCount > 100) return reject(L"Source PDF page count is outside the conservative 1..100 native range.");
    if (input.requestedPages.empty() || input.requestedPages.size() > 100) return reject(L"Requested page list is empty or exceeds 100 pages.");
    if (!input.allPagesSelected && !input.allowPartialPageRanges) return reject(L"Partial page ranges are not enabled for native full-file upload.");
    if (!input.fullPdfConsentGranted) return reject(L"Full-PDF Cloud upload consent was not granted.");

    decision.canonicalPageRanges = BuildCanonicalCloudPageRanges(input.requestedPages);
    if (decision.canonicalPageRanges.empty()) return reject(L"Requested pages are not positive, unique, and ascending.");
    if (input.requestedPages.back() > input.sourcePageCount) {
        return reject(L"Requested page list exceeds the source PDF page count.");
    }
    if (input.allPagesSelected) {
        if (input.requestedPages.size() != static_cast<size_t>(input.sourcePageCount)) {
            return reject(L"All-pages selection does not contain every source PDF page.");
        }
        for (int page = 1; page <= input.sourcePageCount; ++page) {
            if (input.requestedPages[static_cast<size_t>(page - 1)] != page) {
                return reject(L"All-pages selection is not the exact 1..N source page sequence.");
            }
        }
    }
    decision.eligible = true;
    decision.transportKind = L"cloud_native_pdf";
    decision.reason = L"Eligible for opt-in native PDF document transport.";
    return decision;
}

PaddleCloudPdfMultipartRequest BuildPaddleCloudPdfMultipartRequest(
    const std::vector<unsigned char>& pdfBytes,
    const std::string& model,
    const std::string& optionalPayload,
    const std::string& pageRanges,
    const std::string& batchId,
    const std::string& boundary)
{
    PaddleCloudPdfMultipartRequest request;
    request.boundary = boundary;
    if (pdfBytes.empty()) {
        request.error = L"PDF payload is empty.";
        return request;
    }
    if (model.empty()) {
        request.error = L"Cloud model is empty.";
        return request;
    }
    if (!IsValidMultipartBoundary(boundary)) {
        request.error = L"Multipart boundary is empty or invalid.";
        return request;
    }
    if (ContainsMultipartDelimiter(model, boundary) ||
        ContainsMultipartDelimiter(optionalPayload, boundary) ||
        ContainsMultipartDelimiter(pageRanges, boundary) ||
        ContainsMultipartDelimiter(batchId, boundary) ||
        std::search(
            pdfBytes.begin(),
            pdfBytes.end(),
            boundary.begin(),
            boundary.end()) != pdfBytes.end()) {
        request.error = L"Multipart boundary collides with request content.";
        return request;
    }

    request.contentType = "multipart/form-data; boundary=" + boundary;
    request.body.reserve(pdfBytes.size() + model.size() + optionalPayload.size() + 1024);
    AppendTextPart(request.body, boundary, "model", model);
    AppendTextPart(request.body, boundary, "optionalPayload", optionalPayload.empty() ? "{}" : optionalPayload);
    if (!pageRanges.empty()) AppendTextPart(request.body, boundary, "pageRanges", pageRanges);
    if (!batchId.empty()) AppendTextPart(request.body, boundary, "batchId", batchId);

    request.body += "--" + boundary + "\r\n";
    request.body += "Content-Disposition: form-data; name=\"file\"; filename=\"document.pdf\"\r\n";
    request.body += "Content-Type: application/pdf\r\n\r\n";
    request.body.append(reinterpret_cast<const char*>(pdfBytes.data()), pdfBytes.size());
    request.body += "\r\n--" + boundary + "--\r\n";
    return request;
}

PaddleCloudFileUrlJsonRequest BuildPaddleCloudFileUrlJsonRequest(
    const std::wstring& fileUrl,
    const std::wstring& model,
    const std::wstring& optionalPayload,
    const std::wstring& pageRanges,
    const std::wstring& batchId)
{
    PaddleCloudFileUrlJsonRequest request;
    if (!IsHttpOrHttpsUrl(fileUrl)) {
        request.error = L"Remote document fileUrl must use HTTP or HTTPS.";
        return request;
    }
    if (model.empty()) {
        request.error = L"Cloud model is empty.";
        return request;
    }
    const std::wstring payload = TrimString(optionalPayload.empty() ? L"{}" : optionalPayload);
    std::wstring payloadError;
    if (!ValidatePaddleCloudJsonObjectSyntax(payload, payloadError)) {
        request.error = L"Cloud optionalPayload must be a JSON object.";
        return request;
    }

    std::wstring json = L"{\"fileUrl\":\"" + EscapeJsonString(fileUrl) +
        L"\",\"model\":\"" + EscapeJsonString(model) +
        L"\",\"optionalPayload\":" + payload;
    if (!pageRanges.empty()) {
        json += L",\"pageRanges\":\"" + EscapeJsonString(pageRanges) + L"\"";
    }
    if (!batchId.empty()) {
        json += L",\"batchId\":\"" + EscapeJsonString(batchId) + L"\"";
    }
    json += L"}";
    request.body = WideToUtf8(json, request.error);
    return request;
}

bool BuildPaddleCloudAuthorizationHeader(
    const std::wstring& bearerToken,
    std::wstring& header,
    std::wstring& error)
{
    header.clear();
    error.clear();
    std::wstring token = TrimString(bearerToken);
    // OWN-73: pure starts-with for auth scheme strip.
    if (DashboardWideStartsWithNoCase(token, L"Bearer ")) token = TrimString(token.substr(7));
    if (DashboardWideStartsWithNoCase(token, L"Token ")) token = TrimString(token.substr(6));
    const bool unsafeControl = std::any_of(token.begin(), token.end(), [](wchar_t ch) {
        return ch < 0x20 || ch == 0x7f;
    });
    if (token.empty() || token.size() > 16 * 1024 || unsafeControl) {
        error = L"Cloud bearer token is empty or contains unsafe header characters.";
        return false;
    }
    header = L"Authorization: Bearer " + token;
    return true;
}

PaddleCloudApiEnvelope ParsePaddleCloudApiEnvelope(
    int httpStatus,
    const std::string& responseBody,
    bool submitPhase,
    bool alreadyHasJobId)
{
    PaddleCloudApiEnvelope envelope;
    envelope.httpStatus = httpStatus;
    bool codeValid = true;
    if (responseBody.size() > 1024 * 1024) {
        envelope.message = L"Cloud API response envelope exceeds the 1 MiB safety limit.";
        envelope.diagnosticCode = L"response_too_large";
        return envelope;
    }
    const std::wstring json = Utf8ToWide(responseBody);
    if (json.empty() && !responseBody.empty()) {
        envelope.message = L"Response body is not valid UTF-8 JSON.";
    } else if (!json.empty() && !LooksLikeJsonObject(json)) {
        envelope.message = L"Response body is not a complete JSON object.";
    } else if (!json.empty()) {
        std::wstring codeText = OcrBlockJsonExtractValue(json, L"code");
        envelope.codePresent = !codeText.empty();
        if (envelope.codePresent) {
            codeValid = TryParseEnvelopeCode(codeText, envelope.code);
            if (!codeValid) envelope.message = L"Cloud API response has an invalid body code.";
        }
        envelope.jobId = OcrBlockJsonText(json, L"jobId");
        envelope.state = OcrBlockJsonText(json, L"state");
        envelope.jsonUrl = OcrBlockJsonText(json, L"jsonUrl");
        envelope.markdownUrl = OcrBlockJsonText(json, L"markdownUrl");
        if (codeValid) envelope.message = FirstMessage(json);
    }

    envelope.message = RedactDocumentOcrSensitiveText(envelope.message);

    const bool bodySuccess = codeValid && (!envelope.codePresent || envelope.code == 0);
    envelope.success = httpStatus >= 200 && httpStatus < 300 && bodySuccess;
    envelope.submitAccepted = submitPhase && envelope.success && !envelope.jobId.empty();
    const bool transient = IsTransientHttp(httpStatus) ||
        MessageSuggestsTransientQueue(envelope.message);
    envelope.retrySameJob = alreadyHasJobId && transient;
    envelope.reconcileBeforeReplay = submitPhase && !alreadyHasJobId && transient;

    if (!envelope.success || (submitPhase && envelope.jobId.empty())) {
        if (envelope.codePresent && !codeValid) {
            envelope.diagnosticCode = L"body_code_invalid";
        } else if (envelope.codePresent) {
            // OWN-126: pure int-label diagnostic codes (WideStringUtils).
            envelope.diagnosticCode = L"body_code_" + WideFormatIntLabel(envelope.code);
        } else if (httpStatus > 0) {
            envelope.diagnosticCode = L"http_" + WideFormatIntLabel(httpStatus);
        } else {
            envelope.diagnosticCode = L"response_invalid";
        }
        if (envelope.message.empty()) {
            envelope.message = submitPhase && envelope.jobId.empty() && envelope.success
                ? L"Submit response has no jobId."
                : L"Cloud API request failed.";
        }
        envelope.success = false;
    }
    return envelope;
}

bool BuildPaddleCloudRequestFingerprint(
    const std::wstring& sourcePdfSha256,
    const std::wstring& model,
    const std::wstring& canonicalPageRanges,
    const std::wstring& optionalPayload,
    std::wstring& fingerprint,
    std::wstring& error)
{
    if (!IsSha256Hex(sourcePdfSha256) || model.empty() || canonicalPageRanges.empty()) {
        fingerprint.clear();
        error = L"Request fingerprint inputs are incomplete or invalid.";
        return false;
    }
    const std::wstring material =
        L"paddleocr_official_api\n" + sourcePdfSha256 + L"\n" +
        model + L"\n" + canonicalPageRanges + L"\n" + optionalPayload;
    return ComputeUtf8Sha256Hex(material, fingerprint, error);
}
