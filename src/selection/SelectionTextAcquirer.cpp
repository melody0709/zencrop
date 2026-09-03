#include "SelectionTextAcquirer.h"

#include "ClipboardCopyPolicy.h"
#include "ClipboardCopyTransaction.h"
#include "AppMessages.h"

#include <uiautomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace selection {
namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD kUiaConnectionTimeoutMs = 400;
constexpr DWORD kUiaTransactionTimeoutMs = 700;
constexpr DWORD kUiaWorkflowBudgetMs = 850;
constexpr DWORD kUiaWaitSliceMs = 12;
constexpr DWORD kUiaWorkerShutdownWaitMs = 500;
constexpr int kMaximumPatternParentDepth = 12;
constexpr int kMaximumIdentityParentDepth = 32;

enum class CandidateStatus {
    Success,
    NoSelection,
    Secure,
    TooLong,
    Unavailable,
};

struct CandidateResult {
    CandidateStatus status = CandidateStatus::Unavailable;
    std::wstring text;
    std::vector<RECT> rectangles;
    std::wstring diagnosticCode;
};

SelectionAcquisitionResult BaseResult(
    const SelectionTargetSnapshot& snapshot) {
    SelectionAcquisitionResult result;
    result.generation = snapshot.generation;
    result.cursor = snapshot.cursor;
    result.anchorRect = CursorAnchorRect(snapshot.cursor);
    result.clipboardDisposition = ClipboardDisposition::Untouched;
    return result;
}

enum class NativeIdentity {
    Absent,
    MatchesTarget,
    MismatchesTarget,
};

NativeIdentity ElementNativeIdentity(
    IUIAutomationElement* element,
    const SelectionTargetSnapshot& snapshot) {
    if (!element) return NativeIdentity::MismatchesTarget;
    UIA_HWND nativeValue = nullptr;
    if (FAILED(element->get_CurrentNativeWindowHandle(&nativeValue)) ||
        !nativeValue) {
        return NativeIdentity::Absent;
    }
    const HWND nativeWindow = static_cast<HWND>(nativeValue);
    return TopLevelWindow(nativeWindow) == snapshot.topLevelWindow
        ? NativeIdentity::MatchesTarget
        : NativeIdentity::MismatchesTarget;
}

bool SameAutomationElement(
    IUIAutomation* automation,
    IUIAutomationElement* left,
    IUIAutomationElement* right) {
    if (!automation || !left || !right) return false;
    BOOL same = FALSE;
    return SUCCEEDED(automation->CompareElements(left, right, &same)) && same;
}

CandidateStatus ElementSecurityStatus(IUIAutomationElement* element) {
    if (!element) return CandidateStatus::Unavailable;
    BOOL isPassword = FALSE;
    const HRESULT passwordResult = element->get_CurrentIsPassword(&isPassword);
    if (SUCCEEDED(passwordResult) && isPassword) {
        return CandidateStatus::Secure;
    }
    CONTROLTYPEID controlType = 0;
    const HRESULT typeResult = element->get_CurrentControlType(&controlType);
    if (FAILED(passwordResult) && SUCCEEDED(typeResult) &&
        controlType == UIA_EditControlTypeId) {
        return CandidateStatus::Secure;
    }
    return CandidateStatus::Unavailable;
}

bool CapturedTargetIdentityStillValid(
    const SelectionTargetSnapshot& snapshot) {
    if (!snapshot.topLevelWindow || !IsWindow(snapshot.topLevelWindow) ||
        TopLevelWindow(snapshot.topLevelWindow) != snapshot.topLevelWindow) {
        return false;
    }
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(
        snapshot.topLevelWindow, &processId);
    return threadId != 0 && processId == snapshot.processId &&
        threadId == snapshot.foregroundThreadId;
}

