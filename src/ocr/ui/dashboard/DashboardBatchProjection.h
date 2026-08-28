#pragma once

#include "ocr/batch/BatchOcrTypes.h"

// Stage 1 D-E seed: pure batch status projection helpers (no HWND / queues).

inline bool DashboardBatchStatusIsActive(BatchOcrTaskStatus status) {
    return status == BatchOcrTaskStatus::Pending
        || status == BatchOcrTaskStatus::Recognizing
        || status == BatchOcrTaskStatus::Writing;
}

inline bool DashboardBatchStatusIsTerminal(BatchOcrTaskStatus status) {
    return status == BatchOcrTaskStatus::Completed
        || status == BatchOcrTaskStatus::Failed
        || status == BatchOcrTaskStatus::Canceled;
}

inline bool DashboardBatchStatusIsFailureLike(BatchOcrTaskStatus status) {
    return status == BatchOcrTaskStatus::Failed
        || status == BatchOcrTaskStatus::Canceled;
}

// Generation/stale guard: completion applies only when token matches.
inline bool DashboardBatchCompletionTokenMatches(
    unsigned long long expectedGeneration,
    unsigned long long completionGeneration)
{
    return expectedGeneration == completionGeneration;
}
