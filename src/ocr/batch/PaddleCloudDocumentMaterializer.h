#pragma once

// Stage3 3-A-7: materializer consumes neutral cloud-resource port only.
#include "ocr/PaddleCloudHttpClient.h"
#include "BatchOcrTypes.h"

#include <cstdint>
#include <string>

struct PaddleCloudDocumentMaterializeResult {
    bool success = false;
    int completedPages = 0;
    int textOnlyWarningPages = 0;
    std::wstring warning;
    std::wstring error;
};

PaddleCloudDocumentMaterializeResult MaterializePaddleCloudDocument(
    BatchOcrPdfJob& job,
    const DocumentOcrResult& document,
    IPaddleCloudDocumentHttpClient& httpClient,
    int resourceTimeoutMs = 30000,
    uint64_t maxPageImageBytes = 64ull * 1024ull * 1024ull,
    uint32_t documentElapsedMs = 0);
