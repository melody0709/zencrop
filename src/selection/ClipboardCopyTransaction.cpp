#include "ClipboardCopyTransaction.h"

#include "ClipboardDataSnapshot.h"
#include "ClipboardCopyPolicy.h"

#include <objidl.h>
#include <ole2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace selection {
namespace {

constexpr DWORD kKeyReleaseBudgetMs = 500;
constexpr DWORD kCopyUpdateBudgetMs = 800;
constexpr DWORD kClipboardOpenSliceMs = 8;
constexpr ULONG_PTR kSyntheticCopyMarker =
    static_cast<ULONG_PTR>(0x5A43534C43505931ULL); // ZCSLCPY1

DWORD RemainingMilliseconds(ULONGLONG deadline, DWORD cap = MAXDWORD) {
    const ULONGLONG now = GetTickCount64();
    if (deadline <= now) return 0;
    return static_cast<DWORD>((std::min)(deadline - now,
        static_cast<ULONGLONG>(cap)));
}

bool StopRequested(HANDLE stopEvent) {
    return stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0;
}

void PumpPendingMessages() {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool PumpDelay(HANDLE stopEvent, DWORD milliseconds) {
    const DWORD wait = MsgWaitForMultipleObjectsEx(
        stopEvent ? 1 : 0, stopEvent ? &stopEvent : nullptr,
        milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (stopEvent && wait == WAIT_OBJECT_0) return false;
    if (wait == WAIT_OBJECT_0 + (stopEvent ? 1 : 0)) PumpPendingMessages();
    return !StopRequested(stopEvent);
}

bool IsKeyDown(int virtualKey) {
    return virtualKey != 0 &&
        (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool TriggerKeysReleased(const HotkeyConfig& trigger) {
    const std::array<int, 8> keys = {
        static_cast<int>(trigger.key), 'C', VK_CONTROL, VK_MENU, VK_SHIFT,
        VK_LWIN, VK_RWIN, 0,
    };
    return std::none_of(keys.begin(), keys.end(), IsKeyDown);
}

bool WaitForTriggerKeysReleased(
    const SelectionTargetSnapshot& snapshot, HANDLE stopEvent) {
    const ULONGLONG releaseDeadline = (std::min)(snapshot.deadlineTick,
        GetTickCount64() + static_cast<ULONGLONG>(kKeyReleaseBudgetMs));
    while (GetTickCount64() < releaseDeadline) {
        if (TriggerKeysReleased(snapshot.triggerHotkey)) return true;
        if (!PumpDelay(stopEvent, 10)) return false;
    }
    return TriggerKeysReleased(snapshot.triggerHotkey);
}

bool IsNativeEditClass(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[96] = {};
    if (!GetClassNameW(window, className, static_cast<int>(std::size(className)))) {
        return false;
    }
    return _wcsicmp(className, L"Edit") == 0 ||
        _wcsnicmp(className, L"RichEdit", 8) == 0;
}

bool ValidateTarget(const SelectionTargetSnapshot& snapshot) {
    const HWND foreground = GetForegroundWindow();
    const HWND root = TopLevelWindow(foreground);
    if (!root || root != snapshot.topLevelWindow) return false;
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(root, &processId);
    if (!threadId || processId != snapshot.processId ||
        threadId != snapshot.foregroundThreadId) {
        return false;
    }

    GUITHREADINFO info = {sizeof(info)};
    const HWND currentFocus = GetGUIThreadInfo(threadId, &info)
        ? info.hwndFocus : nullptr;
    if (snapshot.focusWindow && currentFocus &&
        snapshot.focusWindow != currentFocus &&
        (IsNativeEditClass(snapshot.focusWindow) ||
         IsNativeEditClass(currentFocus))) {
        return false;
    }
    return true;
}

bool OpenClipboardUntil(HWND owner, ULONGLONG deadline, HANDLE stopEvent) {
    do {
        if (OpenClipboard(owner)) return true;
        if (!PumpDelay(stopEvent, kClipboardOpenSliceMs)) return false;
    } while (GetTickCount64() < deadline);
    return false;
}

bool SetClipboardBytes(UINT format, const void* bytes, SIZE_T size) {
    if (!format || !bytes || size == 0) return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return false;
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, bytes, size);
    GlobalUnlock(memory);
    if (!SetClipboardData(format, memory)) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

bool SetClipboardWideText(UINT format, const std::wstring& value) {
    return SetClipboardBytes(format, value.c_str(),
        (value.size() + 1) * sizeof(wchar_t));
}

std::wstring MakeTransactionId() {
    GUID id = {};
    if (FAILED(CoCreateGuid(&id))) {
        return L"ZenCrop.Selection." + std::to_wstring(GetTickCount64());
    }
    wchar_t buffer[64] = {};
    StringFromGUID2(id, buffer, static_cast<int>(std::size(buffer)));
    return L"ZenCrop.Selection." + std::wstring(buffer);
}

struct ClipboardSnapshot {
    ClipboardDataSnapshot data;
    bool wasEmpty = false;
    bool readable = false;
    DWORD sequence = 0;
};

struct ClipboardObservedState {
    DWORD sequence = 0;
    uint64_t fingerprint = 0;
    bool transactionMarkerPresent = false;
    bool transactionMarkerMatches = false;
    bool valid = false;
};

enum class RestoreOutcome {
    Restored,
    SkippedExternalUpdate,
    Incomplete,
};

ClipboardDisposition ToDisposition(RestoreOutcome outcome) {
    switch (outcome) {
    case RestoreOutcome::Restored:
        return ClipboardDisposition::Restored;
    case RestoreOutcome::SkippedExternalUpdate:
        return ClipboardDisposition::RestoreSkippedExternalUpdate;
    default:
        return ClipboardDisposition::RestoreIncomplete;
    }
}

void ReleasePartiallyInjectedKeys(UINT inserted) {
    SyntheticCopyCleanupInputs cleanup =
        BuildSyntheticCopyCleanupInputs(inserted, kSyntheticCopyMarker);
    if (cleanup.count != 0) {
        SendInput(cleanup.count, cleanup.inputs.data(), sizeof(INPUT));
    }
}

enum class ClipboardTextReadStatus {
    Success,
    NoText,
    TooLong,
    Invalid,
    Busy,
};

ClipboardTextReadStatus ReadWideClipboardHandle(
    HANDLE handle, std::wstring& text) {
    const SIZE_T bytes = GlobalSize(handle);
    if (bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t) != 0 ||
        bytes > (kMaxSelectionTextUnits + 2) * sizeof(wchar_t)) {
        return bytes > (kMaxSelectionTextUnits + 2) * sizeof(wchar_t)
            ? ClipboardTextReadStatus::TooLong
            : ClipboardTextReadStatus::Invalid;
    }
    const wchar_t* value = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!value) return ClipboardTextReadStatus::Invalid;
    const size_t capacity = bytes / sizeof(wchar_t);
    const wchar_t* terminator = static_cast<const wchar_t*>(
        std::wmemchr(value, L'\0', capacity));
    if (!terminator) {
        GlobalUnlock(handle);
        return ClipboardTextReadStatus::Invalid;
    }
    text.assign(value, terminator);
    GlobalUnlock(handle);
    if (text.size() > kMaxSelectionTextUnits) {
        text.clear();
        return ClipboardTextReadStatus::TooLong;
    }
    if (!IsValidSelectionUtf16(text)) {
        text.clear();
        return ClipboardTextReadStatus::Invalid;
    }
    return HasNonWhitespace(text)
        ? ClipboardTextReadStatus::Success
        : ClipboardTextReadStatus::NoText;
}

ClipboardTextReadStatus ReadAnsiClipboardHandle(
    HANDLE handle, UINT codePage, std::wstring& text) {
    const SIZE_T bytes = GlobalSize(handle);
    if (bytes == 0 || bytes > (kMaxSelectionTextUnits * 4 + 4)) {
        return bytes > (kMaxSelectionTextUnits * 4 + 4)
            ? ClipboardTextReadStatus::TooLong
            : ClipboardTextReadStatus::Invalid;
    }
    const char* value = static_cast<const char*>(GlobalLock(handle));
    if (!value) return ClipboardTextReadStatus::Invalid;
    const char* terminator = static_cast<const char*>(
        std::memchr(value, '\0', bytes));
    if (!terminator) {
        GlobalUnlock(handle);
        return ClipboardTextReadStatus::Invalid;
    }
    const int sourceLength = static_cast<int>(terminator - value);
    const int required = MultiByteToWideChar(
        codePage, 0, value, sourceLength, nullptr, 0);
    if (required <= 0 ||
        static_cast<size_t>(required) > kMaxSelectionTextUnits) {
        GlobalUnlock(handle);
        return required > static_cast<int>(kMaxSelectionTextUnits)
            ? ClipboardTextReadStatus::TooLong
            : ClipboardTextReadStatus::Invalid;
    }
    text.assign(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(codePage, 0, value, sourceLength,
        text.data(), required);
    GlobalUnlock(handle);
    return HasNonWhitespace(text)
        ? ClipboardTextReadStatus::Success
        : ClipboardTextReadStatus::NoText;
}

ClipboardTextReadStatus ReadClipboardTextOpen(std::wstring& text) {
    text.clear();
    ClipboardTextReadStatus status = ClipboardTextReadStatus::NoText;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        status = ReadWideClipboardHandle(handle, text);
    } else if (HANDLE handle = GetClipboardData(CF_TEXT)) {
        status = ReadAnsiClipboardHandle(handle, CP_ACP, text);
    } else if (HANDLE handle = GetClipboardData(CF_OEMTEXT)) {
        status = ReadAnsiClipboardHandle(handle, CP_OEMCP, text);
    }
    return status;
}

void HashFingerprintBytes(
    uint64_t& hash, const void* bytes, size_t byteCount) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    const auto* cursor = static_cast<const unsigned char*>(bytes);
    for (size_t index = 0; index < byteCount; ++index) {
        hash ^= cursor[index];
        hash *= kFnvPrime;
    }
}

template <typename Value>
void HashFingerprintValue(uint64_t& hash, const Value& value) {
    HashFingerprintBytes(hash, &value, sizeof(value));
}

ClipboardObservedState ObserveOpenClipboard(
    ClipboardTextReadStatus textStatus, const std::wstring& text,
    UINT transactionFormat,
    const std::wstring* expectedTransactionId = nullptr) {
    ClipboardObservedState observed;
    constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
    observed.fingerprint = kFnvOffset;

    std::vector<UINT> formats;
    for (UINT format = EnumClipboardFormats(0); format != 0;
         format = EnumClipboardFormats(format)) {
        formats.push_back(format);
    }
    std::sort(formats.begin(), formats.end());
    const uint64_t formatCount = static_cast<uint64_t>(formats.size());
    HashFingerprintValue(observed.fingerprint, formatCount);
    for (const UINT format : formats) {
        HashFingerprintValue(observed.fingerprint, format);
    }

    const auto statusValue = static_cast<unsigned int>(textStatus);
    HashFingerprintValue(observed.fingerprint, statusValue);
    const uint64_t textUnits = static_cast<uint64_t>(text.size());
    HashFingerprintValue(observed.fingerprint, textUnits);
    if (!text.empty()) {
        HashFingerprintBytes(observed.fingerprint, text.data(),
            text.size() * sizeof(wchar_t));
    }

    // The transaction GUID format is private to ZenCrop and HGLOBAL-backed.
    // Hash its bounded bytes when present so ownership does not rely on a
    // sequence number and the public text sentinel alone.
    if (transactionFormat != 0) {
        const bool markerPresent =
            std::binary_search(formats.begin(), formats.end(), transactionFormat);
        observed.transactionMarkerPresent = markerPresent;
        HashFingerprintValue(observed.fingerprint, markerPresent);
        if (markerPresent) {
            if (HANDLE handle = GetClipboardData(transactionFormat)) {
                const SIZE_T size = GlobalSize(handle);
                if (size > 0 && size <= 4096) {
                    const void* data = GlobalLock(handle);
                    if (data) {
                        HashFingerprintValue(observed.fingerprint, size);
                        HashFingerprintBytes(observed.fingerprint, data, size);
                        if (expectedTransactionId &&
                            size % sizeof(wchar_t) == 0) {
                            const auto* value =
                                static_cast<const wchar_t*>(data);
                            const size_t capacity = size / sizeof(wchar_t);
                            const wchar_t* terminator =
                                static_cast<const wchar_t*>(std::wmemchr(
                                    value, L'\0', capacity));
                            observed.transactionMarkerMatches = terminator &&
                                static_cast<size_t>(terminator - value) ==
                                    expectedTransactionId->size() &&
                                std::equal(value, terminator,
                                    expectedTransactionId->begin());
                        }
                        GlobalUnlock(handle);
                    }
                }
            }
        }
    }

    observed.sequence = GetClipboardSequenceNumber();
    observed.valid = true;
    return observed;
}

ClipboardTextReadStatus ReadClipboardText(
    HWND owner, ULONGLONG deadline, HANDLE stopEvent,
    UINT transactionFormat, const std::wstring* expectedTransactionId,
    std::wstring& text,
    ClipboardObservedState& observed) {
    observed = {};
    if (!OpenClipboardUntil(owner, deadline, stopEvent)) {
        return ClipboardTextReadStatus::Busy;
    }
    const ClipboardTextReadStatus status = ReadClipboardTextOpen(text);
    observed = ObserveOpenClipboard(
        status, text, transactionFormat, expectedTransactionId);
    CloseClipboard();
    return status;
}

bool BeginClipboardTransaction(
    HWND owner, ULONGLONG deadline, HANDLE stopEvent,
    const std::wstring& transactionId, UINT transactionFormat,
    ClipboardSnapshot& snapshot, ClipboardObservedState& sentinelState,
    bool& clipboardMutated) {
    clipboardMutated = false;
    sentinelState = {};
    snapshot.sequence = GetClipboardSequenceNumber();
    IDataObject* liveClipboard = nullptr;
    const HRESULT objectResult = OleGetClipboard(&liveClipboard);
    if (!OpenClipboardUntil(owner, deadline, stopEvent)) {
        if (liveClipboard) liveClipboard->Release();
        return false;
    }

    const DWORD sequenceWhileOpen = GetClipboardSequenceNumber();
    snapshot.wasEmpty = CountClipboardFormats() == 0;
    CloseClipboard();
    if (sequenceWhileOpen != snapshot.sequence) {
        if (liveClipboard) liveClipboard->Release();
        return false;
    }

    snapshot.readable = snapshot.wasEmpty;
    if (!snapshot.wasEmpty && SUCCEEDED(objectResult) && liveClipboard) {
        snapshot.readable = snapshot.data.Capture(liveClipboard);
    }
    if (liveClipboard) liveClipboard->Release();
    if (!snapshot.readable ||
        GetClipboardSequenceNumber() != snapshot.sequence ||
        !OpenClipboardUntil(owner, deadline, stopEvent)) {
        return false;
    }

    const DWORD sequenceBeforeMutation = GetClipboardSequenceNumber();
    const bool stillEmpty = CountClipboardFormats() == 0;
    if (sequenceBeforeMutation != snapshot.sequence ||
        stillEmpty != snapshot.wasEmpty) {
        CloseClipboard();
        return false;
    }

    bool success = EmptyClipboard() != FALSE;
    clipboardMutated = success;
    const std::wstring sentinel = transactionId + L".sentinel";
    if (success) success = SetClipboardWideText(CF_UNICODETEXT, sentinel);
    if (success) {
        success = SetClipboardWideText(transactionFormat, transactionId);
    }

    const UINT historyFormat = RegisterClipboardFormatW(
        L"CanIncludeInClipboardHistory");
    const UINT cloudFormat = RegisterClipboardFormatW(
        L"CanUploadToCloudClipboard");
    const UINT monitorFormat = RegisterClipboardFormatW(
        L"ExcludeClipboardContentFromMonitorProcessing");
    const DWORD disabled = 0;
    const wchar_t excluded = L'1';
    if (success && historyFormat) {
        success = SetClipboardBytes(historyFormat, &disabled, sizeof(disabled));
    }
    if (success && cloudFormat) {
        success = SetClipboardBytes(cloudFormat, &disabled, sizeof(disabled));
    }
    if (success && monitorFormat) {
        success = SetClipboardBytes(monitorFormat, &excluded, sizeof(excluded));
    }

    CloseClipboard();

    // Closing the clipboard can expose synthesized/system-added formats.
    // Re-open it and fingerprint the externally visible final state instead
    // of combining a pre-close fingerprint with a post-close sequence.
    std::wstring finalizedText;
    ClipboardObservedState finalizedState;
    const ULONGLONG finalizeDeadline = (std::max)(
        deadline, GetTickCount64() + static_cast<ULONGLONG>(250));
    const ClipboardTextReadStatus finalizedStatus = ReadClipboardText(
        owner, finalizeDeadline, nullptr, transactionFormat,
        &transactionId, finalizedText, finalizedState);
    const bool ownsFinalState = finalizedState.valid &&
        finalizedState.transactionMarkerMatches &&
        finalizedText == sentinel;
    sentinelState = ownsFinalState
        ? finalizedState : ClipboardObservedState{};
    return success && finalizedStatus == ClipboardTextReadStatus::Success &&
        ownsFinalState;
}

bool ClipboardMatchesObservedStateOpen(
    const ClipboardObservedState& expected, UINT transactionFormat) {
    if (!expected.valid) return false;
    std::wstring currentText;
    const ClipboardTextReadStatus currentStatus =
        ReadClipboardTextOpen(currentText);
    const ClipboardObservedState current = ObserveOpenClipboard(
        currentStatus, currentText, transactionFormat);
    return current.valid && current.sequence == expected.sequence &&
        current.fingerprint == expected.fingerprint;
}

bool ClipboardMatchesObservedState(
    HWND owner, const ClipboardObservedState& expected,
    UINT transactionFormat, ULONGLONG deadline, HANDLE stopEvent) {
    if (!expected.valid ||
        GetClipboardSequenceNumber() != expected.sequence ||
        !OpenClipboardUntil(owner, deadline, stopEvent)) {
        return false;
    }
    const bool matches = ClipboardMatchesObservedStateOpen(
        expected, transactionFormat);
    CloseClipboard();
    return matches;
}

RestoreOutcome RestoreClipboard(
    HWND owner, ClipboardSnapshot& snapshot,
    const ClipboardObservedState& expected, UINT transactionFormat,
    ULONGLONG deadline, HANDLE stopEvent,
    std::wstring* diagnostic = nullptr) {
    const auto setDiagnostic = [diagnostic](const wchar_t* value) {
        if (diagnostic) *diagnostic = value;
    };
    if (!expected.valid ||
        GetClipboardSequenceNumber() != expected.sequence) {
        setDiagnostic(L"RESTORE_SEQUENCE_CHANGED");
        return RestoreOutcome::SkippedExternalUpdate;
    }
    if (!OpenClipboardUntil(owner, deadline, stopEvent)) {
        setDiagnostic(L"RESTORE_OPEN_FAILED");
        return RestoreOutcome::Incomplete;
    }
    if (!ClipboardMatchesObservedStateOpen(expected, transactionFormat)) {
        CloseClipboard();
        setDiagnostic(L"RESTORE_FINGERPRINT_CHANGED");
        return RestoreOutcome::SkippedExternalUpdate;
    }
    if (snapshot.wasEmpty) {
        const bool emptied = EmptyClipboard() != FALSE;
        CloseClipboard();
        setDiagnostic(emptied ? L"RESTORE_EMPTY_OK" : L"RESTORE_EMPTY_FAILED");
        return emptied ? RestoreOutcome::Restored : RestoreOutcome::Incomplete;
    }
    CloseClipboard();

    IDataObject* const snapshotData = snapshot.data.DataObject();
    if (!snapshotData) {
        setDiagnostic(L"RESTORE_DATA_OBJECT_MISSING");
        return RestoreOutcome::Incomplete;
    }
    if (GetClipboardSequenceNumber() != expected.sequence) {
        setDiagnostic(L"RESTORE_SEQUENCE_CHANGED_AFTER_CLOSE");
        return RestoreOutcome::SkippedExternalUpdate;
    }
    HRESULT setResult = E_FAIL;
    do {
        if (GetClipboardSequenceNumber() != expected.sequence) {
            setDiagnostic(L"RESTORE_SEQUENCE_CHANGED_BEFORE_OLE_SET");
            return RestoreOutcome::SkippedExternalUpdate;
        }
        setResult = OleSetClipboard(snapshotData);
        if (SUCCEEDED(setResult) || setResult != CLIPBRD_E_CANT_OPEN ||
            GetTickCount64() >= deadline) {
            break;
        }
    } while (PumpDelay(stopEvent, kClipboardOpenSliceMs));
    const bool dataObjectIsCurrent =
        OleIsCurrentClipboard(snapshotData) == S_OK;
    if (FAILED(setResult) && !dataObjectIsCurrent) {
        if (diagnostic) {
            const HWND openWindow = GetOpenClipboardWindow();
            DWORD openProcessId = 0;
            const DWORD openThreadId = openWindow
                ? GetWindowThreadProcessId(openWindow, &openProcessId) : 0;
            *diagnostic = L"RESTORE_OLE_SET_FAILED_" +
                std::to_wstring(static_cast<unsigned long>(setResult)) +
                L"_OPEN_HWND_" +
                std::to_wstring(reinterpret_cast<uintptr_t>(openWindow)) +
                L"_PID_" + std::to_wstring(openProcessId) +
                L"_TID_" + std::to_wstring(openThreadId);
        }
        return RestoreOutcome::Incomplete;
    }
    HRESULT flushResult = E_FAIL;
    do {
        flushResult = OleFlushClipboard();
        if (SUCCEEDED(flushResult) || flushResult != CLIPBRD_E_CANT_OPEN ||
            OleIsCurrentClipboard(snapshotData) != S_OK ||
            GetTickCount64() >= deadline) {
            break;
        }
    } while (PumpDelay(stopEvent, kClipboardOpenSliceMs));
    if (FAILED(flushResult)) {
        if (diagnostic) {
            *diagnostic = L"RESTORE_OLE_FLUSH_FAILED_" +
                std::to_wstring(static_cast<unsigned long>(flushResult));
        }
        return RestoreOutcome::Incomplete;
    }
    if (!snapshot.data.Complete()) {
        setDiagnostic(L"RESTORE_OLE_PARTIAL_SNAPSHOT");
        return RestoreOutcome::Incomplete;
    }
    setDiagnostic(L"RESTORE_OLE_OK");
    return RestoreOutcome::Restored;
}

SelectionAcquisitionResult BaseResult(
    const SelectionTargetSnapshot& snapshot) {
    SelectionAcquisitionResult result;
    result.generation = snapshot.generation;
    result.cursor = snapshot.cursor;
    result.anchorRect = CursorAnchorRect(snapshot.cursor);
    result.clipboardDisposition = ClipboardDisposition::Untouched;
    return result;
}

struct ClipboardThreadJob {
    explicit ClipboardThreadJob(const SelectionTargetSnapshot& value)
        : snapshot(value), done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ClipboardThreadJob() {
        if (done) CloseHandle(done);
    }

    SelectionTargetSnapshot snapshot;
    SelectionAcquisitionResult result;
    HANDLE done = nullptr;
    std::atomic<bool> cancelled{false};
};

SelectionAcquisitionResult RunTransaction(
    const std::shared_ptr<ClipboardThreadJob>& job,
    HWND listenerWindow, HANDLE stopEvent) {
    const SelectionTargetSnapshot& snapshot = job->snapshot;
    SelectionAcquisitionResult result = BaseResult(snapshot);
    if (snapshot.copyShortcutConflict) {
        result.error = SelectionAcquisitionError::CopyShortcutConflict;
        result.diagnosticCode = L"COPY_SHORTCUT_CONFLICT";
        return result;
    }
    if (!WaitForTriggerKeysReleased(snapshot, stopEvent)) {
        result.error = StopRequested(stopEvent) || job->cancelled.load()
            ? SelectionAcquisitionError::Cancelled
            : SelectionAcquisitionError::TriggerKeysHeld;
        result.diagnosticCode = L"TRIGGER_KEYS_HELD";
        return result;
    }
    if (job->cancelled.load() || StopRequested(stopEvent)) {
        result.error = SelectionAcquisitionError::Cancelled;
        result.diagnosticCode = L"COPY_CANCELLED_BEFORE_TRANSACTION";
        return result;
    }
    if (!ValidateTarget(snapshot)) {
        result.error = SelectionAcquisitionError::TargetChanged;
        result.diagnosticCode = L"COPY_TARGET_CHANGED";
        return result;
    }
    if (IsNativePasswordEdit(snapshot.focusWindow)) {
        result.error = SelectionAcquisitionError::SecureField;
        result.diagnosticCode = L"NATIVE_PASSWORD_EDIT";
        return result;
    }

    const std::wstring transactionId = MakeTransactionId();
    const UINT transactionFormat = RegisterClipboardFormatW(
        L"ZenCrop.SelectionTransaction.v1");
    ClipboardSnapshot clipboardSnapshot;
    ClipboardObservedState sentinelState;
    bool clipboardMutated = false;
    if (!transactionFormat || !BeginClipboardTransaction(
            listenerWindow, snapshot.deadlineTick, stopEvent,
            transactionId, transactionFormat, clipboardSnapshot,
            sentinelState, clipboardMutated)) {
        result.error = SelectionAcquisitionError::ClipboardBusy;
        result.diagnosticCode = clipboardMutated
            ? L"CLIPBOARD_SENTINEL_FAILED"
            : L"CLIPBOARD_SNAPSHOT_FAILED";
        if (clipboardMutated && sentinelState.valid) {
            result.clipboardDisposition = ToDisposition(RestoreClipboard(
                listenerWindow, clipboardSnapshot, sentinelState,
                transactionFormat, GetTickCount64() + 500, nullptr));
        } else if (clipboardMutated) {
            result.clipboardDisposition =
                ClipboardDisposition::RestoreIncomplete;
        }
        return result;
    }

    std::wstring restoreDiagnostic;
    const auto restoreOwnedState = [&](const ClipboardObservedState& state) {
        const RestoreOutcome outcome = RestoreClipboard(
            listenerWindow, clipboardSnapshot, state, transactionFormat,
            GetTickCount64() + 500, nullptr, &restoreDiagnostic);
        result.clipboardDisposition = ToDisposition(outcome);
    };

    if (job->cancelled.load() || StopRequested(stopEvent)) {
        restoreOwnedState(sentinelState);
        result.error = SelectionAcquisitionError::Cancelled;
        result.diagnosticCode = L"COPY_CANCELLED_BEFORE_INJECTION";
        return result;
    }
    if (!ValidateTarget(snapshot)) {
        restoreOwnedState(sentinelState);
        result.error = SelectionAcquisitionError::TargetChanged;
        result.diagnosticCode = L"COPY_TARGET_CHANGED_BEFORE_INJECTION";
        return result;
    }
    if (!TriggerKeysReleased(snapshot.triggerHotkey)) {
        restoreOwnedState(sentinelState);
        result.error = SelectionAcquisitionError::TriggerKeysHeld;
        result.diagnosticCode = L"COPY_KEYS_PRESSED_BEFORE_INJECTION";
        return result;
    }
    if (!ClipboardMatchesObservedState(listenerWindow, sentinelState,
            transactionFormat, snapshot.deadlineTick, stopEvent)) {
        restoreOwnedState(sentinelState);
        result.error = SelectionAcquisitionError::ClipboardBusy;
        result.diagnosticCode = L"CLIPBOARD_SENTINEL_OWNERSHIP_LOST";
        return result;
    }

    std::array<INPUT, kSyntheticCopyInputCount> inputs =
        BuildSyntheticCopyInputs(kSyntheticCopyMarker);
    SetLastError(ERROR_SUCCESS);
    const UINT inserted = SendInput(static_cast<UINT>(inputs.size()),
        inputs.data(), sizeof(INPUT));
    if (inserted != inputs.size()) {
        const DWORD sendError = GetLastError();
        ReleasePartiallyInjectedKeys(inserted);
        restoreOwnedState(sentinelState);
        result.error = SelectionAcquisitionError::CopyNotPermittedOrUnsupported;
        result.diagnosticCode = L"SENDINPUT_PARTIAL_" +
            std::to_wstring(inserted) + L"_ERROR_" +
            std::to_wstring(sendError);
        return result;
    }

    const ULONGLONG copyDeadline = (std::min)(snapshot.deadlineTick,
        GetTickCount64() + static_cast<ULONGLONG>(kCopyUpdateBudgetMs));
    ClipboardObservedState lastOwnedState = sentinelState;
    ClipboardObservedState observedState;
    ClipboardTextReadStatus readStatus = ClipboardTextReadStatus::NoText;
    bool externalTransactionMarker = false;
    while (GetTickCount64() < copyDeadline &&
           !job->cancelled.load() && !StopRequested(stopEvent)) {
        const DWORD sequence = GetClipboardSequenceNumber();
        if (sequence != lastOwnedState.sequence) {
            std::wstring copiedText;
            readStatus = ReadClipboardText(listenerWindow, copyDeadline,
                stopEvent, transactionFormat, &transactionId,
                copiedText, observedState);
            if (readStatus != ClipboardTextReadStatus::Busy) {
                // A clipboard manager may rewrite or re-publish our sentinel.
                // The private format must be gone before any text can be
                // attributed to the target application's copy operation.
                if (observedState.valid &&
                    observedState.transactionMarkerMatches) {
                    lastOwnedState = observedState;
                    observedState = {};
                    result.text.clear();
                    readStatus = ClipboardTextReadStatus::NoText;
                    continue;
                }
                if (observedState.valid &&
                    observedState.transactionMarkerPresent) {
                    externalTransactionMarker = true;
                    result.text.clear();
                    break;
                }
                result.text = std::move(copiedText);
                break;
            }
        }
        if (!PumpDelay(stopEvent, 12)) break;
    }

    if (externalTransactionMarker) {
        result.clipboardDisposition =
            ClipboardDisposition::RestoreSkippedExternalUpdate;
    } else {
        const ClipboardObservedState& ownedState = observedState.valid
            ? observedState : lastOwnedState;
        restoreOwnedState(ownedState);
    }
    if (job->cancelled.load() || StopRequested(stopEvent)) {
        result.error = SelectionAcquisitionError::Cancelled;
        result.diagnosticCode = L"COPY_CANCELLED_AFTER_INJECTION";
        return result;
    }
    if (externalTransactionMarker) {
        result.error =
            SelectionAcquisitionError::CopyNotPermittedOrUnsupported;
        result.diagnosticCode =
            L"CLIPBOARD_EXTERNAL_TRANSACTION_MARKER";
        return result;
    }
    if (!observedState.valid) {
        result.error = SelectionAcquisitionError::CopyTimedOut;
        result.diagnosticCode = L"COPY_UPDATE_TIMEOUT";
        return result;
    }
    if (readStatus == ClipboardTextReadStatus::TooLong) {
        result.error = SelectionAcquisitionError::TextTooLong;
        result.diagnosticCode = L"CLIPBOARD_TEXT_TOO_LONG";
        return result;
    }
    if (readStatus != ClipboardTextReadStatus::Success) {
        result.error = SelectionAcquisitionError::CopyNotPermittedOrUnsupported;
        result.diagnosticCode = L"CLIPBOARD_COPY_NO_TEXT";
        return result;
    }

    result.error = SelectionAcquisitionError::None;
    result.source = SelectionAcquisitionSource::ClipboardCopy;
    result.diagnosticCode = L"COPY_SUCCESS";
    if (!restoreDiagnostic.empty()) {
        result.diagnosticCode += L"_" + restoreDiagnostic;
    }
    return result;
}

const wchar_t* ClipboardListenerClassName() {
    return L"ZenCrop.SelectionClipboardListener";
}

LRESULT CALLBACK ClipboardListenerProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateClipboardListenerWindow() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSEXW windowClass = {sizeof(windowClass)};
        windowClass.lpfnWndProc = ClipboardListenerProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = ClipboardListenerClassName();
        RegisterClassExW(&windowClass);
    });
    HWND window = CreateWindowExW(0, ClipboardListenerClassName(), L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window) AddClipboardFormatListener(window);
    return window;
}

} // namespace

