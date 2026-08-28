#include "ocr/ui/dashboard/DashboardHostUtils.h"

#include <atomic>
#include <limits>
#include <vector>

// D-I-3: free Host utils with process-wide counters (multi-TU safe).

uint64_t DashboardNextHostGeneration() {
    static std::atomic<uint64_t> generation{0};
    return ++generation;
}

static std::wstring JoinOcrImageCachePath(const std::wstring& fileName, const SYSTEMTIME& st) {
    std::wstring dir = GetOcrImageDateDir(st);
    return dir + fileName;
}

std::wstring DashboardMakeOcrImageCachePath(const wchar_t* prefix) {
    static std::atomic<unsigned> counter{0};
    SYSTEMTIME st;
    GetLocalTime(&st);
    const unsigned seq = ++counter % 1000;
    const std::wstring name = WideFormatTimedSeqPng(
        prefix,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        seq);
    return JoinOcrImageCachePath(name, st);
}

std::wstring DashboardMakeOcrImportCacheFilePath(const std::wstring& originalName) {
    static std::atomic<unsigned> counter{0};
    std::wstring ext = WideToLower(WideExtensionFromPath(originalName));
    if (ext.empty() || ext.size() > 12 ||
        ext.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos) {
        ext = L".bin";
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    const unsigned seq = ++counter % 1000;
    const std::wstring stem = WideFormatOcrVirtualStem(
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        seq);
    return JoinOcrImageCachePath(stem + ext, st);
}

bool DashboardGetPngEncoderClsid(CLSID& clsid) {
    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) {
        return false;
    }
    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(num, size, encoders) != Gdiplus::Ok) {
        return false;
    }
    for (UINT i = 0; i < num; ++i) {
        if (WideEquals(encoders[i].MimeType, L"image/png")) {
            clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

bool DashboardSaveBitmapAsPng(Gdiplus::Bitmap* bitmap, const std::wstring& destPath) {
    if (!bitmap) return false;
    CLSID pngClsid = {};
    if (!DashboardGetPngEncoderClsid(pngClsid)) return false;
    return bitmap->Save(destPath.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
}

bool DashboardCacheImageForHistory(
    const std::wstring& sourcePath,
    std::wstring& cachedPath,
    bool* created)
{
    if (created) *created = false;
    if (sourcePath.empty() || !PathFileExistsW(sourcePath.c_str())) return false;
    if (DashboardIsPathInOcrImageCache(sourcePath)) {
        cachedPath = sourcePath;
        return true;
    }

    (void)WideToLower(WideExtensionFromPath(sourcePath));

    std::unique_ptr<Gdiplus::Bitmap> bitmap(ImageCodec::LoadBitmapFromFile(sourcePath));
    if (!bitmap) return false;

    CreateDirectoryW(GetOcrImageDir().c_str(), nullptr);
    std::wstring destPath = DashboardMakeOcrImageCachePath(L"ocr_import");
    if (!DashboardSaveBitmapAsPng(bitmap.get(), destPath)) return false;

    cachedPath = destPath;
    if (created) *created = true;
    return true;
}

bool DashboardWriteBytesToFile(const std::wstring& path, const void* data, DWORD size) {
    if (!data || size == 0) return false;
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    const BYTE* cursor = static_cast<const BYTE*>(data);
    DWORD remaining = size;
    bool ok = true;
    while (remaining > 0) {
        DWORD written = 0;
        DWORD chunk = remaining;
        if (!WriteFile(file, cursor, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        cursor += written;
        remaining -= written;
    }
    if (!FlushFileBuffers(file)) ok = false;
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

bool DashboardWriteStreamToFile(IStream* stream, const std::wstring& path) {
    if (!stream) return false;
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    BYTE buffer[64 * 1024];
    bool ok = true;
    bool wroteAny = false;
    for (;;) {
        ULONG read = 0;
        HRESULT hr = stream->Read(buffer, sizeof(buffer), &read);
        if (FAILED(hr)) {
            ok = false;
            break;
        }
        if (read == 0) break;

        DWORD written = 0;
        if (!WriteFile(file, buffer, read, &written, nullptr) || written != read) {
            ok = false;
            break;
        }
        wroteAny = true;
    }
    if (!wroteAny) ok = false;
    if (!FlushFileBuffers(file)) ok = false;
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

bool DashboardWriteStorageMediumToFile(STGMEDIUM& medium, const std::wstring& path) {
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
        SIZE_T size = GlobalSize(medium.hGlobal);
        if (size == 0 || size > static_cast<SIZE_T>((std::numeric_limits<DWORD>::max)())) {
            return false;
        }
        void* data = GlobalLock(medium.hGlobal);
        if (!data) return false;
        bool ok = DashboardWriteBytesToFile(path, data, static_cast<DWORD>(size));
        GlobalUnlock(medium.hGlobal);
        return ok;
    }
    if (medium.tymed == TYMED_ISTREAM && medium.pstm) {
        LARGE_INTEGER zero = {};
        medium.pstm->Seek(zero, STREAM_SEEK_SET, nullptr);
        return DashboardWriteStreamToFile(medium.pstm, path);
    }
    return false;
}
