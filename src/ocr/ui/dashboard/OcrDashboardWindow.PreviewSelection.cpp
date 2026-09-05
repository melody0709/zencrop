#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"

#include "ocr/ui/OcrMarkdownPreviewHost.h"
#include "selection/SelectionStructuredContent.h"

#include <utility>

namespace {

constexpr UINT kPreviewSelectionTimeoutMs = 2500;

} // namespace

void OcrDashboardWindow::UpdatePreviewSelectionState(
    PreviewSelectionHost host, bool hasSelection, uint64_t generation)
{
    const bool available = hasSelection && generation != 0;
    if (host == PreviewSelectionHost::Source) {
        m_sourcePreviewSelectionGeneration = available ? generation : 0;
        m_sourcePreviewSelectionTick = available ? GetTickCount64() : 0;
    } else if (host == PreviewSelectionHost::Translation) {
        m_translationPreviewSelectionGeneration = available ? generation : 0;
        m_translationPreviewSelectionTick = available ? GetTickCount64() : 0;
    }
    if (available) {
        m_recentPreviewSelectionHost = host;
        m_recentPreviewSelectionGeneration = generation;
    } else if (m_recentPreviewSelectionHost == host) {
        const bool sourceAvailable =
            m_sourcePreviewSelectionGeneration != 0;
        const bool translationAvailable =
            m_translationPreviewSelectionGeneration != 0;
        if (sourceAvailable && translationAvailable) {
            m_recentPreviewSelectionHost =
                m_sourcePreviewSelectionTick >=
                        m_translationPreviewSelectionTick
                    ? PreviewSelectionHost::Source
                    : PreviewSelectionHost::Translation;
        } else if (sourceAvailable) {
            m_recentPreviewSelectionHost = PreviewSelectionHost::Source;
        } else if (translationAvailable) {
            m_recentPreviewSelectionHost = PreviewSelectionHost::Translation;
        } else {
            m_recentPreviewSelectionHost = PreviewSelectionHost::None;
        }
        m_recentPreviewSelectionGeneration =
            m_recentPreviewSelectionHost == PreviewSelectionHost::Source
                ? m_sourcePreviewSelectionGeneration
                : (m_recentPreviewSelectionHost ==
                           PreviewSelectionHost::Translation
                       ? m_translationPreviewSelectionGeneration
                       : 0);
    }
}

bool OcrDashboardWindow::RequestPreviewSelectionInternal(
    uint64_t requestGeneration,
    std::function<void(selection::SelectionContent)> callback)
{
    if (!callback || requestGeneration == 0 ||
        m_recentPreviewSelectionHost == PreviewSelectionHost::None ||
        m_recentPreviewSelectionGeneration == 0) {
        return false;
    }
    OcrMarkdownPreviewHost* host =
        m_recentPreviewSelectionHost == PreviewSelectionHost::Source
        ? m_previewHost.get() : m_translationPreviewHost.get();
    if (!host || !host->IsAvailable() || host->HasActiveEditor()) return false;

    CancelPendingPreviewSelection(L"superseded");
    OcrMarkdownPreviewHost::StructuredSelectionRequest request;
    request.token = selection::MakeSelectionRequestToken();
    request.generation = requestGeneration;
    request.selectionGeneration = m_recentPreviewSelectionGeneration;
    request.previewSelection = true;
    m_pendingPreviewSelectionHost = m_recentPreviewSelectionHost;
    m_pendingPreviewSelectionToken = request.token;
    m_pendingPreviewSelectionGeneration = requestGeneration;
    m_pendingPreviewSelectionCallback = std::move(callback);
    if (!host->PrepareStructuredSelection(request)) {
        m_pendingPreviewSelectionHost = PreviewSelectionHost::None;
        m_pendingPreviewSelectionToken.clear();
        m_pendingPreviewSelectionGeneration = 0;
        m_pendingPreviewSelectionCallback = {};
        return false;
    }
    if (!m_pendingPreviewSelectionCallback ||
        m_pendingPreviewSelectionToken != request.token ||
        m_pendingPreviewSelectionGeneration != request.generation) {
        return true;
    }
    m_pendingPreviewSelectionTimer = SetTimer(
        m_hwnd, 0, kPreviewSelectionTimeoutMs, nullptr);
    if (!m_pendingPreviewSelectionTimer) {
        m_pendingPreviewSelectionCallback = {};
        m_pendingPreviewSelectionHost = PreviewSelectionHost::None;
        m_pendingPreviewSelectionToken.clear();
        m_pendingPreviewSelectionGeneration = 0;
        host->CancelStructuredSelection(
            request.token, request.generation, L"timer_unavailable");
        return false;
    }
    return true;
}

void OcrDashboardWindow::HandlePreparedPreviewSelection(
    PreviewSelectionHost host,
    const std::wstring& token, uint64_t generation, bool success,
    const std::wstring& planJson, const std::wstring&)
{
    if (host != m_pendingPreviewSelectionHost ||
        token != m_pendingPreviewSelectionToken ||
        generation != m_pendingPreviewSelectionGeneration ||
        !m_pendingPreviewSelectionCallback) {
        return;
    }
    if (m_pendingPreviewSelectionTimer) {
        KillTimer(m_hwnd, m_pendingPreviewSelectionTimer);
        m_pendingPreviewSelectionTimer = 0;
    }
    auto callback = std::move(m_pendingPreviewSelectionCallback);
    m_pendingPreviewSelectionHost = PreviewSelectionHost::None;
    m_pendingPreviewSelectionToken.clear();
    m_pendingPreviewSelectionGeneration = 0;
    selection::SelectionContent content;
    if (success && !planJson.empty()) {
        content.kind = selection::SelectionContentKind::ZenCropPreview;
        content.fidelity = selection::SelectionFidelity::Semantic;
        content.requestToken = token;
        content.requestGeneration = generation;
        content.structuredPlanJson = planJson;
    }
    callback(std::move(content));
}

void OcrDashboardWindow::CancelPendingPreviewSelection(
    const std::wstring& errorCode)
{
    if (!m_pendingPreviewSelectionCallback) return;
    OcrMarkdownPreviewHost* host =
        m_pendingPreviewSelectionHost == PreviewSelectionHost::Source
        ? m_previewHost.get() : m_translationPreviewHost.get();
    const std::wstring token = m_pendingPreviewSelectionToken;
    const uint64_t generation = m_pendingPreviewSelectionGeneration;
    if (m_pendingPreviewSelectionTimer) {
        KillTimer(m_hwnd, m_pendingPreviewSelectionTimer);
        m_pendingPreviewSelectionTimer = 0;
    }
    auto callback = std::move(m_pendingPreviewSelectionCallback);
    m_pendingPreviewSelectionHost = PreviewSelectionHost::None;
    m_pendingPreviewSelectionToken.clear();
    m_pendingPreviewSelectionGeneration = 0;
    if (host) host->CancelStructuredSelection(token, generation, errorCode);
    callback({});
}
