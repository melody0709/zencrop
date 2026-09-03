#pragma once

#include <objidl.h>

#include <cstddef>

namespace selection {

// Owns a short-lived, fully materialized IDataObject built from the current
// clipboard object. Capture copies bounded HGLOBAL representations before the
// live clipboard is changed, so restoration never calls back into a stale
// OleGetClipboard proxy.
class ClipboardDataSnapshot {
public:
    ClipboardDataSnapshot() = default;
    ~ClipboardDataSnapshot();

    ClipboardDataSnapshot(const ClipboardDataSnapshot&) = delete;
    ClipboardDataSnapshot& operator=(const ClipboardDataSnapshot&) = delete;

    bool Capture(IDataObject* source);
    void Reset();

    IDataObject* DataObject() const noexcept { return dataObject_; }
    std::size_t FormatCount() const noexcept { return formatCount_; }
    bool Complete() const noexcept { return complete_; }

private:
    IDataObject* dataObject_ = nullptr;
    std::size_t formatCount_ = 0;
    bool complete_ = false;
};

} // namespace selection
