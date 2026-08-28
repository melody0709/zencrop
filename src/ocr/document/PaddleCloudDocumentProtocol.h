#pragma once

#include "OcrDocumentTypes.h"

#include <cstdint>
#include <string>
#include <vector>

struct NativePdfEligibilityInput {
    bool featureFlagEnabled = false;
    bool gateProfileVerified = false;
    bool fullPdfConsentGranted = false;
    bool providerHealthy = true;
    bool encrypted = false;
    bool requiresPassword = false;
    bool localOnly = false;
    bool allPagesSelected = false;
    bool allowPartialPageRanges = false;
    uint64_t sourceBytes = 0;
    int sourcePageCount = 0;
    std::vector<int> requestedPages;
    std::wstring engineMode;
    std::wstring model;
};

struct NativePdfEligibilityDecision {
    bool eligible = false;
    std::wstring transportKind = L"raster_pages";
    std::wstring canonicalPageRanges;
    std::wstring reason;
};

NativePdfEligibilityDecision EvaluatePaddleCloudNativePdfEligibility(
    const NativePdfEligibilityInput& input);

struct PaddleCloudPdfMultipartRequest {
    std::string boundary;
    std::string contentType;
    std::string body;
    std::wstring error;
};

struct PaddleCloudFileUrlJsonRequest {
    std::string contentType = "application/json";
    std::string body;
    std::wstring error;
};

PaddleCloudPdfMultipartRequest BuildPaddleCloudPdfMultipartRequest(
    const std::vector<unsigned char>& pdfBytes,
    const std::string& model,
    const std::string& optionalPayload,
    const std::string& pageRanges,
    const std::string& batchId,
    const std::string& boundary);

PaddleCloudFileUrlJsonRequest BuildPaddleCloudFileUrlJsonRequest(
    const std::wstring& fileUrl,
    const std::wstring& model,
    const std::wstring& optionalPayload,
    const std::wstring& pageRanges,
    const std::wstring& batchId);

bool BuildPaddleCloudAuthorizationHeader(
    const std::wstring& bearerToken,
    std::wstring& header,
    std::wstring& error);

struct PaddleCloudApiEnvelope {
    int httpStatus = 0;
    int code = 0;
    bool codePresent = false;
    bool success = false;
    bool submitAccepted = false;
    bool retrySameJob = false;
    bool reconcileBeforeReplay = false;
    std::wstring jobId;
    std::wstring state;
    std::wstring jsonUrl;
    std::wstring markdownUrl;
    std::wstring message;
    std::wstring diagnosticCode;
};

PaddleCloudApiEnvelope ParsePaddleCloudApiEnvelope(
    int httpStatus,
    const std::string& responseBody,
    bool submitPhase,
    bool alreadyHasJobId);

bool BuildPaddleCloudRequestFingerprint(
    const std::wstring& sourcePdfSha256,
    const std::wstring& model,
    const std::wstring& canonicalPageRanges,
    const std::wstring& optionalPayload,
    std::wstring& fingerprint,
    std::wstring& error);
