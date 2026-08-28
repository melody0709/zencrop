#pragma once

#include "OcrDocumentTypes.h"
#include "PaddleCloudDocumentNormalizer.h"
#include "PaddleCloudDocumentProtocol.h"
// Stage3 3-A-2: document↛batch — remote job type sole in DocumentOcrTypes.
#include "ocr/PaddleCloudHttpClient.h"

#include <string>
#include <vector>

struct PaddleCloudDocumentSubmitRequest {
    std::wstring sourcePdfPath;
    std::wstring jobsEndpoint;
    std::wstring bearerToken;
    std::wstring model = L"PaddleOCR-VL-1.6";
    std::wstring optionalPayload =
        L"{\"useDocOrientationClassify\":false,\"useDocUnwarping\":false}";
    std::vector<int> requestedPageNumbers;
    std::wstring batchId;
    int timeoutMs = 120000;
};

struct PaddleCloudDocumentUrlSubmitRequest {
    std::wstring fileUrl;
    std::wstring jobsEndpoint;
    std::wstring bearerToken;
    std::wstring model = L"PaddleOCR-VL-1.6";
    std::wstring optionalPayload =
        L"{\"useDocOrientationClassify\":false,\"useDocUnwarping\":false}";
    std::vector<int> requestedPageNumbers;
    std::wstring batchId;
    int timeoutMs = 120000;
};

struct PaddleCloudDocumentSubmitResult {
    bool success = false;
    bool ambiguous = false;
    std::wstring sourcePdfSha256;
    std::wstring requestFingerprint;
    DocumentOcrRemoteJob remoteJob;
    PaddleCloudApiEnvelope envelope;
    std::wstring error;
};

struct PaddleCloudAsyncSubmitResult {
    bool success = false;
    bool ambiguous = false;
    PaddleCloudApiEnvelope envelope;
    std::wstring error;
};

struct PaddleCloudDocumentPollResult {
    bool success = false;
    bool terminal = false;
    bool retrySameJob = false;
    DocumentOcrTransportState state = DocumentOcrTransportState::Unknown;
    std::wstring jsonUrl;
    std::wstring markdownUrl;
    int extractedPages = 0;
    int totalPages = 0;
    std::wstring diagnosticCode;
    std::wstring error;
};

struct PaddleCloudDocumentDownloadResult {
    bool success = false;
    DocumentOcrResult document;
    std::wstring error;
};

struct PaddleCloudDocumentBatchQueryResult {
    bool success = false;
    std::vector<DocumentOcrRemoteJob> jobs;
    std::wstring diagnosticCode;
    std::wstring error;
};

PaddleCloudDocumentSubmitResult SubmitPaddleCloudDocument(
    const PaddleCloudDocumentSubmitRequest& request,
    IPaddleCloudDocumentHttpClient& httpClient);

PaddleCloudAsyncSubmitResult SubmitPaddleCloudAsyncJob(
    const std::wstring& jobsEndpoint,
    const std::wstring& bearerToken,
    const std::string& body,
    const std::wstring& contentType,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient);

PaddleCloudDocumentSubmitResult SubmitPaddleCloudDocumentUrl(
    const PaddleCloudDocumentUrlSubmitRequest& request,
    IPaddleCloudDocumentHttpClient& httpClient);

PaddleCloudDocumentPollResult PollPaddleCloudDocument(
    const std::wstring& jobsEndpoint,
    const std::wstring& bearerToken,
    const std::wstring& jobId,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient);

PaddleCloudDocumentBatchQueryResult QueryPaddleCloudDocumentBatch(
    const std::wstring& jobsEndpoint,
    const std::wstring& bearerToken,
    const std::wstring& batchId,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient);

PaddleCloudDocumentDownloadResult DownloadAndNormalizePaddleCloudDocument(
    const std::wstring& jsonUrl,
    const std::vector<int>& requestedPageNumbers,
    const PaddleCloudDocumentNormalizeOptions& options,
    int timeoutMs,
    IPaddleCloudDocumentHttpClient& httpClient);

bool IsOfficialPaddleCloudJobsEndpoint(const std::wstring& url, std::wstring& error);
