#pragma once

#include "TranslationEngine.h"
#include "TranslationLaunchContext.h"
#include "TranslationResultWindow.h"
#include "core/Settings.h"
#include "selection/SelectionStructuredContent.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
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
    TranslationStartResult StartText(
        HWND owner,
        const TranslationLaunchContext& context,
        std::wstring sourceText);
    TranslationStartResult StartSelection(
        HWND owner,
        const TranslationLaunchContext& context,
        selection::SelectionContent content);
    TranslationStartResult OpenTextEntry(
        HWND owner,
        const TranslationLaunchContext& context,
        ManualEntryReason reason);
    bool RequestPreviewSelection(
        HWND topLevelWindow,
        uint64_t requestGeneration,
        std::function<void(selection::SelectionContent)> callback);
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
    TranslationSourceMode sourceMode_ = TranslationSourceMode::OcrImage;
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
    // ---- Untranslatable segments (kept verbatim, never sent) ----
    // One flag per request_.segments entry: 1 when the whole segment is a URL,
    // a path, a hash and so on, i.e. content with nothing to translate. Such
    // segments stay inside the batch range but are excluded from the request,
    // because asking a model for them only invites an empty "text" (a contract
    // failure) or a wasted retry. Filled by the plain-text entry points only;
    // the structured selection paths reset it so their marker protocol and leaf
    // accounting stay untouched.
    std::vector<char> untranslatableSegments_;
    // ---- Bounded automatic retry (B1) ----
    // The coordinator is the single retry owner. Retries are issued from the UI
    // thread inside HandleTranslationDone's failure branch: translationOperation_
    // is only ever written there, and the engine callback runs on a worker
    // thread, so re-issuing from the callback would be a data race.
    struct IssuedBatch {
        // Copy of the batch exactly as it was sent. Retrying reuses this object
        // instead of re-slicing, so requestId/segments can never diverge from
        // the original request even if batching logic changes later.
        TranslationRequest request;
        uint64_t generation = 0;
        // Global segment range this batch covers: [beginIndex, endIndex). The
        // range also spans untranslatable segments, which is why the assembly
        // cannot be derived from the number of returned translations alone.
        size_t beginIndex = 0;
        size_t endIndex = 0;
    };
    IssuedBatch lastIssuedBatch_;
    // Attempt counters are per batch: transport-class and content-class retries
    // have independent quotas so a content failure cannot consume the transport
    // allowance (and vice versa). Reset in BeginTranslation and after every
    // accepted batch.
    int translationAttempt_ = 0;
    int translationContentAttempt_ = 0;
    // Start of the current batch's total budget. Paired with
    // ResolveTranslationBudget()'s requestDeadlineMs through
    // RemainingTranslationBudgetMs(). The budget is per batch by design.
    ULONGLONG translationBudgetStartTick_ = 0;
    // Number of batches issued for this translation (retries not included).
    size_t translationBatchCount_ = 0;
    enum class StructuredTranslationMode {
        None,
        LlmBlocks,
        DirectLeaves,
        LeafRetry,
    };
    std::shared_ptr<const selection::StructuredSelectionPlan> structuredPlan_;
    StructuredTranslationMode structuredTranslationMode_ =
        StructuredTranslationMode::None;
    std::wstring structuredMarkerNonce_;
    std::unordered_map<std::wstring, std::vector<std::wstring>>
        structuredBlockLeaves_;
    std::unordered_map<std::wstring, size_t> structuredLeafMarkerIndexes_;
    std::unordered_map<std::wstring, std::wstring> structuredLeafTranslations_;
    std::unordered_set<std::wstring> structuredInvalidBlocks_;
    bool structuredRetryAttempted_ = false;

    void CancelActiveTranslation();
    void CloseOcrDeliveryGate();
    void ReleaseOcrSourceBitmap();
    void ClearTranslationTextState();
    bool StartOcrRecognition(uint64_t generation);
    void CancelOcrWatchdog();
    void ShowError(const std::wstring& message);
    void BeginTranslation(uint64_t generation);
    void BeginNextTranslationBatch(uint64_t generation);
    // Classifies the current request_.segments. Called by the plain-text entry
    // points after segmentation; the structured paths clear the flags instead.
    void RefreshUntranslatableSegments();
    bool IsUntranslatableIndex(size_t index) const;
    // Appends the source text (and its preserved breaks) of the untranslatable
    // segments inside [begin, end) to translatedBuffer_, and records them in
    // completedTranslations_ so id-count contracts of downstream consumers stay
    // satisfied.
    void AppendUntranslatableRange(size_t begin, size_t end);
    // Shared terminal path for a fully assembled plain translation.
    void FinalizePlainTranslation(uint64_t generation);
    // Appends one diagnostics line, but only when this translation needed a
    // retry or failed: a clean translation has nothing to diagnose.
    void RecordTranslationDiagnostic(const wchar_t* outcome, bool failed,
                                     ErrorCode code, const std::wstring& error,
                                     const std::wstring& batchId);
    // Sends one already-sliced batch. Deliberately does NOT touch the stage
    // label: the caller owns the wording, otherwise a retry's "request timed
    // out, retrying..." would be overwritten by "translating..." within the same
    // UI-thread turn and never become visible.
    void IssueTranslationBatch(TranslationRequest batch);
    // Remaining duration of the current batch's total budget, or 0 when none.
    long long RemainingTranslationBudgetMs() const;
    // Decides and issues a bounded retry for a failed batch. Returns true when
    // a retry was started (the caller must then stop processing the failure).
    bool TryRetryFailedTranslation(
        const TranslationResult& result, uint64_t generation);
    void OnWindowCommand(TranslationResultWindow::Command command);
    void StartTranslationForSource(const std::wstring& source,
                                   const std::wstring& sourceLanguage,
                                   const std::wstring& targetLanguage);
    bool StartWindowTextTranslation();
    void HandlePreparedStructuredSelection(
        uint64_t workflowGeneration,
        selection::SelectionContent content,
        const std::wstring& token,
        uint64_t planGeneration,
        bool success,
        const std::wstring& planJson,
        const std::wstring& errorCode);
    void StartStructuredTranslation(
        std::shared_ptr<const selection::StructuredSelectionPlan> plan,
        const std::wstring& sourceLanguage,
        const std::wstring& targetLanguage);
    bool AcceptStructuredTranslation(
        const TranslationSegment& translation);
    bool BeginStructuredLeafRetry(uint64_t generation);
    void FinalizeStructuredTranslation(bool degraded);
};

} // namespace translation