void AppendBoundingRectangles(
    IUIAutomationTextRange* range, std::vector<RECT>& rectangles) {
    if (!range) return;
    SAFEARRAY* bounds = nullptr;
    if (FAILED(range->GetBoundingRectangles(&bounds)) || !bounds) return;
    if (SafeArrayGetDim(bounds) != 1) {
        SafeArrayDestroy(bounds);
        return;
    }
    LONG lower = 0;
    LONG upper = -1;
    if (FAILED(SafeArrayGetLBound(bounds, 1, &lower)) ||
        FAILED(SafeArrayGetUBound(bounds, 1, &upper)) ||
        upper < lower || ((upper - lower + 1) % 4) != 0) {
        SafeArrayDestroy(bounds);
        return;
    }
    double* values = nullptr;
    if (FAILED(SafeArrayAccessData(bounds,
            reinterpret_cast<void**>(&values))) || !values) {
        SafeArrayDestroy(bounds);
        return;
    }
    const LONG count = upper - lower + 1;
    for (LONG index = 0; index + 3 < count; index += 4) {
        const double x = values[index];
        const double y = values[index + 1];
        const double width = values[index + 2];
        const double height = values[index + 3];
        if (!std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(width) || !std::isfinite(height) ||
            width <= 0.0 || height <= 0.0 ||
            std::abs(x) > 10000000.0 || std::abs(y) > 10000000.0 ||
            width > 10000000.0 || height > 10000000.0) {
            continue;
        }
        const double right = x + width;
        const double bottom = y + height;
        if (!std::isfinite(right) || !std::isfinite(bottom) ||
            right > static_cast<double>((std::numeric_limits<LONG>::max)()) ||
            bottom > static_cast<double>((std::numeric_limits<LONG>::max)()) ||
            x < static_cast<double>((std::numeric_limits<LONG>::min)()) ||
            y < static_cast<double>((std::numeric_limits<LONG>::min)())) {
            continue;
        }
        RECT rectangle = {
            static_cast<LONG>(std::lround(x)),
            static_cast<LONG>(std::lround(y)),
            static_cast<LONG>(std::lround(right)),
            static_cast<LONG>(std::lround(bottom)),
        };
        if (rectangle.right > rectangle.left &&
            rectangle.bottom > rectangle.top &&
            MonitorFromRect(&rectangle, MONITOR_DEFAULTTONULL)) {
            rectangles.push_back(rectangle);
        }
    }
    SafeArrayUnaccessData(bounds);
    SafeArrayDestroy(bounds);
}

CandidateResult ReadPatternSelection(IUIAutomationTextPattern* pattern) {
    CandidateResult result;
    if (!pattern) return result;
    ComPtr<IUIAutomationTextRangeArray> ranges;
    if (FAILED(pattern->GetSelection(&ranges)) || !ranges) {
        result.diagnosticCode = L"UIA_GET_SELECTION_FAILED";
        return result;
    }
    int count = 0;
    if (FAILED(ranges->get_Length(&count)) || count <= 0) {
        result.status = CandidateStatus::NoSelection;
        result.diagnosticCode = L"UIA_SELECTION_EMPTY";
        return result;
    }

    bool appended = false;
    for (int index = 0; index < count; ++index) {
        ComPtr<IUIAutomationTextRange> range;
        if (FAILED(ranges->GetElement(index, &range)) || !range) {
            result.status = CandidateStatus::Unavailable;
            result.text.clear();
            result.rectangles.clear();
            result.diagnosticCode = L"UIA_SELECTION_RANGE_UNAVAILABLE";
            return result;
        }
        int endpointComparison = 0;
        if (SUCCEEDED(range->CompareEndpoints(
                TextPatternRangeEndpoint_Start, range.Get(),
                TextPatternRangeEndpoint_End, &endpointComparison)) &&
            endpointComparison == 0) {
            continue;
        }

        const size_t separatorUnits = appended ? 1 : 0;
        if (result.text.size() + separatorUnits >= kMaxSelectionTextUnits) {
            result.status = CandidateStatus::TooLong;
            result.text.clear();
            result.diagnosticCode = L"UIA_SELECTION_TOO_LONG";
            return result;
        }
        const size_t remaining = kMaxSelectionTextUnits -
            result.text.size() - separatorUnits;
        BSTR value = nullptr;
        const int requestLength = static_cast<int>((std::min)(
            remaining + 1,
            static_cast<size_t>((std::numeric_limits<int>::max)())));
        if (FAILED(range->GetText(requestLength, &value)) || !value) {
            result.status = CandidateStatus::Unavailable;
            result.text.clear();
            result.rectangles.clear();
            result.diagnosticCode = L"UIA_SELECTION_TEXT_UNAVAILABLE";
            return result;
        }
        std::wstring segment(value, SysStringLen(value));
        SysFreeString(value);
        while (!segment.empty() && segment.back() == L'\0') {
            segment.pop_back();
        }
        if (segment.find(L'\0') != std::wstring::npos ||
            !IsValidSelectionUtf16(segment)) {
            result.status = CandidateStatus::Unavailable;
            result.text.clear();
            result.rectangles.clear();
            result.diagnosticCode = L"UIA_SELECTION_INVALID_UTF16";
            return result;
        }
        if (segment.size() > remaining) {
            result.status = CandidateStatus::TooLong;
            result.text.clear();
            result.rectangles.clear();
            result.diagnosticCode = L"UIA_SELECTION_TOO_LONG";
            return result;
        }
        if (!HasNonWhitespace(segment)) continue;
        if (appended) result.text.push_back(L'\n');
        result.text += segment;
        appended = true;
        AppendBoundingRectangles(range.Get(), result.rectangles);
    }
    if (!appended) {
        result.status = CandidateStatus::NoSelection;
        result.diagnosticCode = L"UIA_SELECTION_WHITESPACE_OR_DEGENERATE";
        return result;
    }
    result.status = CandidateStatus::Success;
    result.diagnosticCode = L"UIA_SELECTION_SUCCESS";
    return result;
}

