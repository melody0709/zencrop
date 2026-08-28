#pragma once

#include "TranslationEngine.h"
#include "TranslationResultWindow.h"
#include "core/Settings.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

struct OcrOutput;
struct OcrSettings;
class IOcrEngine;

namespace translation {

struct OcrDeliveryGate;

// Typed UI sink for document translation. The sink is called only on the
// application/UI thread after the composition root dispatches the completion
// message; it owns presentation state, while the coordinator owns provider,
// batching, cancellation, and generation state.
class ITranslationEmbeddedSink {
public:
    virtual ~ITranslationEmbeddedSink() = default;
    virtual void OnTranslationStarted(uint64_t generation) = 0;
    virtual void OnTranslationFailed(
        uint64_t generation, const std::wstring& message) = 0;
    virtual void OnTranslationCompleted(
        uint64_t generation,
        const std::vector<TranslationSegment>& translations,
        const std::wstring& detectedSourceLanguage,
        DWORD elapsedMs) = 0;
};

// Composition-root-owned controller for OCR/text -> translation workflows.
// All methods are called on the application/UI thread except for
// the short-lived callbacks that only post WM_APP messages back to that
// thread. The controller never uploads the bitmap; only OCR text reaches the
// translation backend.
class TranslationCoordinator {
public:
    struct Dependencies {
        // Optional composition-root overrides. Production passes none and
        // resolves the configured OCR route/DeepSeek adapter below; tests and
        // future local adapters can provide typed engines without a callback
        // facade or a second mutable workflow owner.
        std::shared_ptr<IOcrEngine> ocrEngine;
        std::shared_ptr<ITranslationEngine> translationEngine;
    };

    explicit TranslationCoordinator(Dependencies dependencies = {});
    ~TranslationCoordinator();

    TranslationCoordinator(const TranslationCoordinator&) = delete;
    TranslationCoordinator& operator=(const TranslationCoordinator&) = delete;

    // Returns true once the result window has taken ownership of the workflow.
    // Synchronous preflight failures return false so the screenshot overlay can
    // restore the user's current selection.
    bool Start(HWND owner, RECT sourceRect, HBITMAP hBitmap);
    // Starts a block-aware, windowless translation workflow. Segment ids are
    // preserved in the final completion so a document projection can rebuild
    // Markdown and PreviewBlocks without touching the OCR model.
    bool StartEmbeddedSegments(
        HWND owner,
        RECT sourceRect,
        const std::vector<TranslationSegment>& segments,
        ITranslationEmbeddedSink* sink);
    void HandleOcrDone(uint64_t generation, OcrOutput* result);
    void HandleTranslationDone(uint64_t generation, TranslationResult* result);
    void CleanupInvalid();
    void Shutdown();

private:
    Dependencies dependencies_;
    HWND owner_ = nullptr;
    RECT sourceRect_ = {};
    uint64_t generation_ = 0;
    bool shuttingDown_ = false;
    bool active_ = false;
    bool ocrInFlight_ = false;
    UINT completionOcrMessage_ = 0;
    UINT completionTranslationMessage_ = 0;
    bool embeddedMode_ = false;
    ITranslationEmbeddedSink* embeddedSink_ = nullptr;

    TranslationSettings settings_;
    // Persisted/user-selected values stay independent from execution results.
    // In particular, source Auto must not be replaced with the provider's
    // detected language before a later edit/retranslation.
    std::wstring selectedSourceLanguage_ = L"auto";
    std::wstring selectedTargetLanguage_ = L"auto";
    std::wstring resolvedTargetLanguage_;
    TranslationRequest request_;
    std::shared_ptr<IOcrEngine> ocrEngine_;
    // OCR engines expose a fire-and-forget callback rather than a cancellable
    // operation. The gate closes delivery before coordinator/window teardown
    // and serializes the final PostMessage with queue draining.
    std::shared_ptr<OcrDeliveryGate> ocrDeliveryGate_;
    std::shared_ptr<AsyncHttpRequest> ocrWatchdog_;
    std::wstring ocrDisplayLabel_;
    HBITMAP ocrSourceBitmap_ = nullptr;
    std::shared_ptr<ITranslationEngine> translationEngine_;
    std::shared_ptr<AsyncHttpRequest> translationOperation_;
    std::unique_ptr<TranslationResultWindow> resultWindow_;
    std::wstring translatedBuffer_;
    std::vector<std::wstring> segmentBreaksAfter_;
    std::wstring translationLeadingBreaks_;
    std::wstring translationTrailingBreaks_;
    std::wstring detectedSourceLanguage_ = L"und";
    std::vector<TranslationSegment> completedTranslations_;
    // Identifies the currently outstanding translation batch. The engine may
    // be replaced or cancelled while callbacks are still draining; matching
    // this id prevents a late result from advancing the next batch.
    std::wstring currentBatchRequestId_;
    // A completed batch may still have a duplicate callback in flight after
    // the engine has been cancelled/advanced. Keep the small set of accepted
    // ids so those callbacks are harmless no-ops instead of visible mismatch
    // errors. It is cleared whenever a new generation starts.
    std::unordered_set<std::wstring> completedBatchRequestIds_;
    size_t nextSegmentIndex_ = 0;
    ULONGLONG translationStartedTick_ = 0;

    void CancelActiveTranslation();
    void CloseOcrDeliveryGate();
    void ReleaseOcrSourceBitmap();
    void ClearTranslationTextState();
    bool StartOcrRecognition(uint64_t generation);
    void CancelOcrWatchdog();
    void ShowError(const std::wstring& message);
    void BeginTranslation(uint64_t generation);
    void BeginNextTranslationBatch(uint64_t generation);
    void OnWindowCommand(TranslationResultWindow::Command command);
    void StartTranslationForSource(const std::wstring& source,
                                   const std::wstring& sourceLanguage,
                                   const std::wstring& targetLanguage);
};

} // namespace translation
