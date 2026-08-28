#include "PaddleCloudDocumentTransport.h"

#include "JsonUtils.h"
#include "OcrBlockJson.h"
#include "Sha256.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <limits>
#include "core/WideStringUtils.h"

namespace {

constexpr uint64_t kMaxNativePdfBytes = 50'000'000ull;
constexpr size_t kMaxDocumentJsonlBytes = 128ull * 1024ull * 1024ull;
constexpr wchar_t kPaddleCloudDocumentModel[] = L"PaddleOCR-VL-1.6";

bool IsSupportedPaddleCloudDocumentModel(const std::wstring& model) {
    return WideEqualsNoCase(TrimString(model), kPaddleCloudDocumentModel);
}

bool IsJsonResponseContentType(const std::wstring& contentType, bool allowPlainText) {
    if (contentType.empty()) return true;
    std::wstring lower = contentType;
    lower = WideToLower(std::move(lower)); // OWN-79
    const size_t parameters = lower.find(L';');
    const std::wstring mediaType = TrimString(lower.substr(0, parameters));
    return mediaType == L"application/json" ||
        mediaType == L"application/x-ndjson" ||
        mediaType == L"application/jsonl" ||
        mediaType == L"application/octet-stream" ||
        (allowPlainText && mediaType == L"text/plain");
}

bool SameUrlTarget(const std::wstring& left, const std::wstring& right) {
    auto parse = [](const std::wstring& url, URL_COMPONENTS& components) {
        components = {};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = 1;
        components.dwHostNameLength = 1;
        components.dwUserNameLength = 1;
        components.dwPasswordLength = 1;
        components.dwUrlPathLength = 1;
        components.dwExtraInfoLength = 1;
        return WinHttpCrackUrl(
            url.c_str(),
            static_cast<DWORD>(url.size()),
            0,
            &components) == TRUE;
    };
    URL_COMPONENTS a = {};
    URL_COMPONENTS b = {};
    if (!parse(left, a) || !parse(right, b)) return false;
    const std::wstring hostA(a.lpszHostName, a.dwHostNameLength);
    const std::wstring hostB(b.lpszHostName, b.dwHostNameLength);
    const std::wstring userA = a.dwUserNameLength > 0
        ? std::wstring(a.lpszUserName, a.dwUserNameLength)
        : L"";
    const std::wstring userB = b.dwUserNameLength > 0
        ? std::wstring(b.lpszUserName, b.dwUserNameLength)
        : L"";
    const std::wstring passwordA = a.dwPasswordLength > 0
        ? std::wstring(a.lpszPassword, a.dwPasswordLength)
        : L"";
    const std::wstring passwordB = b.dwPasswordLength > 0
        ? std::wstring(b.lpszPassword, b.dwPasswordLength)
        : L"";
    const std::wstring pathA(a.lpszUrlPath, a.dwUrlPathLength);
    const std::wstring pathB(b.lpszUrlPath, b.dwUrlPathLength);
    const std::wstring extraA = a.dwExtraInfoLength > 0
        ? std::wstring(a.lpszExtraInfo, a.dwExtraInfoLength)
        : L"";
    const std::wstring extraB = b.dwExtraInfoLength > 0
        ? std::wstring(b.lpszExtraInfo, b.dwExtraInfoLength)
        : L"";
    return a.nScheme == b.nScheme && a.nPort == b.nPort &&
        WideEqualsNoCase(hostA, hostB) &&
        userA == userB && passwordA == passwordB &&
        pathA == pathB && extraA == extraB;
}

std::string WideToUtf8(const std::wstring& text, std::wstring& error) {
    error.clear();
    if (text.empty()) return {};
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        error = L"Document request text is too large to encode as UTF-8.";
        return {};
    }
    int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        error = L"Failed to encode document request text as UTF-8.";
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
        error = L"Failed to encode complete document request text as UTF-8.";
        return {};
    }
    return utf8;
}

