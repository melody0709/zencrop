#pragma once

#include "SelectionTypes.h"

#include <memory>
#include <thread>

namespace selection {

// UIA client owner. The worker is a windowless MTA; clipboard fallback is
// delegated to its own STA transaction owner.
class SelectionTextAcquirer {
public:
    explicit SelectionTextAcquirer(HWND deliveryWindow);
    ~SelectionTextAcquirer();

    SelectionTextAcquirer(const SelectionTextAcquirer&) = delete;
    SelectionTextAcquirer& operator=(const SelectionTextAcquirer&) = delete;

    bool Start(const SelectionTargetSnapshot& snapshot);
    void Cancel();
    void Shutdown();

private:
    struct State;
    std::shared_ptr<State> state_;
    std::thread worker_;
};

} // namespace selection
