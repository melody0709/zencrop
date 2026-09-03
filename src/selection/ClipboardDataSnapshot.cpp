#include "ClipboardDataSnapshot.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace selection {
namespace {

constexpr std::size_t kMaximumSnapshotFormats = 256;
constexpr SIZE_T kMaximumSnapshotFormatBytes = 32ULL * 1024ULL * 1024ULL;
constexpr SIZE_T kMaximumSnapshotTotalBytes = 64ULL * 1024ULL * 1024ULL;

struct MaterializedFormat {
    FORMATETC format = {};
    std::vector<unsigned char> bytes;
};

bool SameFormat(const FORMATETC& left, const FORMATETC& right) {
    return left.cfFormat == right.cfFormat &&
        left.dwAspect == right.dwAspect &&
        left.lindex == right.lindex;
}

class MaterializedFormatEnumerator final : public IEnumFORMATETC {
public:
    explicit MaterializedFormatEnumerator(std::vector<FORMATETC> formats)
        : formats_(std::move(formats)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IEnumFORMATETC) {
            *value = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE Next(
        ULONG requested, FORMATETC* values, ULONG* fetched) override {
        if (!values || (requested != 1 && !fetched)) return E_POINTER;
        ULONG copied = 0;
        while (copied < requested && index_ < formats_.size()) {
            values[copied] = formats_[index_];
            values[copied].ptd = nullptr;
            ++copied;
            ++index_;
        }
        if (fetched) *fetched = copied;
        return copied == requested ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        const std::size_t remaining = formats_.size() - index_;
        const std::size_t skipped = (std::min)(
            remaining, static_cast<std::size_t>(count));
        index_ += skipped;
        return skipped == count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Reset() override {
        index_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto* clone = new (std::nothrow) MaterializedFormatEnumerator(formats_);
        if (!clone) return E_OUTOFMEMORY;
        clone->index_ = index_;
        *value = clone;
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    std::vector<FORMATETC> formats_;
    std::size_t index_ = 0;
};

class MaterializedClipboardDataObject final : public IDataObject {
public:
    explicit MaterializedClipboardDataObject(
        std::vector<MaterializedFormat> formats)
        : formats_(std::move(formats)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDataObject) {
            *value = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetData(
        FORMATETC* requested, STGMEDIUM* medium) override {
        if (!requested || !medium) return E_POINTER;
        *medium = {};
        if ((requested->tymed & TYMED_HGLOBAL) == 0) return DV_E_TYMED;

        const auto match = std::find_if(
            formats_.begin(), formats_.end(),
            [requested](const MaterializedFormat& candidate) {
                return SameFormat(candidate.format, *requested);
            });
        if (match == formats_.end()) return DV_E_FORMATETC;

        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, match->bytes.size());
        if (!data) return E_OUTOFMEMORY;
        void* destination = GlobalLock(data);
        if (!destination) {
            GlobalFree(data);
            return E_OUTOFMEMORY;
        }
        std::memcpy(destination, match->bytes.data(), match->bytes.size());
        GlobalUnlock(data);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = data;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* requested) override {
        if (!requested) return E_POINTER;
        if ((requested->tymed & TYMED_HGLOBAL) == 0) return DV_E_TYMED;
        return std::any_of(
            formats_.begin(), formats_.end(),
            [requested](const MaterializedFormat& candidate) {
                return SameFormat(candidate.format, *requested);
            }) ? S_OK : DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(
        FORMATETC*, FORMATETC* canonical) override {
        if (!canonical) return E_POINTER;
        *canonical = {};
        canonical->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(
        DWORD direction, IEnumFORMATETC** enumerator) override {
        if (!enumerator) return E_POINTER;
        *enumerator = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;

        std::vector<FORMATETC> formats;
        formats.reserve(formats_.size());
        for (const MaterializedFormat& value : formats_) {
            formats.push_back(value.format);
            formats.back().ptd = nullptr;
        }
        auto* value = new (std::nothrow) MaterializedFormatEnumerator(
            std::move(formats));
        if (!value) return E_OUTOFMEMORY;
        *enumerator = value;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(
        FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** enumerator) override {
        if (enumerator) *enumerator = nullptr;
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    std::atomic<ULONG> references_{1};
    std::vector<MaterializedFormat> formats_;
};

void ReleaseEnumeratedFormat(FORMATETC& format) {
    if (format.ptd) {
        CoTaskMemFree(format.ptd);
        format.ptd = nullptr;
    }
}

bool HasFormatId(
    const std::vector<MaterializedFormat>& formats, CLIPFORMAT format) {
    return std::any_of(
        formats.begin(), formats.end(),
        [format](const MaterializedFormat& candidate) {
            return candidate.format.cfFormat == format;
        });
}

bool OpenCurrentClipboard() {
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (OpenClipboard(nullptr)) return true;
        Sleep(5);
    }
    return false;
}

} // namespace

ClipboardDataSnapshot::~ClipboardDataSnapshot() {
    Reset();
}

void ClipboardDataSnapshot::Reset() {
    if (dataObject_) dataObject_->Release();
    dataObject_ = nullptr;
    formatCount_ = 0;
    complete_ = false;
}

bool ClipboardDataSnapshot::Capture(IDataObject* source) {
    Reset();

    bool clipboardEnumerationComplete = false;
    bool hitLimit = false;
    SIZE_T totalBytes = 0;
    std::vector<MaterializedFormat> materialized;
    std::vector<CLIPFORMAT> clipboardFormats;

    const auto saveGlobal = [&](CLIPFORMAT format, HGLOBAL global,
                                DWORD aspect, LONG index) {
        if (!global || HasFormatId(materialized, format)) return false;
        const SIZE_T bytes = GlobalSize(global);
        if (bytes == 0 || bytes > kMaximumSnapshotFormatBytes ||
            totalBytes > kMaximumSnapshotTotalBytes - bytes) {
            hitLimit = true;
            return false;
        }
        const void* sourceBytes = GlobalLock(global);
        if (!sourceBytes) return false;
        MaterializedFormat saved;
        saved.format = {format, nullptr,
            aspect == 0 ? DVASPECT_CONTENT : aspect, index, TYMED_HGLOBAL};
        saved.bytes.resize(bytes);
        std::memcpy(saved.bytes.data(), sourceBytes, bytes);
        GlobalUnlock(global);
        totalBytes += bytes;
        materialized.push_back(std::move(saved));
        return true;
    };

    if (OpenCurrentClipboard()) {
        SetLastError(ERROR_SUCCESS);
        for (UINT format = EnumClipboardFormats(0); format != 0;
             format = EnumClipboardFormats(format)) {
            if (clipboardFormats.size() >= kMaximumSnapshotFormats) {
                hitLimit = true;
                break;
            }
            const CLIPFORMAT clipboardFormat = static_cast<CLIPFORMAT>(format);
            clipboardFormats.push_back(clipboardFormat);
            HANDLE data = GetClipboardData(format);
            if (data) {
                saveGlobal(clipboardFormat, static_cast<HGLOBAL>(data),
                    DVASPECT_CONTENT, -1);
            }
            SetLastError(ERROR_SUCCESS);
        }
        clipboardEnumerationComplete = !hitLimit && GetLastError() == ERROR_SUCCESS;
        CloseClipboard();
    }

    const bool needsOleFallback = source && std::any_of(
        clipboardFormats.begin(), clipboardFormats.end(),
        [&materialized](CLIPFORMAT format) {
            return !HasFormatId(materialized, format);
        });
    if (needsOleFallback) {
        IEnumFORMATETC* enumerator = nullptr;
        if (SUCCEEDED(source->EnumFormatEtc(DATADIR_GET, &enumerator)) &&
            enumerator) {
            for (;;) {
                FORMATETC offered = {};
                ULONG fetched = 0;
                const HRESULT next = enumerator->Next(1, &offered, &fetched);
                if (next == S_FALSE && fetched == 0) break;
                if (FAILED(next) || fetched != 1) {
                    ReleaseEnumeratedFormat(offered);
                    clipboardEnumerationComplete = false;
                    break;
                }
                const bool wanted = std::find(
                    clipboardFormats.begin(), clipboardFormats.end(),
                    offered.cfFormat) != clipboardFormats.end();
                if (!wanted || HasFormatId(materialized, offered.cfFormat)) {
                    ReleaseEnumeratedFormat(offered);
                    continue;
                }

                FORMATETC request = offered;
                request.ptd = nullptr;
                if (request.dwAspect == 0) request.dwAspect = DVASPECT_CONTENT;
                request.tymed = TYMED_HGLOBAL;
                STGMEDIUM medium = {};
                const HRESULT get = source->GetData(&request, &medium);
                if (SUCCEEDED(get) && medium.tymed == TYMED_HGLOBAL &&
                    medium.hGlobal) {
                    saveGlobal(request.cfFormat, medium.hGlobal,
                        request.dwAspect, request.lindex);
                }
                if (medium.tymed != TYMED_NULL) ReleaseStgMedium(&medium);
                ReleaseEnumeratedFormat(offered);
            }
            enumerator->Release();
        }
    }

    if (materialized.empty()) return false;
    const bool allClipboardFormatsCaptured = clipboardEnumerationComplete &&
        !hitLimit && std::all_of(
            clipboardFormats.begin(), clipboardFormats.end(),
            [&materialized](CLIPFORMAT format) {
                return HasFormatId(materialized, format);
            });
    const std::size_t capturedCount = materialized.size();
    auto* object = new (std::nothrow) MaterializedClipboardDataObject(
        std::move(materialized));
    if (!object) return false;
    dataObject_ = object;
    formatCount_ = capturedCount;
    complete_ = allClipboardFormatsCaptured;
    return true;
}

} // namespace selection