std::wstring Utf8ToWide(const std::string& text, std::wstring& error) {
    error.clear();
    if (text.empty()) return L"";
    int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (size <= 0) {
        error = L"Cloud document result is not valid UTF-8.";
        return L"";
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            size) != size) {
        error = L"Failed to decode the complete Cloud document result as UTF-8.";
        return L"";
    }
    return wide;
}

bool ReadBoundedFile(
    const std::wstring& path,
    uint64_t maxBytes,
    std::vector<unsigned char>& bytes,
    std::wstring& error)
{
    bytes.clear();
    error.clear();
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Failed to open source PDF.";
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        static_cast<uint64_t>(size.QuadPart) > maxBytes) {
        CloseHandle(file);
        error = L"Source PDF is empty or exceeds the native upload limit.";
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD chunk = static_cast<DWORD>((std::min)(
            bytes.size() - offset,
            static_cast<size_t>(1024 * 1024)));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr) || read == 0) {
            CloseHandle(file);
            bytes.clear();
            error = L"Failed to read the complete source PDF.";
            return false;
        }
        offset += read;
    }
    CloseHandle(file);
    return true;
}

bool SafeJobId(const std::wstring& jobId) {
    if (jobId.empty() || jobId.size() > 256) return false;
    for (wchar_t ch : jobId) {
        const bool asciiAlphaNumeric =
            (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9');
        if (!(asciiAlphaNumeric || ch == L'-' || ch == L'_')) return false;
    }
    return true;
}

std::string RandomMultipartBoundary(std::wstring& error) {
    unsigned char random[18] = {};
    if (BCryptGenRandom(
            nullptr,
            random,
            static_cast<ULONG>(sizeof(random)),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        error = L"Failed to generate a cryptographically random multipart boundary.";
        return {};
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string boundary = "----ZenCropPaddlePdf";
    boundary.reserve(boundary.size() + sizeof(random) * 2);
    for (unsigned char byte : random) {
        boundary.push_back(kHex[(byte >> 4) & 0x0f]);
        boundary.push_back(kHex[byte & 0x0f]);
    }
    return boundary;
}

std::wstring BuildJobUrl(std::wstring endpoint, const std::wstring& jobId) {
    while (!endpoint.empty() && endpoint.back() == L'/') endpoint.pop_back();
    return endpoint + L"/" + jobId;
}

DocumentOcrTransportState MapRemoteState(const std::wstring& state) {
    if (WideEqualsNoCase(state, L"pending")) return DocumentOcrTransportState::Pending;
    if (WideEqualsNoCase(state, L"running")) return DocumentOcrTransportState::Running;
    if (WideEqualsNoCase(state, L"done")) return DocumentOcrTransportState::Downloading;
    if (WideEqualsNoCase(state, L"failed")) return DocumentOcrTransportState::Failed;
    if (WideEqualsNoCase(state, L"expired")) return DocumentOcrTransportState::Expired;
    return DocumentOcrTransportState::Unknown;
}

int ExtractProgressInt(const std::string& body, const wchar_t* key) {
    std::wstring error;
    std::wstring json = Utf8ToWide(body, error);
    if (!error.empty()) return 0;
    return (std::max)(0, OcrBlockJsonInt(json, key, 0));
}

bool TryParseIpv4(
    const std::wstring& host,
    unsigned char& a,
    unsigned char& b,
    unsigned char& c,
    unsigned char& d)
{
    unsigned char* parts[] = {&a, &b, &c, &d};
    size_t cursor = 0;
    for (size_t part = 0; part < 4; ++part) {
        if (cursor >= host.size() || !iswdigit(host[cursor])) return false;
        int value = 0;
        size_t digits = 0;
        while (cursor < host.size() && iswdigit(host[cursor])) {
            value = value * 10 + (host[cursor] - L'0');
            if (value > 255 || ++digits > 3) return false;
            ++cursor;
        }
        *parts[part] = static_cast<unsigned char>(value);
        if (part + 1 < 4) {
            if (cursor >= host.size() || host[cursor++] != L'.') return false;
        }
    }
    return cursor == host.size();
}

bool IsStructurallyPublicHost(const std::wstring& host) {
    if (host.empty() || host.find(L'%') != std::wstring::npos) return false;
    unsigned char a = 0;
    unsigned char b = 0;
    unsigned char c = 0;
    unsigned char d = 0;
    // Until Gate 0 provides a provider resource-host allowlist and the network
    // layer can pin resolved addresses, every numeric literal is rejected.
    if (TryParseIpv4(host, a, b, c, d)) return false;
    if (host.find(L':') != std::wstring::npos) return false;

    std::wstring lower = host;
    lower = WideToLower(std::move(lower)); // OWN-79
    while (!lower.empty() && lower.back() == L'.') lower.pop_back();
    if (lower.empty() || lower.find(L'.') == std::wstring::npos ||
        lower.rfind(L"0x", 0) == 0 ||
        lower == L"localhost" ||
        (lower.size() > 10 && lower.substr(lower.size() - 10) == L".localhost") ||
        (lower.size() > 6 && lower.substr(lower.size() - 6) == L".local") ||
        (lower.size() > 9 && lower.substr(lower.size() - 9) == L".internal") ||
        (lower.size() > 10 && lower.substr(lower.size() - 10) == L".home.arpa")) {
        return false;
    }
    for (wchar_t ch : lower) {
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') ||
              ch == L'-' || ch == L'.')) {
            return false;
        }
    }
    if (std::none_of(lower.begin(), lower.end(), [](wchar_t ch) {
            return ch >= L'a' && ch <= L'z';
        })) {
        return false;
    }
    return true;
}

} // namespace

