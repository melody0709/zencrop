#pragma once

#include "BatchOcrTypes.h"
#include <string>
#include <vector>

struct BatchOcrManifestScanResult {
    std::vector<BatchOcrImageJob> jobs;
    std::vector<BatchOcrPdfJob> pdfJobs;
    int manifestCount = 0;
    int retryableCount = 0;
    int completedCount = 0;
    int missingSourceCount = 0;
    int pdfPageCount = 0;
    int pdfRetryablePageCount = 0;
    int pdfMissingPageImageCount = 0;
    int invalidCount = 0;
    int skippedCount = 0;
};

class BatchOcrManifestStore {
public:
    static bool LoadImageJob(
        const std::wstring& manifestPath,
        const std::wstring& selectedOutputRoot,
        BatchOcrImageJob& job,
        std::wstring& error);

    static bool LoadPdfJob(
        const std::wstring& manifestPath,
        const std::wstring& selectedOutputRoot,
        BatchOcrPdfJob& job,
        std::wstring& error);

    static bool ScanJobs(
        const std::wstring& outputRoot,
        BatchOcrManifestScanResult& result,
        std::wstring& error);

    static bool ScanImageJobs(
        const std::wstring& outputRoot,
        BatchOcrManifestScanResult& result,
        std::wstring& error);

    static bool IsRetryableStatus(BatchOcrTaskStatus status);
};