CandidateResult ReadElementSelection(
    IUIAutomation* automation,
    IUIAutomationElement* candidate,
    IUIAutomationElement* trustedTargetRoot,
    const SelectionTargetSnapshot& snapshot) {
    CandidateResult last;
    if (!automation || !candidate) {
        last.diagnosticCode = L"UIA_TARGET_IDENTITY_MISMATCH";
        return last;
    }
    ComPtr<IUIAutomationTreeWalker> walker;
    automation->get_ControlViewWalker(&walker);
    ComPtr<IUIAutomationElement> current = candidate;
    std::vector<ComPtr<IUIAutomationElement>> candidateChain;
    bool targetIdentityConfirmed = false;
    bool identityDepthLimitReached = false;
    HRESULT parentResult = S_OK;
    int deepestVisited = -1;
    int identityAnchorDepth = -1;
    bool identityAnchoredByTrustedRoot = false;
    int nativeHandleCount = 0;
    for (int depth = 0;
         current && depth <= kMaximumIdentityParentDepth;
         ++depth) {
        deepestVisited = depth;
        const NativeIdentity nativeIdentity = ElementNativeIdentity(
            current.Get(), snapshot);
        if (nativeIdentity == NativeIdentity::MismatchesTarget) {
            last.status = CandidateStatus::Unavailable;
            last.diagnosticCode = L"UIA_TARGET_WINDOW_MISMATCH";
            return last;
        }
        if (nativeIdentity == NativeIdentity::MatchesTarget) {
            ++nativeHandleCount;
            targetIdentityConfirmed = true;
            identityAnchorDepth = depth;
        }
        if (ElementSecurityStatus(current.Get()) == CandidateStatus::Secure) {
            last.status = CandidateStatus::Secure;
            last.diagnosticCode = L"UIA_PASSWORD_ELEMENT";
            return last;
        }
        if (depth <= kMaximumPatternParentDepth) {
            candidateChain.push_back(current);
        }

        if (targetIdentityConfirmed) {
            break;
        }
        if (SameAutomationElement(
                automation, current.Get(), trustedTargetRoot)) {
            targetIdentityConfirmed = true;
            identityAnchorDepth = depth;
            identityAnchoredByTrustedRoot = true;
            break;
        }
        if (!walker) {
            parentResult = E_NOINTERFACE;
            break;
        }
        if (depth == kMaximumIdentityParentDepth) {
            identityDepthLimitReached = true;
            break;
        }
        ComPtr<IUIAutomationElement> parent;
        parentResult = walker->GetParentElement(current.Get(), &parent);
        if (FAILED(parentResult) || !parent) {
            break;
        }
        current = std::move(parent);
    }
    if (!targetIdentityConfirmed) {
        last.status = CandidateStatus::Unavailable;
        last.diagnosticCode =
            L"UIA_TARGET_ROOT_UNCONFIRMED:depth=" +
            std::to_wstring(deepestVisited) +
            L",limit=" + std::to_wstring(identityDepthLimitReached ? 1 : 0) +
            L",trusted=" + std::to_wstring(trustedTargetRoot ? 1 : 0) +
            L",native=" + std::to_wstring(nativeHandleCount) +
            L",parentHr=" +
            std::to_wstring(static_cast<unsigned long>(parentResult));
        return last;
    }

    // Modern browsers and Electron can expose document nodes from a renderer
    // process whose UIA ProcessId differs from the native top-level browser
    // HWND. Do not read selection text until the bounded parent chain above
    // has been anchored to the captured top-level HWND. Native handles that
    // point at another root have already been rejected.
    for (const auto& element : candidateChain) {
        if (ElementSecurityStatus(element.Get()) == CandidateStatus::Secure) {
            last.status = CandidateStatus::Secure;
            last.diagnosticCode = L"UIA_PASSWORD_ELEMENT";
            return last;
        }
        ComPtr<IUIAutomationTextPattern> pattern;
        ComPtr<IUIAutomationTextPattern2> pattern2;
        if (SUCCEEDED(element->GetCurrentPatternAs(
                UIA_TextPattern2Id, IID_PPV_ARGS(&pattern2))) && pattern2) {
            pattern2.As(&pattern);
        }
        if (!pattern) {
            element->GetCurrentPatternAs(
                UIA_TextPatternId, IID_PPV_ARGS(&pattern));
        }
        if (pattern) {
            CandidateResult read = ReadPatternSelection(pattern.Get());
            if (read.status == CandidateStatus::Success ||
                read.status == CandidateStatus::TooLong) {
                return read;
            } else {
                last = std::move(read);
            }
        }
    }
    if (last.diagnosticCode.empty()) {
        last.status = CandidateStatus::Unavailable;
        last.diagnosticCode =
            L"UIA_TEXT_PATTERN_UNAVAILABLE:anchorDepth=" +
            std::to_wstring(identityAnchorDepth) +
            L",trusted=" +
            std::to_wstring(identityAnchoredByTrustedRoot ? 1 : 0) +
            L",inspected=" + std::to_wstring(candidateChain.size());
    }
    return last;
}