bool IsSamePaddleCloudUrlTarget(
    const std::wstring& expectedUrl,
    const std::wstring& actualUrl)
{
    return SameUrlTarget(expectedUrl, actualUrl);
}

HttpResponse WinHttpPaddleCloudDocumentClient::Post(
    const std::wstring& url,
    const std::string& body,
    const std::vector<std::wstring>& headers,
    int timeoutMs)
{
    HttpRequestOptions options;
    options.timeoutMs = timeoutMs;
    options.maxResponseBytes = 1024 * 1024;
    options.allowRedirects = false;
    return HttpPost(url, body, headers, options);
}

HttpResponse WinHttpPaddleCloudDocumentClient::Get(
    const std::wstring& url,
    const std::vector<std::wstring>& headers,
    int timeoutMs)
{
    HttpRequestOptions options;
    options.timeoutMs = timeoutMs;
    options.maxResponseBytes = kMaxDocumentJsonlBytes;
    options.allowRedirects = false;
    return HttpGet(url, headers, options);
}

PaddleCloudAsyncSubmitResult SubmitPaddleCloudAsyncJob(
    const std::wstring& jobsEndpoint,
    const std::wstring& bearerToken,
    const std::string& body,
    const std::wstring& contentType,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient)
{
    PaddleCloudAsyncSubmitResult result;
    if (body.empty() || contentType.empty()) {
        result.error = L"Cloud async submit body or content type is empty.";
        return result;
    }
    if (!IsOfficialPaddleCloudJobsEndpoint(jobsEndpoint, result.error)) return result;
    std::wstring authorization;
    if (!BuildPaddleCloudAuthorizationHeader(
            bearerToken,
            authorization,
            result.error)) {
        return result;
    }
    const std::wstring endpoint = NormalizePaddleOcrJobsUrl(jobsEndpoint);
    const std::vector<std::wstring> headers = {
        authorization,
        L"Content-Type: " + contentType
    };
    HttpResponse response = httpClient.Post(endpoint, body, headers, timeoutMs);
    if (!response.error.empty()) {
        result.ambiguous = true;
        result.error = RedactDocumentOcrSensitiveText(
            L"Cloud async submit transport failed: " + response.error);
        return result;
    }
    if (!response.finalUrl.empty() && !SameUrlTarget(response.finalUrl, endpoint)) {
        result.ambiguous = true;
        result.error = L"Cloud async submit unexpectedly redirected; replay is unsafe.";
        return result;
    }
    if (!IsJsonResponseContentType(response.contentType, false)) {
        result.error = L"Cloud async submit returned a non-JSON content type.";
        return result;
    }
    result.envelope = ParsePaddleCloudApiEnvelope(
        response.statusCode,
        response.body,
        true,
        false);
    result.ambiguous = result.envelope.reconcileBeforeReplay;
    if (!result.envelope.submitAccepted) {
        result.error = RedactDocumentOcrSensitiveText(result.envelope.message);
        return result;
    }
    result.success = true;
    return result;
}

