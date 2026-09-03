#pragma once

#include "SelectionTypes.h"

#include <memory>
#include <mutex>
#include <thread>

namespace selection {

// Serializes the process-wide synthetic-copy fallback on a dedicated OLE STA.
// All clipboard/OLE ownership and restoration decisions stay on that thread.
class ClipboardCopyTransaction {
public:
    ClipboardCopyTransaction();
    ~ClipboardCopyTransaction();

    ClipboardCopyTransaction(const ClipboardCopyTransaction&) = delete;
    ClipboardCopyTransaction& operator=(const ClipboardCopyTransaction&) = delete;

    SelectionAcquisitionResult Acquire(
        const SelectionTargetSnapshot& snapshot);
    void Cancel();
    void Shutdown();

private:
    struct State;
    std::mutex lifecycleMutex_;
    std::shared_ptr<State> state_;
    std::thread worker_;
};

} // namespace selection
