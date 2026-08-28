#pragma once

#include <cstddef>
#include <vector>

// Stage 1 pure helpers for batch/OCR queue presentation without HWND.
// Window still owns live queues; these project counts / readiness.

struct DashboardBatchQueueCounts {
    size_t pending = 0;
    size_t active = 0;
    size_t failed = 0;
    size_t finished = 0;
};

inline size_t DashboardBatchQueueTotal(const DashboardBatchQueueCounts& c)
{
    return c.pending + c.active + c.failed + c.finished;
}

// True when there is unfinished work that should keep the rail "busy".
inline bool DashboardBatchQueueIsBusy(const DashboardBatchQueueCounts& c)
{
    return c.pending > 0 || c.active > 0;
}

// Clear-finished is meaningful only when finished > 0 and nothing is active/pending.
inline bool DashboardBatchQueueCanClearFinished(const DashboardBatchQueueCounts& c)
{
    return c.finished > 0 && c.pending == 0 && c.active == 0;
}

// Clamp a zero-based queue index into [0, count) or -1 when empty.
inline int DashboardBatchQueueClampIndex(int index, size_t count)
{
    if (count == 0) return -1;
    if (index < 0) return -1;
    if (static_cast<size_t>(index) >= count) {
        return static_cast<int>(count) - 1;
    }
    return index;
}