PaddleCloudDocumentSubmitResult SubmitPaddleCloudDocument(
    const PaddleCloudDocumentSubmitRequest& request,
    IPaddleCloudDocumentHttpClient& httpClient)
{
    PaddleCloudDocumentSubmitResult result;
    if (request.jobsEndpoint.empty() || request.bearerToken.empty() ||
        request.requestedPageNumbers.empty() || request.requestedPageNumbers.size() > 100) {
        result.error = L"Cloud document submit request is incomplete.";
        return result;
    }
    if (!IsSupportedPaddleCloudDocumentModel(request.model)) {
        result.error = L"PaddleOCR Cloud only supports PaddleOCR-VL-1.6.";
        return result;
    }
    const std::wstring canonicalModel = kPaddleCloudDocumentModel;
    if (!IsOfficialPaddleCloudJobsEndpoint(request.jobsEndpoint, result.error)) {
        return result;
    }

    std::vector<unsigned char> pdfBytes;
    if (!ReadBoundedFile(
            request.sourcePdfPath,
            kMaxNativePdfBytes,
            pdfBytes,
            result.error)) {
        return result;
    }
    if (pdfBytes.size() < 5 || memcmp(pdfBytes.data(), "%PDF-", 5) != 0) {
        result.error = L"Source file does not have a PDF header.";
        return result;
    }
    if (!ComputeSha256Hex(
            pdfBytes.data(),
            pdfBytes.size(),
            result.sourcePdfSha256,
            result.error)) {
        return result;
    }

    const std::wstring canonicalRanges =
        BuildCanonicalCloudPageRanges(request.requestedPageNumbers);
    if (canonicalRanges.empty()) {
        result.error = L"Requested pages cannot be represented canonically.";
        return result;
    }
    const std::wstring optionalPayload = request.optionalPayload.empty()
        ? L"{}"
        : TrimString(request.optionalPayload);
    if (!ValidatePaddleCloudJsonObjectSyntax(optionalPayload, result.error)) {
        result.error = L"Cloud document optionalPayload is not a valid JSON object.";
        return result;
    }
    if (!BuildPaddleCloudRequestFingerprint(
            result.sourcePdfSha256,
            canonicalModel,
            canonicalRanges,
            optionalPayload,
            result.requestFingerprint,
            result.error)) {
        return result;
    }

    std::wstring encodingError;
    const std::string model = WideToUtf8(canonicalModel, encodingError);
    if (!encodingError.empty()) {
        result.error = encodingError;
        return result;
    }
    const std::string optionalPayloadUtf8 = WideToUtf8(optionalPayload, encodingError);
    if (!encodingError.empty()) {
        result.error = encodingError;
        return result;
    }
    const std::string pageRanges = WideToUtf8(canonicalRanges, encodingError);
    if (!encodingError.empty()) {
        result.error = encodingError;
        return result;
    }
    const std::string batchId = WideToUtf8(request.batchId, encodingError);
    if (!encodingError.empty()) {
        result.error = encodingError;
        return result;
    }

    PaddleCloudPdfMultipartRequest multipart;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::string boundary = RandomMultipartBoundary(result.error);
        if (boundary.empty()) return result;
        multipart = BuildPaddleCloudPdfMultipartRequest(
            pdfBytes,
            model,
            optionalPayloadUtf8,
            pageRanges,
            batchId,
            boundary);
        if (multipart.error != L"Multipart boundary collides with request content.") break;
    }
    if (!multipart.error.empty()) {
        result.error = multipart.error;
        return result;
    }

    const std::wstring contentType = Utf8ToWide(multipart.contentType, encodingError);
    if (!encodingError.empty()) {
        result.error = encodingError;
        return result;
    }
    PaddleCloudAsyncSubmitResult submitted = SubmitPaddleCloudAsyncJob(
        request.jobsEndpoint,
        request.bearerToken,
        multipart.body,
        contentType,
        request.timeoutMs,
        httpClient);
    result.envelope = submitted.envelope;
    result.ambiguous = submitted.ambiguous;
    if (!submitted.success) {
        result.error = submitted.error;
        return result;
    }

    result.remoteJob.provider = L"paddleocr_official_api";
    result.remoteJob.model = canonicalModel;
    result.remoteJob.jobId = result.envelope.jobId;
    result.remoteJob.batchId = request.batchId;
    result.remoteJob.state = MapRemoteState(result.envelope.state);
    if (result.remoteJob.state != DocumentOcrTransportState::Pending &&
        result.remoteJob.state != DocumentOcrTransportState::Running) {
        result.remoteJob.state = DocumentOcrTransportState::Pending;
    }
    result.remoteJob.requestedPageNumbers = request.requestedPageNumbers;
    result.remoteJob.pageRanges = canonicalRanges;
    result.remoteJob.requestFingerprint = result.requestFingerprint;
    result.remoteJob.attempt = 1;
    result.success = true;
    return result;
}

