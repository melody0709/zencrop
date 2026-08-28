#pragma once

// D-I-4: shared Host result/async types for multi-TU OcrDashboardWindow.

#include "OcrDashboardWindow.h"
#include "PdfPageRenderer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

struct OcrBackgroundResult {
    bool success = false;
    std::wstring text;
    std::wstring imagePath;
    std::wstring error;
    std::vector<RECT> bboxes;
    std::vector<std::wstring> bboxClasses;
    std::vector<OcrLayoutBlock> blocks;
    std::vector<OcrEmbeddedAssetSpec> embeddedAssets;
    std::vector<std::wstring> transientOwnedFiles;
    std::wstring rawOcrJson;
    std::wstring debugOutputImagesJson;
    DWORD elapsedMs = 0;
    uint64_t generation = 0;
    bool hasImageTask = false;
    BatchOcrImageJob imageTaskJob;
    bool hasBatchJob = false;
    BatchOcrImageJob batchJob;
    bool hasPdfPageJob = false;
    BatchOcrPdfJob pdfJob;
    BatchOcrPdfPageJob pdfPage;
    std::wstring engineMode;
    std::wstring sourcePath;
};

struct OcrRunParams {
    HWND hwndNotify = nullptr;
    std::wstring filePath;
    std::wstring sourcePath;
    std::wstring engineMode;
    HBITMAP hBitmap = nullptr;
    uint64_t generation = 0;
    bool hasImageTask = false;
    BatchOcrImageJob imageTaskJob;
    bool hasBatchJob = false;
    BatchOcrImageJob batchJob;
    bool hasPdfPageJob = false;
    BatchOcrPdfJob pdfJob;
    BatchOcrPdfPageJob pdfPage;
};

struct DashboardPdfRenderResult {
    uint64_t generation = 0;
    BatchOcrPdfJob pdfJob;
    PdfRenderResult render;
};

struct DashboardPdfCoverResult {
    uint64_t generation = 0;
    std::wstring jobKey;
    std::wstring manifestPath;
    std::wstring sourcePath;
    std::wstring outputDir;
    std::wstring candidatePath;
    PdfCoverRenderResult render;
};

struct DashboardCloudNativePdfResult {
    uint64_t generation = 0;
    BatchOcrPdfJob pdfJob;
    DWORD elapsedMs = 0;
    bool success = false;
    bool ambiguousSubmit = false;
    bool canRasterFallback = false;
    std::wstring error;
};

inline bool DashboardPostAsyncMessage(
    const std::shared_ptr<DashboardAsyncDispatchState>& state,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (!state) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->accepting && state->hwnd && IsWindow(state->hwnd) &&
        PostMessageW(state->hwnd, message, wParam, lParam) != FALSE;
}

// Keep page-1 cover work ahead of the monolithic page render on the same
// bounded worker. The dispatch step is intentionally part of the ordering.
template <typename CoverWork, typename CoverDispatch, typename RenderWork, typename RenderDispatch>
inline void DashboardRunPdfRenderStages(
    CoverWork&& coverWork,
    CoverDispatch&& coverDispatch,
    RenderWork&& renderWork,
    RenderDispatch&& renderDispatch)
{
    auto coverResult = std::forward<CoverWork>(coverWork)();
    std::forward<CoverDispatch>(coverDispatch)(std::move(coverResult));
    auto renderResult = std::forward<RenderWork>(renderWork)();
    std::forward<RenderDispatch>(renderDispatch)(std::move(renderResult));
}