struct ClipboardCopyTransaction::State {
    State()
        : stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          workEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
          exitEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    ~State() {
        if (stopEvent) CloseHandle(stopEvent);
        if (workEvent) CloseHandle(workEvent);
        if (exitEvent) CloseHandle(exitEvent);
    }

    std::mutex mutex;
    std::shared_ptr<ClipboardThreadJob> pending;
    std::shared_ptr<ClipboardThreadJob> current;
    bool stopping = false;
    bool workerExited = false;
    HANDLE stopEvent = nullptr;
    HANDLE workEvent = nullptr;
    HANDLE exitEvent = nullptr;
};

ClipboardCopyTransaction::ClipboardCopyTransaction()
    : state_(std::make_shared<State>()) {
    const auto state = state_;
    worker_ = std::thread([state] {
        const HRESULT oleResult = OleInitialize(nullptr);
        const bool oleReady = SUCCEEDED(oleResult) || oleResult == S_FALSE;
        bool platformReady = oleReady && state->stopEvent &&
            state->workEvent && state->exitEvent;
        HWND listenerWindow = oleReady ? CreateClipboardListenerWindow() : nullptr;
        const HANDLE handles[] = {state->stopEvent, state->workEvent};
        while (platformReady && !StopRequested(state->stopEvent)) {
            const DWORD wait = MsgWaitForMultipleObjectsEx(
                2, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED) {
                platformReady = false;
                break;
            }
            if (wait == WAIT_OBJECT_0 + 2) {
                PumpPendingMessages();
                continue;
            }
            if (wait != WAIT_OBJECT_0 + 1) continue;
            std::shared_ptr<ClipboardThreadJob> job;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                job = std::move(state->pending);
                state->current = job;
            }
            if (!job) continue;
            job->result = RunTransaction(job, listenerWindow, state->stopEvent);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->current == job) state->current.reset();
            }
            if (job->done) SetEvent(job->done);
        }

        std::shared_ptr<ClipboardThreadJob> abandoned;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            abandoned = std::move(state->pending);
            state->workerExited = true;
        }
        if (abandoned) {
            abandoned->result = BaseResult(abandoned->snapshot);
            abandoned->result.error = platformReady
                ? SelectionAcquisitionError::Cancelled
                : SelectionAcquisitionError::PlatformError;
            abandoned->result.diagnosticCode = platformReady
                ? L"COPY_WORKER_STOPPED"
                : L"COPY_WORKER_PLATFORM_UNAVAILABLE";
            if (abandoned->done) SetEvent(abandoned->done);
        }
        if (listenerWindow) {
            RemoveClipboardFormatListener(listenerWindow);
            DestroyWindow(listenerWindow);
        }
        if (oleReady) OleUninitialize();
        if (state->exitEvent) SetEvent(state->exitEvent);
    });
}