PaddleCloudDocumentSubmitResult SubmitPaddleCloudDocumentUrl(
    const PaddleCloudDocumentUrlSubmitRequest& request,
    IPaddleCloudDocumentHttpClient& httpClient)
{
    PaddleCloudDocumentSubmitResult result;
    if (request.fileUrl.empty() || request.jobsEndpoint.empty() ||
        request.bearerToken.empty() || request.requestedPageNumbers.empty() ||
        request.requestedPageNumbers.size() > 100) {
        result.error = L"Cloud fileUrl document submit request is incomplete.";
        return result;
    }
    if (!IsSupportedPaddleCloudDocumentModel(request.model)) {
        result.error = L"PaddleOCR Cloud only supports PaddleOCR-VL-1.6.";
        return result;
    }
    const std::wstring canonicalModel = kPaddleCloudDocumentModel;
    if (!IsOfficialPaddleCloudJobsEndpoint(request.jobsEndpoint, result.error)) return result;

    const std::wstring canonicalRanges =
        BuildCanonicalCloudPageRanges(request.requestedPageNumbers);
    if (canonicalRanges.empty()) {
        result.error = L"Requested pages cannot be represented canonically.";
        return result;
    }
    const std::wstring optionalPayload = request.optionalPayload.empty()
        ? L"{}"
        : TrimString(request.optionalPayload);
    if (!ValidatePaddleCloudJsonObjectSyntax(optionalPayload, result.error)) {
        result.error = L"Cloud fileUrl optionalPayload is not a valid JSON object.";
        return result;
    }
    if (!ComputeUtf8Sha256Hex(request.fileUrl, result.sourcePdfSha256, result.error) ||
        !BuildPaddleCloudRequestFingerprint(
            result.sourcePdfSha256,
            canonicalModel,
            canonicalRanges,
            optionalPayload,
            result.requestFingerprint,
            result.error)) {
        return result;
    }

    PaddleCloudFileUrlJsonRequest jsonRequest = BuildPaddleCloudFileUrlJsonRequest(
        request.fileUrl,
        canonicalModel,
        optionalPayload,
        canonicalRanges,
        request.batchId);
    if (!jsonRequest.error.empty()) {
        result.error = jsonRequest.error;
        return result;
    }
    PaddleCloudAsyncSubmitResult submitted = SubmitPaddleCloudAsyncJob(
        request.jobsEndpoint,
        request.bearerToken,
        jsonRequest.body,
        L"application/json",
        request.timeoutMs,
        httpClient);
    result.envelope = submitted.envelope;
    result.ambiguous = submitted.ambiguous;
    if (!submitted.success) {
        result.error = submitted.error;
        return result;
    }
    result.remoteJob.provider = L"paddleocr_official_api";
    result.remoteJob.model = canonicalModel;
    result.remoteJob.jobId = result.envelope.jobId;
    result.remoteJob.batchId = request.batchId;
    result.remoteJob.state = MapRemoteState(result.envelope.state);
    if (result.remoteJob.state != DocumentOcrTransportState::Pending &&
        result.remoteJob.state != DocumentOcrTransportState::Running) {
        result.remoteJob.state = DocumentOcrTransportState::Pending;
    }
    result.remoteJob.requestedPageNumbers = request.requestedPageNumbers;
    result.remoteJob.pageRanges = canonicalRanges;
    result.remoteJob.requestFingerprint = result.requestFingerprint;
    result.remoteJob.attempt = 1;
    result.success = true;
    return result;
}