CandidateResult AcquireByUiAutomation(
    IUIAutomation* automation,
    const SelectionTargetSnapshot& snapshot) {
    CandidateResult focusedResult;
    if (!automation) {
        focusedResult.diagnosticCode = L"UIA_AUTOMATION_UNAVAILABLE";
        return focusedResult;
    }
    if (!CapturedTargetIdentityStillValid(snapshot)) {
        focusedResult.diagnosticCode = L"UIA_TARGET_SNAPSHOT_CHANGED";
        return focusedResult;
    }
    if (IsNativePasswordEdit(snapshot.focusWindow)) {
        focusedResult.status = CandidateStatus::Secure;
        focusedResult.diagnosticCode = L"NATIVE_PASSWORD_EDIT";
        return focusedResult;
    }

    ComPtr<IUIAutomationElement> trustedTargetRoot;
    automation->ElementFromHandle(
        snapshot.topLevelWindow, &trustedTargetRoot);

    ComPtr<IUIAutomationElement> focused;
    HRESULT focusResult = automation->GetFocusedElement(&focused);
    if (focusResult == UIA_E_ELEMENTNOTAVAILABLE) {
        focusResult = automation->GetFocusedElement(&focused);
    }
    if (SUCCEEDED(focusResult) && focused) {
        focusedResult = ReadElementSelection(
            automation, focused.Get(), trustedTargetRoot.Get(), snapshot);
        if (focusedResult.status == CandidateStatus::Success ||
            focusedResult.status == CandidateStatus::Secure ||
            focusedResult.status == CandidateStatus::TooLong) {
            return focusedResult;
        }
    } else {
        focusedResult.diagnosticCode = L"UIA_FOCUSED_ELEMENT_UNAVAILABLE";
    }

    RECT targetRect = {};
    if (!snapshot.topLevelWindow ||
        !GetWindowRect(snapshot.topLevelWindow, &targetRect) ||
        !PtInRect(&targetRect, snapshot.cursor)) {
        return focusedResult;
    }
    ComPtr<IUIAutomationElement> pointed;
    if (FAILED(automation->ElementFromPoint(snapshot.cursor, &pointed)) ||
        !pointed) {
        return focusedResult;
    }
    if (focused) {
        BOOL same = FALSE;
        if (SUCCEEDED(automation->CompareElements(
                focused.Get(), pointed.Get(), &same)) && same) {
            return focusedResult;
        }
    }
    CandidateResult pointedResult = ReadElementSelection(
        automation, pointed.Get(), trustedTargetRoot.Get(), snapshot);
    if (pointedResult.status == CandidateStatus::Unavailable &&
        !focusedResult.diagnosticCode.empty()) {
        pointedResult.diagnosticCode = focusedResult.diagnosticCode + L";" +
            pointedResult.diagnosticCode;
    }
    return pointedResult;
}

