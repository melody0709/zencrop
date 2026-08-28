#pragma once

#include "BatchOcrTypes.h"
#include <string>
#include <vector>

class BatchOcrController {
public:
    bool CreateImageJobs(
        const std::vector<std::wstring>& imageFiles,
        const std::wstring& outputRoot,
        std::vector<BatchOcrImageJob>& jobs,
        std::wstring& error,
        const std::wstring& engineMode = L"",
        const OcrOutputArtifactOptions* outputArtifacts = nullptr) const;

    bool CreatePdfJob(
        const std::wstring& pdfFile,
        const std::wstring& outputRoot,
        BatchOcrPdfJob& job,
        std::wstring& error,
        const std::wstring& engineMode = L"",
        const OcrOutputArtifactOptions* outputArtifacts = nullptr) const;

    bool InitializePdfPages(
        BatchOcrPdfJob& job,
        int pageCount,
        std::wstring& error) const;

    bool InitializePdfPages(
        BatchOcrPdfJob& job,
        const std::vector<int>& pageIndices,
        std::wstring& error) const;
};
