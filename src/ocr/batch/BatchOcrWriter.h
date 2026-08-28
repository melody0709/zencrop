#pragma once

#include "BatchOcrTypes.h"
#include "OcrUtils.h"
#include <string>

struct BatchOcrAssetTransaction;

class BatchOcrWriter {
public:
    static bool EnsureDirectory(const std::wstring& dir);
    static BatchOcrWriteResult WriteLayoutArtifacts(
        const std::wstring& sourceImagePath,
        const std::wstring& outputBasePath,
        const std::vector<OcrLayoutBlock>& blocks);

    static BatchOcrWriteResult WriteImagePending(const BatchOcrImageJob& job);
    static BatchOcrWriteResult WriteImageSuccess(
        const BatchOcrImageJob& job,
        const std::wstring& cachedSourceImagePath,
        const std::wstring& markdown,
        const std::wstring& plainText,
        const std::wstring& engineMode,
        DWORD elapsedMs,
        const std::vector<OcrLayoutBlock>& blocks = {},
        const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets = {});
    static BatchOcrWriteResult WriteImageFailure(
        const BatchOcrImageJob& job,
        const std::wstring& cachedSourceImagePath,
        const std::wstring& engineMode,
        const std::wstring& error,
        DWORD elapsedMs);
    static BatchOcrWriteResult WriteImageCanceled(
        const BatchOcrImageJob& job,
        const std::wstring& cachedSourceImagePath,
        const std::wstring& engineMode,
        const std::wstring& reason,
        DWORD elapsedMs);

    static BatchOcrWriteResult WritePdfPending(const BatchOcrPdfJob& job);
    static BatchOcrWriteResult WritePdfManifestState(BatchOcrPdfJob& job);
    static BatchOcrWriteResult WritePdfPageSuccess(
        BatchOcrPdfJob& job,
        int pageIndex,
        const std::wstring& markdown,
        const std::wstring& plainText,
        const std::wstring& engineMode,
        DWORD elapsedMs,
        const std::vector<OcrLayoutBlock>& blocks = {},
        const std::wstring& rawOcrJson = L"",
        const std::wstring& debugOutputImagesJson = L"",
        const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets = {},
        BatchOcrAssetTransaction* preMaterializedAssetTransaction = nullptr);
    static BatchOcrWriteResult WritePdfPageFailure(
        BatchOcrPdfJob& job,
        int pageIndex,
        const std::wstring& engineMode,
        const std::wstring& error,
        DWORD elapsedMs);
    static BatchOcrWriteResult WritePdfPageCanceled(
        BatchOcrPdfJob& job,
        int pageIndex,
        const std::wstring& engineMode,
        const std::wstring& reason,
        DWORD elapsedMs);
    static BatchOcrWriteResult FinalizePdfJob(BatchOcrPdfJob& job);
};