ComPtr<IUIAutomation> CreateAutomationClient() {
    ComPtr<IUIAutomation> automation;
    HRESULT result = CoCreateInstance(CLSID_CUIAutomation8, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    if (FAILED(result) || !automation) {
        automation.Reset();
        result = CoCreateInstance(CLSID_CUIAutomation, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    }
    if (FAILED(result) || !automation) return {};
    ComPtr<IUIAutomation2> automation2;
    if (SUCCEEDED(automation.As(&automation2)) && automation2) {
        automation2->put_ConnectionTimeout(kUiaConnectionTimeoutMs);
        automation2->put_TransactionTimeout(kUiaTransactionTimeoutMs);
    }
    return automation;
}

struct UiaThreadJob {
    explicit UiaThreadJob(const SelectionTargetSnapshot& value)
        : snapshot(value), done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    ~UiaThreadJob() {
        if (done) CloseHandle(done);
    }

    SelectionTargetSnapshot snapshot;
    CandidateResult result;
    HANDLE done = nullptr;
};

struct UiaWorkerState {
    UiaWorkerState()
        : exitEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    ~UiaWorkerState() {
        if (exitEvent) CloseHandle(exitEvent);
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::shared_ptr<UiaThreadJob> pending;
    bool stopping = false;
    HANDLE exitEvent = nullptr;
};

struct UiaWorkerSlot {
    ~UiaWorkerSlot() {
        // A quarantined provider call cannot be force-cancelled. Its thread
        // captures shared state only, so detaching is safe if its bounded join
        // was not possible during shutdown.
        if (thread.joinable()) thread.detach();
    }

    std::shared_ptr<UiaWorkerState> state;
    std::thread thread;
};

std::unique_ptr<UiaWorkerSlot> CreateUiaWorker() {
    auto state = std::make_shared<UiaWorkerState>();
    if (!state->exitEvent) return {};
    auto slot = std::make_unique<UiaWorkerSlot>();
    slot->state = state;
    try {
        slot->thread = std::thread([state] {
            const HRESULT initResult = CoInitializeEx(
                nullptr, COINIT_MULTITHREADED);
            const bool comReady = SUCCEEDED(initResult);
            ComPtr<IUIAutomation> automation = comReady
                ? CreateAutomationClient() : ComPtr<IUIAutomation>{};

            for (;;) {
                std::shared_ptr<UiaThreadJob> job;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->condition.wait(lock, [&] {
                        return state->stopping || state->pending != nullptr;
                    });
                    if (state->stopping && !state->pending) break;
                    job = std::move(state->pending);
                }
                if (!job) continue;
                if (GetTickCount64() >= job->snapshot.deadlineTick) {
                    job->result.status = CandidateStatus::Unavailable;
                    job->result.diagnosticCode =
                        L"UIA_WORKFLOW_DEADLINE_EXPIRED";
                } else {
                    job->result = AcquireByUiAutomation(
                        automation.Get(), job->snapshot);
                }
                if (job->done) SetEvent(job->done);
            }

            if (comReady) CoUninitialize();
            SetEvent(state->exitEvent);
        });
    } catch (...) {
        return {};
    }
    return slot;
}

bool SubmitUiaJob(
    UiaWorkerSlot& worker, const std::shared_ptr<UiaThreadJob>& job) {
    if (!worker.state || !job || !job->done) return false;
    {
        std::lock_guard<std::mutex> lock(worker.state->mutex);
        if (worker.state->stopping || worker.state->pending) return false;
        worker.state->pending = job;
    }
    worker.state->condition.notify_one();
    return true;
}

void StopUiaWorker(UiaWorkerSlot* worker) {
    if (!worker || !worker->state) return;
    std::shared_ptr<UiaThreadJob> abandoned;
    {
        std::lock_guard<std::mutex> lock(worker->state->mutex);
        if (worker->state->stopping) return;
        worker->state->stopping = true;
        abandoned = std::move(worker->state->pending);
    }
    if (abandoned) {
        abandoned->result.status = CandidateStatus::Unavailable;
        abandoned->result.diagnosticCode = L"UIA_WORKER_STOPPED";
        if (abandoned->done) SetEvent(abandoned->done);
    }
    worker->state->condition.notify_all();
}

bool JoinExitedUiaWorker(
    std::unique_ptr<UiaWorkerSlot>& worker, DWORD waitMilliseconds) {
    if (!worker) return true;
    if (!worker->state || !worker->state->exitEvent ||
        WaitForSingleObject(worker->state->exitEvent, waitMilliseconds) !=
            WAIT_OBJECT_0) {
        return false;
    }
    if (worker->thread.joinable()) worker->thread.join();
    worker.reset();
    return true;
}

void DisposeUiaWorker(
    std::unique_ptr<UiaWorkerSlot>& worker, DWORD waitMilliseconds) {
    if (!worker) return;
    StopUiaWorker(worker.get());
    if (!JoinExitedUiaWorker(worker, waitMilliseconds)) {
        worker->thread.detach();
        worker.reset();
    }
}

bool PostResult(
    HWND deliveryWindow, SelectionAcquisitionResult result,
    const std::shared_ptr<std::mutex>& deliveryMutex,
    const std::shared_ptr<std::atomic<bool>>& deliveryClosed) {
    std::lock_guard<std::mutex> lock(*deliveryMutex);
    if (deliveryClosed->load(std::memory_order_acquire) ||
        !deliveryWindow || !IsWindow(deliveryWindow)) {
        return false;
    }
    auto* payload = new (std::nothrow)
        SelectionAcquisitionResult(std::move(result));
    if (!payload) return false;
    if (!PostMessageW(deliveryWindow, WM_APP_SELECTION_TEXT_ACQUIRED,
            static_cast<WPARAM>(payload->generation),
            reinterpret_cast<LPARAM>(payload))) {
        delete payload;
        return false;
    }
    return true;
}

} // namespace

struct SelectionTextAcquirer::State {
    explicit State(HWND window)
        : deliveryWindow(window),
          deliveryMutex(std::make_shared<std::mutex>()),
          deliveryClosed(std::make_shared<std::atomic<bool>>(false)),
          clipboard(std::make_shared<ClipboardCopyTransaction>()),
          exitEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    ~State() {
        if (exitEvent) CloseHandle(exitEvent);
    }

    HWND deliveryWindow = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    std::optional<SelectionTargetSnapshot> pending;
    std::atomic<uint64_t> latestGeneration{0};
    bool stopping = false;
    std::shared_ptr<std::mutex> deliveryMutex;
    std::shared_ptr<std::atomic<bool>> deliveryClosed;
    std::shared_ptr<ClipboardCopyTransaction> clipboard;
    HANDLE exitEvent = nullptr;
};

SelectionTextAcquirer::SelectionTextAcquirer(HWND deliveryWindow)
    : state_(std::make_shared<State>(deliveryWindow)) {
    const auto state = state_;
    worker_ = std::thread([state] {
        std::unique_ptr<UiaWorkerSlot> healthyUia = CreateUiaWorker();
        std::unique_ptr<UiaWorkerSlot> quarantinedUia;

        for (;;) {
            SelectionTargetSnapshot snapshot;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->condition.wait(lock, [&] {
                    return state->stopping || state->pending.has_value();
                });
                if (state->stopping && !state->pending) break;
                snapshot = *state->pending;
                state->pending.reset();
            }

            if (quarantinedUia &&
                JoinExitedUiaWorker(quarantinedUia, 0)) {
                healthyUia = CreateUiaWorker();
            }
            if (!healthyUia && !quarantinedUia) {
                healthyUia = CreateUiaWorker();
            }

            SelectionAcquisitionResult result = BaseResult(snapshot);
            CandidateResult uia;
            uia.diagnosticCode = quarantinedUia
                ? L"UIA_WORKER_QUARANTINED"
                : L"UIA_WORKER_UNAVAILABLE";

            enum class UiaWaitStatus {
                NotStarted,
                Completed,
                Superseded,
                Stopping,
                TimedOut,
            } waitStatus = UiaWaitStatus::NotStarted;

            if (healthyUia) {
                auto job = std::make_shared<UiaThreadJob>(snapshot);
                if (job->done && SubmitUiaJob(*healthyUia, job)) {
                    const ULONGLONG uiaDeadline = (std::min)(
                        snapshot.deadlineTick,
                        GetTickCount64() +
                            static_cast<ULONGLONG>(kUiaWorkflowBudgetMs));
                    for (;;) {
                        if (WaitForSingleObject(job->done, 0) ==
                            WAIT_OBJECT_0) {
                            waitStatus = UiaWaitStatus::Completed;
                            break;
                        }
                        bool stopping = false;
                        {
                            std::lock_guard<std::mutex> lock(state->mutex);
                            stopping = state->stopping;
                        }
                        if (stopping) {
                            waitStatus = UiaWaitStatus::Stopping;
                            break;
                        }
                        if (state->latestGeneration.load(
                                std::memory_order_acquire) !=
                            snapshot.generation) {
                            waitStatus = UiaWaitStatus::Superseded;
                            break;
                        }
                        const ULONGLONG now = GetTickCount64();
                        if (now >= uiaDeadline) {
                            waitStatus = UiaWaitStatus::TimedOut;
                            break;
                        }
                        const DWORD waitSlice = static_cast<DWORD>((std::min)(
                            uiaDeadline - now,
                            static_cast<ULONGLONG>(kUiaWaitSliceMs)));
                        if (WaitForSingleObject(job->done, waitSlice) ==
                            WAIT_OBJECT_0) {
                            waitStatus = UiaWaitStatus::Completed;
                            break;
                        }
                    }

                    if (waitStatus == UiaWaitStatus::Completed) {
                        uia = std::move(job->result);
                    } else {
                        StopUiaWorker(healthyUia.get());
                        quarantinedUia = std::move(healthyUia);
                        uia.status = CandidateStatus::Unavailable;
                        switch (waitStatus) {
                        case UiaWaitStatus::Superseded:
                            uia.diagnosticCode = L"UIA_WORKFLOW_SUPERSEDED";
                            break;
                        case UiaWaitStatus::Stopping:
                            uia.diagnosticCode = L"UIA_WORKFLOW_STOPPING";
                            break;
                        default:
                            uia.diagnosticCode = L"UIA_WORKFLOW_TIMEOUT";
                            break;
                        }
                    }
                } else {
                    uia.diagnosticCode = L"UIA_WORKER_SUBMIT_FAILED";
                }
            }

            if (state->latestGeneration.load(std::memory_order_acquire) !=
                    snapshot.generation) {
                result.error = SelectionAcquisitionError::Cancelled;
                result.diagnosticCode = L"ACQUISITION_SUPERSEDED";
            } else if (uia.status == CandidateStatus::Success) {
                result.error = SelectionAcquisitionError::None;
                result.source = SelectionAcquisitionSource::UiAutomation;
                result.text = uia.text;
                result.anchorRect = ChooseSelectionAnchor(
                    uia.rectangles, snapshot.cursor);
                result.diagnosticCode = uia.diagnosticCode;
            } else if (uia.status == CandidateStatus::Secure) {
                result.error = SelectionAcquisitionError::SecureField;
                result.diagnosticCode = uia.diagnosticCode;
            } else if (uia.status == CandidateStatus::TooLong) {
                result.error = SelectionAcquisitionError::TextTooLong;
                result.diagnosticCode = uia.diagnosticCode;
            } else if (!snapshot.copyFallbackEnabled) {
                result.error = SelectionAcquisitionError::UiaSelectionUnavailable;
                result.diagnosticCode = uia.diagnosticCode;
            } else if (ShouldSuppressSyntheticCopyForTarget(
                           snapshot.topLevelWindow)) {
                result.error =
                    SelectionAcquisitionError::SyntheticCopySuppressed;
                result.diagnosticCode = uia.diagnosticCode;
                if (!result.diagnosticCode.empty()) {
                    result.diagnosticCode += L";";
                }
                result.diagnosticCode +=
                    L"COPY_FALLBACK_SUPPRESSED_CONSOLE_TARGET";
            } else {
                result = state->clipboard->Acquire(snapshot);
                if (result.diagnosticCode.empty()) {
                    result.diagnosticCode = uia.diagnosticCode;
                } else if (!uia.diagnosticCode.empty()) {
                    result.diagnosticCode = uia.diagnosticCode + L";" +
                        result.diagnosticCode;
                }
            }

            if (state->latestGeneration.load(std::memory_order_acquire) ==
                    snapshot.generation) {
                PostResult(state->deliveryWindow, std::move(result),
                    state->deliveryMutex, state->deliveryClosed);
            }
        }

        DisposeUiaWorker(healthyUia, kUiaWorkerShutdownWaitMs);
        DisposeUiaWorker(quarantinedUia, kUiaWorkerShutdownWaitMs);
        SetEvent(state->exitEvent);
    });
}

