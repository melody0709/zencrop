#include "TranslationResultWindow.h"

#include "ocr/ui/OcrMarkdownPreviewHost.h"
#include "selection/SelectionStructuredContent.h"

#include <utility>

namespace translation {
namespace {

constexpr UINT kStructuredConversionTimeoutMs = 5000;
constexpr UINT kPreviewSelectionTimeoutMs = 2500;

} // namespace

void TranslationResultWindow::UpdatePreviewSelectionState(
    PreviewSelectionHost host, bool hasSelection, uint64_t generation) {
    if (host == PreviewSelectionHost::Source) {
        sourcePreviewSelectionGeneration_ = hasSelection ? generation : 0;
        sourcePreviewSelectionTick_ = hasSelection ? GetTickCount64() : 0;
    } else if (host == PreviewSelectionHost::Translation) {
        translationPreviewSelectionGeneration_ = hasSelection ? generation : 0;
        translationPreviewSelectionTick_ = hasSelection ? GetTickCount64() : 0;
    }
    if (hasSelection) {
        recentPreviewSelectionHost_ = host;
    } else if (recentPreviewSelectionHost_ == host) {
        const bool sourceAvailable = sourcePreviewSelectionGeneration_ != 0;
        const bool translationAvailable =
            translationPreviewSelectionGeneration_ != 0;
        recentPreviewSelectionHost_ = sourceAvailable && translationAvailable
            ? (sourcePreviewSelectionTick_ >= translationPreviewSelectionTick_
                ? PreviewSelectionHost::Source
                : PreviewSelectionHost::Translation)
            : (sourceAvailable ? PreviewSelectionHost::Source
                : (translationAvailable ? PreviewSelectionHost::Translation
                    : PreviewSelectionHost::None));
    }
}

void TranslationResultWindow::HandleStructuredSelectionPrepared(
    PreviewSelectionHost host,
    const std::wstring& token, uint64_t generation, bool success,
    const std::wstring& planJson, const std::wstring& errorCode) {
    if (host != pendingStructuredSelectionHost_ ||
        token != pendingStructuredSelectionToken_ ||
        generation != pendingStructuredSelectionGeneration_) {
        return;
    }
    KillTimer(window_, kStructuredSelectionTimer);
    pendingStructuredSelectionHost_ = PreviewSelectionHost::None;
    pendingStructuredSelectionToken_.clear();
    pendingStructuredSelectionGeneration_ = 0;
    auto callback = std::move(pendingStructuredSelectionCallback_);
    pendingStructuredSelectionCallback_ = {};
    if (callback) callback(token, generation, success, planJson, errorCode);
}

void TranslationResultWindow::CancelPendingStructuredSelection(
    const std::wstring& errorCode) {
    if (pendingStructuredSelectionHost_ == PreviewSelectionHost::None) return;
    OcrMarkdownPreviewHost* host =
        pendingStructuredSelectionHost_ == PreviewSelectionHost::Source
            ? sourcePreview_.get() : translationPreview_.get();
    if (host) {
        host->CancelStructuredSelection(
            pendingStructuredSelectionToken_,
            pendingStructuredSelectionGeneration_, errorCode);
        if (pendingStructuredSelectionHost_ == PreviewSelectionHost::None) return;
    }
    HandleStructuredSelectionPrepared(
        pendingStructuredSelectionHost_, pendingStructuredSelectionToken_,
        pendingStructuredSelectionGeneration_, false, L"", errorCode);
}

bool TranslationResultWindow::PrepareStructuredSelection(
    const StructuredSelectionInput& input,
    StructuredSelectionCallback callback) {
    if (!callback) {
        return false;
    }
    if (!sourcePreview_ || input.token.empty() || input.generation == 0 ||
        input.payload.empty()) {
        callback(input.token, input.generation, false, L"",
            L"preview_unavailable");
        return false;
    }
    CancelPendingStructuredSelection(L"superseded");
    pendingStructuredSelectionHost_ = PreviewSelectionHost::Source;
    pendingStructuredSelectionToken_ = input.token;
    pendingStructuredSelectionGeneration_ = input.generation;
    pendingStructuredSelectionCallback_ = std::move(callback);
    OcrMarkdownPreviewHost::StructuredSelectionRequest request;
    request.token = input.token;
    request.generation = input.generation;
    request.format = input.format;
    request.payload = input.payload;
    request.sourceUrl = input.sourceUrl;
    if (!sourcePreview_->PrepareStructuredSelection(request)) {
        HandleStructuredSelectionPrepared(
            PreviewSelectionHost::Source, input.token, input.generation,
            false, L"", L"preview_unavailable");
        return false;
    }
    if (pendingStructuredSelectionHost_ != PreviewSelectionHost::Source ||
        pendingStructuredSelectionToken_ != input.token ||
        pendingStructuredSelectionGeneration_ != input.generation) {
        return true;
    }
    if (!SetTimer(window_, kStructuredSelectionTimer,
            kStructuredConversionTimeoutMs, nullptr)) {
        CancelPendingStructuredSelection(L"timer_unavailable");
        return false;
    }
    return true;
}

bool TranslationResultWindow::RequestPreviewSelection(
    uint64_t generation,
    StructuredSelectionCallback callback) {
    if (!callback || generation == 0 ||
        recentPreviewSelectionHost_ == PreviewSelectionHost::None) {
        return false;
    }
    OcrMarkdownPreviewHost* host =
        recentPreviewSelectionHost_ == PreviewSelectionHost::Source
            ? sourcePreview_.get() : translationPreview_.get();
    const uint64_t selectionGeneration =
        recentPreviewSelectionHost_ == PreviewSelectionHost::Source
            ? sourcePreviewSelectionGeneration_
            : translationPreviewSelectionGeneration_;
    if (!host || !host->IsReady() || host->HasActiveEditor() ||
        selectionGeneration == 0) {
        return false;
    }

    CancelPendingStructuredSelection(L"superseded");
    const std::wstring token = selection::MakeSelectionRequestToken();
    pendingStructuredSelectionHost_ = recentPreviewSelectionHost_;
    pendingStructuredSelectionToken_ = token;
    pendingStructuredSelectionGeneration_ = generation;
    pendingStructuredSelectionCallback_ = std::move(callback);
    OcrMarkdownPreviewHost::StructuredSelectionRequest request;
    request.token = token;
    request.generation = generation;
    request.selectionGeneration = selectionGeneration;
    request.previewSelection = true;
    if (!host->PrepareStructuredSelection(request)) {
        // 同步失败不回调：调用方约定是"返回 false 时由外层统一启动普通
        // 采集"。此处若回调，外层会再启动一次，形成同 generation 的双重
        // 采集。仅清空 pending 状态（对齐 Dashboard 的失败路径）。
        pendingStructuredSelectionHost_ = PreviewSelectionHost::None;
        pendingStructuredSelectionToken_.clear();
        pendingStructuredSelectionGeneration_ = 0;
        pendingStructuredSelectionCallback_ = {};
        return false;
    }
    if (pendingStructuredSelectionToken_ != token ||
        pendingStructuredSelectionGeneration_ != generation) {
        return true;
    }
    if (!SetTimer(window_, kStructuredSelectionTimer,
            kPreviewSelectionTimeoutMs, nullptr)) {
        // 同上：timer 创建失败属同步失败，不回调，由外层统一回退。
        // 仍需取消 webview 侧挂起的请求；取消会触发一次
        // HandleStructuredSelectionPrepared，但 pending 已清空而被拒收。
        pendingStructuredSelectionCallback_ = {};
        pendingStructuredSelectionHost_ = PreviewSelectionHost::None;
        pendingStructuredSelectionToken_.clear();
        pendingStructuredSelectionGeneration_ = 0;
        host->CancelStructuredSelection(
            token, generation, L"timer_unavailable");
        return false;
    }
    return true;
}

} // namespace translation
