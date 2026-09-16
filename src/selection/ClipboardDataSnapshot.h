#pragma once

#include <windows.h>

#include <objidl.h>

#include <cstddef>

namespace selection {

// Why a capture is not exhaustive. The distinction decides whether the user is
// told anything: a missing GDI-handle representation (CF_BITMAP and friends) is
// invisible when the same image content was captured through an HGLOBAL format,
// whereas a format refused by the capacity caps is a real, bounded loss.
enum class ClipboardSnapshotGap {
    // Every enumerated format was materialized.
    None,
    // Only GDI-handle representations are missing, while the image content
    // itself was captured through an HGLOBAL format.
    RedundantGdiRepresentation,
    // A format was dropped by the per-format/total caps or by the enumeration
    // cap, or its handle claimed a size that could not be read. A handle that
    // reports no size at all is a capability boundary and stays silent.
    ContentDropped,
};

// Pure classification shared by Capture and the contract test.
ClipboardSnapshotGap ClassifySnapshotGap(
    bool capacityDropped, bool unmaterializableDropped,
    bool gdiRepresentationSkipped, bool hglobalImageCaptured);

// True when GetClipboardData() hands back a GDI handle instead of an HGLOBAL for
// this format id (bitmap, palette, enhanced metafile, the owner-display block
// and the GDI-object block). Such a value must never reach GlobalSize,
// GlobalFlags or GlobalLock: ntdll then walks the heap looking for a block
// header at a GDI handle value, and for some handle values that check raises a
// fatal heap-corruption error (0xC0000374) which terminates the process on the
// spot. The failure is fail-fast, so it cannot be caught, and no API can
// validate the handle safely first -- GlobalFlags walks the same heap path and
// dies on the same values. The format id is therefore the only usable signal
// and has to be rejected before the handle is touched.
bool ClipboardFormatCarriesGdiHandle(UINT format);

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
    // Why the last Capture() was not exhaustive. Callers decide whether that is
    // worth telling the user about; a redundant GDI representation is not.
    ClipboardSnapshotGap Gap() const noexcept { return gap_; }

private:
    IDataObject* dataObject_ = nullptr;
    std::size_t formatCount_ = 0;
    ClipboardSnapshotGap gap_ = ClipboardSnapshotGap::None;
};

} // namespace selection