SelectionTextAcquirer::~SelectionTextAcquirer() {
    Shutdown();
}

bool SelectionTextAcquirer::Start(
    const SelectionTargetSnapshot& snapshot) {
    const auto state = state_;
    if (!state || !snapshot.topLevelWindow || snapshot.processId == 0 ||
        snapshot.foregroundThreadId == 0 || snapshot.generation == 0) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->stopping) return false;
        state->latestGeneration.store(snapshot.generation,
            std::memory_order_release);
        state->pending = snapshot;
    }
    state->condition.notify_one();
    return true;
}

void SelectionTextAcquirer::Cancel() {
    const auto state = state_;
    if (!state) return;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->pending.reset();
        state->latestGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
    state->clipboard->Cancel();
    state->condition.notify_one();
}

void SelectionTextAcquirer::Shutdown() {
    const auto state = state_;
    if (!state) return;
    {
        std::lock_guard<std::mutex> deliveryLock(*state->deliveryMutex);
        state->deliveryClosed->store(true, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->stopping) return;
        state->stopping = true;
        state->pending.reset();
        state->latestGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
    state->condition.notify_all();
    state->clipboard->Shutdown();
    if (worker_.joinable()) {
        if (WaitForSingleObject(state->exitEvent, 1600) == WAIT_OBJECT_0) {
            worker_.join();
        } else {
            // UIA providers are out-of-process and cannot be force-cancelled.
            // The detached worker owns only shared state and a closed delivery
            // gate, so it cannot touch the destroyed controller or HWND.
            worker_.detach();
        }
    }
    state_.reset();
}

} // namespace selection