ClipboardCopyTransaction::~ClipboardCopyTransaction() {
    Shutdown();
}

SelectionAcquisitionResult ClipboardCopyTransaction::Acquire(
    const SelectionTargetSnapshot& snapshot) {
    SelectionAcquisitionResult cancelled = BaseResult(snapshot);
    cancelled.error = SelectionAcquisitionError::Cancelled;
    cancelled.diagnosticCode = L"COPY_WORKER_UNAVAILABLE";
    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        state = state_;
    }
    if (!state) return cancelled;

    auto job = std::make_shared<ClipboardThreadJob>(snapshot);
    if (!job->done) return cancelled;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->workerExited) {
            cancelled.error = SelectionAcquisitionError::PlatformError;
            cancelled.diagnosticCode = L"COPY_WORKER_PLATFORM_UNAVAILABLE";
            return cancelled;
        }
        if (state->stopping || state->pending) return cancelled;
        state->pending = job;
    }
    SetEvent(state->workEvent);

    const DWORD waitBudget = RemainingMilliseconds(
        snapshot.deadlineTick + 1500, 4000);
    const DWORD wait = WaitForSingleObject(job->done, waitBudget);
    if (wait != WAIT_OBJECT_0) {
        job->cancelled.store(true, std::memory_order_release);
        if (WaitForSingleObject(job->done, 750) != WAIT_OBJECT_0) {
            cancelled.diagnosticCode = L"COPY_WORKER_COMPLETION_TIMEOUT";
            return cancelled;
        }
    }
    return std::move(job->result);
}

void ClipboardCopyTransaction::Cancel() {
    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        state = state_;
    }
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->pending) {
        state->pending->cancelled.store(true, std::memory_order_release);
    }
    if (state->current) {
        state->current->cancelled.store(true, std::memory_order_release);
    }
}

void ClipboardCopyTransaction::Shutdown() {
    std::shared_ptr<State> state;
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        if (!state_) return;
        state = std::move(state_);
        worker = std::move(worker_);
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->stopping) return;
        state->stopping = true;
        if (state->pending) {
            state->pending->cancelled.store(true, std::memory_order_release);
        }
        if (state->current) {
            state->current->cancelled.store(true, std::memory_order_release);
        }
    }
    SetEvent(state->stopEvent);
    SetEvent(state->workEvent);
    if (worker.joinable()) {
        if (WaitForSingleObject(state->exitEvent, 1500) == WAIT_OBJECT_0) {
            worker.join();
        } else {
            // A foreign clipboard/OLE provider can outlive our deadline. The
            // worker owns shared state and no UI pointer, so detaching here
            // cannot access the destroyed controller; process exit reclaims it.
            worker.detach();
        }
    }
}

} // namespace selection