PaddleCloudDocumentPollResult PollPaddleCloudDocument(
    const std::wstring& jobsEndpoint,
    const std::wstring& bearerToken,
    const std::wstring& jobId,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient)
{
    PaddleCloudDocumentPollResult result;
    if (jobsEndpoint.empty() || bearerToken.empty() || !SafeJobId(jobId)) {
        result.error = L"Cloud document poll request is incomplete or has an unsafe jobId.";
        return result;
    }
    if (!IsOfficialPaddleCloudJobsEndpoint(jobsEndpoint, result.error)) {
        return result;
    }
    std::wstring authorization;
    if (!BuildPaddleCloudAuthorizationHeader(bearerToken, authorization, result.error)) {
        return result;
    }
    const std::vector<std::wstring> headers = {
        authorization,
        L"Content-Type: application/json"
    };
    const std::wstring pollUrl = BuildJobUrl(NormalizePaddleOcrJobsUrl(jobsEndpoint), jobId);
    HttpResponse response = httpClient.Get(
        pollUrl,
        headers,
        timeoutMs);
    if (!response.error.empty()) {
        // Retrying GET for an already-known job is idempotent and must never
        // create a replacement Cloud job.
        result.retrySameJob = true;
        result.error = RedactDocumentOcrSensitiveText(
            L"Cloud document polling transport failed: " + response.error);
        return result;
    }
    if (!response.finalUrl.empty() && !SameUrlTarget(response.finalUrl, pollUrl)) {
        result.error = L"Cloud document poll unexpectedly redirected.";
        return result;
    }
    if (!IsJsonResponseContentType(response.contentType, false)) {
        result.error = L"Cloud document poll returned a non-JSON content type.";
        return result;
    }
    PaddleCloudApiEnvelope envelope = ParsePaddleCloudApiEnvelope(
        response.statusCode,
        response.body,
        false,
        true);
    result.retrySameJob = envelope.retrySameJob;
    result.diagnosticCode = envelope.diagnosticCode;
    if (!envelope.success) {
        result.error = RedactDocumentOcrSensitiveText(envelope.message);
        return result;
    }

    result.state = MapRemoteState(envelope.state);
    result.jsonUrl = envelope.jsonUrl;
    result.markdownUrl = envelope.markdownUrl;
    result.extractedPages = ExtractProgressInt(response.body, L"extractedPages");
    result.totalPages = ExtractProgressInt(response.body, L"totalPages");
    result.terminal = result.state == DocumentOcrTransportState::Downloading ||
        result.state == DocumentOcrTransportState::Failed ||
        result.state == DocumentOcrTransportState::Expired;
    if (result.state == DocumentOcrTransportState::Unknown) {
        result.error = L"Cloud document poll returned an unknown state.";
        return result;
    }
    if (result.state == DocumentOcrTransportState::Downloading && result.jsonUrl.empty()) {
        result.error = L"Completed Cloud document job has no jsonUrl.";
        return result;
    }
    if (result.state == DocumentOcrTransportState::Failed ||
        result.state == DocumentOcrTransportState::Expired) {
        result.diagnosticCode = result.state == DocumentOcrTransportState::Failed
            ? L"job_failed"
            : L"job_expired";
        result.error = envelope.message.empty()
            ? (result.state == DocumentOcrTransportState::Failed
                ? L"Cloud document job failed."
                : L"Cloud document job expired.")
            : envelope.message;
    }
    result.success = true;
    return result;
}

PaddleCloudDocumentBatchQueryResult QueryPaddleCloudDocumentBatch(
    const std::wstring& jobsEndpoint,
    const std::wstring& bearerToken,
    const std::wstring& batchId,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient)
{
    PaddleCloudDocumentBatchQueryResult result;
    if (!SafeJobId(batchId)) {
        result.error = L"Cloud document batch query has an unsafe batchId.";
        return result;
    }
    if (!IsOfficialPaddleCloudJobsEndpoint(jobsEndpoint, result.error)) return result;
    std::wstring authorization;
    if (!BuildPaddleCloudAuthorizationHeader(bearerToken, authorization, result.error)) {
        return result;
    }

    std::wstring endpoint = NormalizePaddleOcrJobsUrl(jobsEndpoint);
    while (!endpoint.empty() && endpoint.back() == L'/') endpoint.pop_back();
    const std::vector<std::wstring> headers = {
        authorization,
        L"Content-Type: application/json"
    };
    const std::wstring batchUrl = endpoint + L"/batch/" + batchId;
    HttpResponse response = httpClient.Get(
        batchUrl,
        headers,
        timeoutMs);
    if (!response.error.empty()) {
        result.error = RedactDocumentOcrSensitiveText(
            L"Cloud document batch query transport failed: " + response.error);
        return result;
    }
    if (!response.finalUrl.empty() && !SameUrlTarget(response.finalUrl, batchUrl)) {
        result.error = L"Cloud document batch query unexpectedly redirected.";
        return result;
    }
    if (!IsJsonResponseContentType(response.contentType, false)) {
        result.error = L"Cloud document batch query returned a non-JSON content type.";
        return result;
    }
    PaddleCloudApiEnvelope envelope = ParsePaddleCloudApiEnvelope(
        response.statusCode,
        response.body,
        false,
        false);
    result.diagnosticCode = envelope.diagnosticCode;
    if (!envelope.success) {
        result.error = envelope.message;
        return result;
    }

    std::wstring decodeError;
    const std::wstring json = Utf8ToWide(response.body, decodeError);
    if (!decodeError.empty()) {
        result.error = decodeError;
        return result;
    }
    std::wstring jobsArray = OcrBlockJsonExtractValue(json, L"jobs");
    const auto jobObjects = OcrBlockJsonObjectArrayItems(jobsArray);
    if (jobObjects.empty()) {
        result.error = L"Cloud batch query returned no jobs collection.";
        return result;
    }
    for (const auto& object : jobObjects) {
        DocumentOcrRemoteJob job;
        job.provider = L"paddleocr_official_api";
        job.jobId = OcrBlockJsonText(object, L"jobId");
        job.batchId = batchId;
        job.model = OcrBlockJsonText(object, L"model");
        job.state = MapRemoteState(OcrBlockJsonText(object, L"state"));
        if (!SafeJobId(job.jobId) || job.state == DocumentOcrTransportState::Unknown) {
            result.jobs.clear();
            result.error = L"Cloud batch query contains an invalid job identity or state.";
            return result;
        }
        result.jobs.push_back(std::move(job));
    }
    result.success = true;
    return result;
}

PaddleCloudDocumentDownloadResult DownloadAndNormalizePaddleCloudDocument(
    const std::wstring& jsonUrl,
    const std::vector<int>& requestedPageNumbers,
    const PaddleCloudDocumentNormalizeOptions& options,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient)
{
    PaddleCloudDocumentDownloadResult result;
    if (!IsSafePaddleCloudResourceUrl(jsonUrl, result.error)) return result;

    // Signed resource requests deliberately receive no Authorization header.
    HttpResponse response = httpClient.Get(jsonUrl, {}, timeoutMs);
    if (!response.error.empty()) {
        result.error = L"Cloud document JSONL download failed: " + response.error;
        return result;
    }
    if (!response.finalUrl.empty() && !SameUrlTarget(response.finalUrl, jsonUrl)) {
        result.error = L"Cloud document JSONL download unexpectedly redirected.";
        return result;
    }
    if (response.statusCode != 200) {
        // OWN-126: pure int-dot error suffix (WideStringUtils).
        result.error = WideFormatIntDotSuffix(
            L"Cloud document JSONL download HTTP ",
            response.statusCode);
        return result;
    }
    if (!IsJsonResponseContentType(response.contentType, true)) {
        result.error = L"Cloud document JSONL response has an unsupported content type.";
        return result;
    }
    if (response.body.empty() || response.body.size() > kMaxDocumentJsonlBytes) {
        result.error = L"Cloud document JSONL is empty or exceeds the 128 MiB limit.";
        return result;
    }
    std::wstring decodeError;
    std::wstring jsonl = Utf8ToWide(response.body, decodeError);
    if (!decodeError.empty()) {
        result.error = decodeError;
        return result;
    }
    if (!NormalizePaddleCloudDocumentJsonl(
            jsonl,
            requestedPageNumbers,
            options,
            result.document)) {
        result.error = result.document.error;
        return result;
    }
    result.success = true;
    return result;
}

bool IsSafePaddleCloudResourceUrl(const std::wstring& url, std::wstring& error) {
    error.clear();
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = 1;
    components.dwHostNameLength = 1;
    components.dwUserNameLength = 1;
    components.dwPasswordLength = 1;
    components.dwUrlPathLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = L"Resource URL is invalid.";
        return false;
    }
    if (components.nScheme != INTERNET_SCHEME_HTTPS ||
        components.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        components.dwHostNameLength == 0 ||
        components.dwUserNameLength > 0 ||
        components.dwPasswordLength > 0) {
        error = L"Resource URL must be credential-free HTTPS on port 443.";
        return false;
    }
    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    if (!IsStructurallyPublicHost(host)) {
        error = L"Resource URL host is local, private, non-public, or malformed.";
        return false;
    }
    return true;
}

bool IsOfficialPaddleCloudJobsEndpoint(const std::wstring& url, std::wstring& error) {
    error.clear();
    if (TrimString(url).empty()) {
        error = L"Cloud jobs endpoint is empty.";
        return false;
    }
    const std::wstring normalized = NormalizePaddleOcrJobsUrl(url);
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = 1;
    components.dwHostNameLength = 1;
    components.dwUserNameLength = 1;
    components.dwPasswordLength = 1;
    components.dwUrlPathLength = 1;
    components.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(
            normalized.c_str(),
            static_cast<DWORD>(normalized.size()),
            0,
            &components)) {
        error = L"Cloud jobs endpoint is invalid.";
        return false;
    }
    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.nScheme != INTERNET_SCHEME_HTTPS ||
        components.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        !WideEqualsNoCase(host, L"paddleocr.aistudio-app.com") ||
        !WideEqualsNoCase(path, L"/api/v2/ocr/jobs") ||
        components.dwUserNameLength > 0 ||
        components.dwPasswordLength > 0 ||
        components.dwExtraInfoLength > 0) {
        error = L"Authorization is restricted to the official PaddleOCR HTTPS jobs endpoint.";
        return false;
    }
    return true;
}
