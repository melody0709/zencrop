#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
#include <tlhelp32.h>
#include "OcrBlockJson.h"

static std::wstring g_dashboardWindowTestBatchOutputRoot;
static std::vector<std::wstring> g_dashboardWindowTestImportFiles;
static bool g_dashboardWindowTestUseFolderImportOptions = false;
static bool g_dashboardWindowTestFolderImportRecursive = true;
static int g_dashboardWindowTestFolderImportMaxDepth = kFolderImportDefaultMaxDepth;
static std::wstring g_dashboardWindowTestFolderImportExcludePatterns;
static bool g_dashboardWindowTestUsePdfImportOptions = false;
static DashboardPdfImportOptions g_dashboardWindowTestPdfImportOptions;
static bool g_dashboardWindowTestDrivePdfOptionsDialog = false;
static bool g_dashboardWindowTestCancelPdfOptionsDialog = false;
static bool g_dashboardWindowTestSavePdfOptionsDialog = false;
static bool g_dashboardWindowTestSavePdfOptionsDialogStayedOpen = false;
static DashboardPdfImportOptions g_dashboardWindowTestDrivenPdfImportOptions;

static bool DashboardWindowTestContains(const std::wstring& text, const std::wstring& needle) {
    return text.find(needle) != std::wstring::npos;
}

static std::wstring DashboardWindowTestHistoryFilePath() {
    return DashboardHistoryRepository::DefaultHistoryPath();
}

static std::wstring DashboardWindowTestDismissedFilePath() {
    return DashboardHistoryRepository::DefaultDismissedPath();
}

static DashboardSourceEditRequest DashboardWindowTestSourceEdit(
    const std::wstring& source,
    const std::wstring& expected,
    size_t occurrence = 0)
{
    std::wstring canonical = DashboardSourceMap::NormalizeLf(source);
    size_t start = 0;
    for (size_t i = 0; i <= occurrence; ++i) {
        start = canonical.find(expected, start);
        if (start == std::wstring::npos) break;
        if (i < occurrence) start += expected.size();
    }
    DashboardSourceEditRequest request;
    request.canonicalSource = L"markdown-body-lf";
    request.offsetUnit = L"utf16-code-unit";
    request.sourceStart = start;
    request.sourceEnd = start == std::wstring::npos ? start : start + expected.size();
    request.revisionSha256 = DashboardSourceMap::RevisionSha256(canonical);
    request.expectedSource = expected;
    return request;
}

static std::wstring DashboardWindowTestPathWithSuffix(std::wstring path, const std::wstring& suffix) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        path.erase(dot);
    }
    path += suffix;
    return path;
}

static int DashboardWindowTestCountFilesRecursive(const std::wstring& root, const std::wstring& pattern) {
    int count = 0;
    std::wstring normalizedRoot = root;
    if (!normalizedRoot.empty() &&
        normalizedRoot.back() != L'\\' &&
        normalizedRoot.back() != L'/') {
        normalizedRoot.push_back(L'\\');
    }

    WIN32_FIND_DATAW match = {};
    HANDLE matchFind = FindFirstFileW((normalizedRoot + pattern).c_str(), &match);
    if (matchFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(match.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++count;
        } while (FindNextFileW(matchFind, &match));
        FindClose(matchFind);
    }

    WIN32_FIND_DATAW child = {};
    HANDLE childFind = FindFirstFileW((normalizedRoot + L"*").c_str(), &child);
    if (childFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (wcscmp(child.cFileName, L".") == 0 || wcscmp(child.cFileName, L"..") == 0) continue;
            count += DashboardWindowTestCountFilesRecursive(normalizedRoot + child.cFileName, pattern);
        } while (FindNextFileW(childFind, &child));
        FindClose(childFind);
    }
    return count;
}

static void DashboardWindowTestClearOverrides() {
    g_dashboardWindowTestBatchOutputRoot.clear();
    g_dashboardWindowTestImportFiles.clear();
    g_dashboardWindowTestUseFolderImportOptions = false;
    g_dashboardWindowTestFolderImportRecursive = true;
    g_dashboardWindowTestFolderImportMaxDepth = kFolderImportDefaultMaxDepth;
    g_dashboardWindowTestFolderImportExcludePatterns.clear();
    g_dashboardWindowTestUsePdfImportOptions = false;
    g_dashboardWindowTestPdfImportOptions = DashboardPdfImportOptions{};
    g_dashboardWindowTestDrivePdfOptionsDialog = false;
    g_dashboardWindowTestCancelPdfOptionsDialog = false;
    g_dashboardWindowTestSavePdfOptionsDialog = false;
    g_dashboardWindowTestSavePdfOptionsDialogStayedOpen = false;
    g_dashboardWindowTestDrivenPdfImportOptions = DashboardPdfImportOptions{};
}

static bool DashboardWindowTestReadUtf8File(const std::wstring& path, std::wstring& text) {
    text.clear();
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    bool ok = bytes.empty() ||
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    bytes.resize(read);

    if (bytes.empty()) return true;
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wideLen <= 0) return false;
    text.resize(wideLen);
    return MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), text.data(), wideLen) == wideLen;
}

static bool DashboardWindowTestWriteUtf8File(const std::wstring& path, const std::wstring& text) {
    int byteCount = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (byteCount < 0) return false;
    std::string bytes(static_cast<size_t>(byteCount), '\0');
    if (byteCount > 0 && WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            bytes.data(), byteCount, nullptr, nullptr) != byteCount) {
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = bytes.empty() || WriteFile(
        file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

static bool DashboardWindowTestReadBinaryFile(const std::wstring& path, std::vector<BYTE>& bytes) {
    bytes.clear();
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
        read == static_cast<DWORD>(bytes.size());
    CloseHandle(file);
    return ok;
}

static void DashboardWindowTestSetError(std::wstring& error, const std::wstring& message) {
    if (error.empty()) error = message;
}

static DWORD DashboardWindowTestCurrentProcessThreadCount() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    const DWORD processId = GetCurrentProcessId();
    DWORD count = 0;
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == processId) ++count;
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

template <typename Predicate>
static bool DashboardWindowTestPumpUntil(HWND hwnd, DWORD timeoutMs, Predicate predicate) {
    DWORD start = GetTickCount();
    MSG msg;
    while (IsWindow(hwnd)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (predicate()) return true;
        if (GetTickCount() - start >= timeoutMs) return false;
        Sleep(10);
    }
    return false;
}

static bool DashboardWindowTestPumpUntilDestroyed(HWND hwnd, DWORD timeoutMs) {
    DWORD start = GetTickCount();
    MSG msg;
    while (IsWindow(hwnd)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetTickCount() - start >= timeoutMs) return false;
        Sleep(10);
    }
    return true;
}

static bool DashboardWindowTestPdfCoverDispatchPrecedesFullRender(std::wstring& error) {
    HANDLE coverDispatched = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE renderEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE releaseRender = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE renderDispatched = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!coverDispatched || !renderEntered || !releaseRender || !renderDispatched) {
        if (coverDispatched) CloseHandle(coverDispatched);
        if (renderEntered) CloseHandle(renderEntered);
        if (releaseRender) CloseHandle(releaseRender);
        if (renderDispatched) CloseHandle(renderDispatched);
        error = L"Failed to create PDF render-stage barrier events.";
        return false;
    }

    std::atomic<bool> payloadsValid{ true };
    std::thread worker([&]() {
        RunDashboardPdfRenderStages(
            []() { return 41; },
            [&](int coverPayload) {
                if (coverPayload != 41) payloadsValid.store(false);
                SetEvent(coverDispatched);
            },
            [&]() {
                if (WaitForSingleObject(coverDispatched, 0) != WAIT_OBJECT_0) {
                    payloadsValid.store(false);
                }
                SetEvent(renderEntered);
                WaitForSingleObject(releaseRender, 5000);
                return 42;
            },
            [&](int renderPayload) {
                if (renderPayload != 42) payloadsValid.store(false);
                SetEvent(renderDispatched);
            });
    });

    bool entered = WaitForSingleObject(renderEntered, 5000) == WAIT_OBJECT_0;
    bool coverWasAlreadyDispatched =
        WaitForSingleObject(coverDispatched, 0) == WAIT_OBJECT_0;
    bool fullRenderStillBlocked =
        WaitForSingleObject(renderDispatched, 0) == WAIT_TIMEOUT;
    SetEvent(releaseRender);
    worker.join();
    bool fullRenderEventuallyDispatched =
        WaitForSingleObject(renderDispatched, 0) == WAIT_OBJECT_0;

    CloseHandle(renderDispatched);
    CloseHandle(releaseRender);
    CloseHandle(renderEntered);
    CloseHandle(coverDispatched);

    if (!entered || !coverWasAlreadyDispatched || !fullRenderStillBlocked ||
        !fullRenderEventuallyDispatched || !payloadsValid.load()) {
        error = L"PDF cover dispatch did not precede the barrier-blocked full render.";
        return false;
    }
    return true;
}

static HDROP DashboardWindowTestCreateDropHandle(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return nullptr;

    struct DashboardWindowTestDropFiles {
        DWORD pFiles;
        POINT pt;
        BOOL fNC;
        BOOL fWide;
    };

    std::wstring payload;
    for (const auto& path : paths) {
        if (path.empty()) continue;
        payload.append(path);
        payload.push_back(L'\0');
    }
    payload.push_back(L'\0');
    if (payload.size() <= 1) return nullptr;

    const SIZE_T payloadBytes = payload.size() * sizeof(wchar_t);
    const SIZE_T totalBytes = sizeof(DashboardWindowTestDropFiles) + payloadBytes;
    HGLOBAL memory = GlobalAlloc(GHND, totalBytes);
    if (!memory) return nullptr;

    auto* drop = static_cast<DashboardWindowTestDropFiles*>(GlobalLock(memory));
    if (!drop) {
        GlobalFree(memory);
        return nullptr;
    }
    drop->pFiles = sizeof(DashboardWindowTestDropFiles);
    drop->fWide = TRUE;
    memcpy(reinterpret_cast<BYTE*>(drop) + sizeof(DashboardWindowTestDropFiles), payload.data(), payloadBytes);
    GlobalUnlock(memory);
    return reinterpret_cast<HDROP>(memory);
}

struct DashboardWindowTestVirtualFile {
    std::wstring name;
    std::vector<BYTE> bytes;
};

class DashboardWindowTestVirtualFileDataObject final : public IDataObject {
public:
    explicit DashboardWindowTestVirtualFileDataObject(
        std::vector<DashboardWindowTestVirtualFile> files,
        std::vector<std::wstring> fileSystemPaths = {})
        : m_files(std::move(files)),
          m_fileSystemPaths(std::move(fileSystemPaths)),
          m_descriptorClip(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW))),
          m_contentsClip(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILECONTENTS))) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppvObject = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (!format || !medium) return E_POINTER;
        ZeroMemory(medium, sizeof(*medium));
        if (IsHdropFormat(*format)) return GetHdropData(medium);
        if (IsDescriptorFormat(*format)) return GetDescriptorData(medium);
        if (IsContentFormat(*format)) return GetContentData(format->lindex, medium);
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (!format) return E_POINTER;
        if (IsHdropFormat(*format)) return S_OK;
        if (IsDescriptorFormat(*format)) return S_OK;
        if (IsContentFormat(*format)) return S_OK;
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* formatOut) override {
        if (formatOut) formatOut->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    bool IsHdropFormat(const FORMATETC& format) const {
        return format.cfFormat == CF_HDROP &&
            (format.tymed & TYMED_HGLOBAL) &&
            format.dwAspect == DVASPECT_CONTENT &&
            !m_fileSystemPaths.empty();
    }

    bool IsDescriptorFormat(const FORMATETC& format) const {
        return format.cfFormat == m_descriptorClip &&
            (format.tymed & TYMED_HGLOBAL) &&
            format.dwAspect == DVASPECT_CONTENT;
    }

    bool IsContentFormat(const FORMATETC& format) const {
        return format.cfFormat == m_contentsClip &&
            (format.tymed & TYMED_HGLOBAL) &&
            format.dwAspect == DVASPECT_CONTENT &&
            format.lindex >= 0 &&
            static_cast<size_t>(format.lindex) < m_files.size();
    }

    HRESULT GetHdropData(STGMEDIUM* medium) const {
        HDROP drop = DashboardWindowTestCreateDropHandle(m_fileSystemPaths);
        if (!drop) return DV_E_FORMATETC;
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = reinterpret_cast<HGLOBAL>(drop);
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT GetDescriptorData(STGMEDIUM* medium) const {
        if (m_files.empty()) return DV_E_FORMATETC;
        SIZE_T bytes = sizeof(FILEGROUPDESCRIPTORW) +
            (m_files.size() - 1) * sizeof(FILEDESCRIPTORW);
        HGLOBAL memory = GlobalAlloc(GHND, bytes);
        if (!memory) return E_OUTOFMEMORY;

        auto* group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(memory));
        if (!group) {
            GlobalFree(memory);
            return E_OUTOFMEMORY;
        }
        group->cItems = static_cast<UINT>(m_files.size());
        for (size_t i = 0; i < m_files.size(); ++i) {
            FILEDESCRIPTORW& descriptor = group->fgd[i];
            descriptor.dwFlags = FD_ATTRIBUTES | FD_FILESIZE;
            descriptor.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            ULONGLONG fileSize = static_cast<ULONGLONG>(m_files[i].bytes.size());
            descriptor.nFileSizeHigh = static_cast<DWORD>(fileSize >> 32);
            descriptor.nFileSizeLow = static_cast<DWORD>(fileSize & 0xffffffff);
            wcsncpy_s(descriptor.cFileName, m_files[i].name.c_str(), _TRUNCATE);
        }
        GlobalUnlock(memory);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = memory;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT GetContentData(LONG index, STGMEDIUM* medium) const {
        if (index < 0 || static_cast<size_t>(index) >= m_files.size()) return DV_E_LINDEX;
        const auto& payload = m_files[static_cast<size_t>(index)].bytes;
        if (payload.empty()) return DV_E_FORMATETC;

        HGLOBAL memory = GlobalAlloc(GHND, payload.size());
        if (!memory) return E_OUTOFMEMORY;
        void* data = GlobalLock(memory);
        if (!data) {
            GlobalFree(memory);
            return E_OUTOFMEMORY;
        }
        memcpy(data, payload.data(), payload.size());
        GlobalUnlock(memory);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = memory;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    LONG m_refCount = 1;
    std::vector<DashboardWindowTestVirtualFile> m_files;
    std::vector<std::wstring> m_fileSystemPaths;
    CLIPFORMAT m_descriptorClip = 0;
    CLIPFORMAT m_contentsClip = 0;
};

static HBITMAP DashboardWindowTestCreateTextBitmap() {
    const int width = 520;
    const int height = 160;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!bitmap) return nullptr;

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    RECT rc = { 0, 0, width, height };
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rc, white);
    DeleteObject(white);

    HFONT font = CreateFontW(
        72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    RECT textRc = { 24, 30, width - 24, height - 24 };
    DrawTextW(dc, L"ZEN CROP 123", -1, &textRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBitmap);
    DeleteDC(dc);
    return bitmap;
}

static bool DashboardWindowTestSaveBitmapAsPng(HBITMAP bitmap, const std::wstring& destPath) {
    if (!bitmap) return false;
    CLSID pngClsid = {};
    if (!GetPngEncoderClsid(pngClsid)) return false;
    Gdiplus::Bitmap image(bitmap, nullptr);
    return image.Save(destPath.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
}

static bool DashboardWindowTestGetEncoderClsid(const wchar_t* mimeType, CLSID& clsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!mimeType || size == 0) return false;

    std::vector<Gdiplus::ImageCodecInfo> encoders(size / sizeof(Gdiplus::ImageCodecInfo));
    Gdiplus::GetImageEncoders(num, size, encoders.data());
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(encoders[i].MimeType, mimeType) == 0) {
            clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

static bool DashboardWindowTestWriteMultiFrameTiff(const std::wstring& destPath) {
    CLSID tiffClsid = {};
    if (!DashboardWindowTestGetEncoderClsid(L"image/tiff", tiffClsid)) return false;

    HBITMAP firstBitmap = DashboardWindowTestCreateTextBitmap();
    HBITMAP secondBitmap = DashboardWindowTestCreateTextBitmap();
    if (!firstBitmap || !secondBitmap) {
        if (firstBitmap) DeleteObject(firstBitmap);
        if (secondBitmap) DeleteObject(secondBitmap);
        return false;
    }

    Gdiplus::Bitmap first(firstBitmap, nullptr);
    Gdiplus::Bitmap second(secondBitmap, nullptr);
    ULONG saveFlag = Gdiplus::EncoderValueMultiFrame;
    Gdiplus::EncoderParameters params = {};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderSaveFlag;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &saveFlag;

    Gdiplus::Status status = first.Save(destPath.c_str(), &tiffClsid, &params);
    if (status == Gdiplus::Ok) {
        saveFlag = Gdiplus::EncoderValueFrameDimensionPage;
        status = first.SaveAdd(&second, &params);
    }
    if (status == Gdiplus::Ok) {
        saveFlag = Gdiplus::EncoderValueFlush;
        status = first.SaveAdd(&params);
    }

    DeleteObject(firstBitmap);
    DeleteObject(secondBitmap);
    return status == Gdiplus::Ok && PathFileExistsW(destPath.c_str());
}

static std::string DashboardWindowTestPdfText(const std::wstring& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L'(' || ch == L')' || ch == L'\\') {
            escaped.push_back('\\');
            escaped.push_back(static_cast<char>(ch));
        } else if (ch >= 32 && ch <= 126) {
            escaped.push_back(static_cast<char>(ch));
        } else {
            escaped.push_back('?');
        }
    }
    return escaped;
}

static bool DashboardWindowTestWriteSimplePdf(
    const std::wstring& path,
    const std::vector<std::wstring>& pageTexts)
{
    if (path.empty() || pageTexts.empty()) return false;

    const int pageCount = (int)pageTexts.size();
    const int firstPageObj = 3;
    const int firstContentObj = firstPageObj + pageCount;
    const int fontObj = firstContentObj + pageCount;
    const int objectCount = fontObj;

    std::string pdf;
    std::vector<size_t> offsets((size_t)objectCount + 1, 0);
    auto append = [&](const std::string& text) {
        pdf += text;
    };
    auto beginObj = [&](int index) {
        offsets[(size_t)index] = pdf.size();
        char header[32] = {};
        sprintf_s(header, "%d 0 obj\n", index);
        append(header);
    };
    auto endObj = [&]() {
        append("endobj\n");
    };

    append("%PDF-1.4\n");
    beginObj(1);
    append("<< /Type /Catalog /Pages 2 0 R >>\n");
    endObj();

    beginObj(2);
    append("<< /Type /Pages /Kids [");
    for (int i = 0; i < pageCount; i++) {
        char kid[32] = {};
        sprintf_s(kid, "%d 0 R", firstPageObj + i);
        if (i > 0) append(" ");
        append(kid);
    }
    char pagesTail[64] = {};
    sprintf_s(pagesTail, "] /Count %d >>\n", pageCount);
    append(pagesTail);
    endObj();

    for (int i = 0; i < pageCount; i++) {
        beginObj(firstPageObj + i);
        char pageLine[192] = {};
        sprintf_s(
            pageLine,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            "/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>\n",
            fontObj,
            firstContentObj + i);
        append(pageLine);
        endObj();
    }

    for (int i = 0; i < pageCount; i++) {
        std::string content =
            "BT\n"
            "/F1 54 Tf\n"
            "72 640 Td\n"
            "(" + DashboardWindowTestPdfText(pageTexts[(size_t)i]) + ") Tj\n"
            "ET\n";
        beginObj(firstContentObj + i);
        char lengthLine[64] = {};
        sprintf_s(lengthLine, "<< /Length %zu >>\nstream\n", content.size());
        append(lengthLine);
        append(content);
        append("endstream\n");
        endObj();
    }

    beginObj(fontObj);
    append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n");
    endObj();

    size_t xrefOffset = pdf.size();
    char xrefHeader[64] = {};
    sprintf_s(xrefHeader, "xref\n0 %d\n", objectCount + 1);
    append(xrefHeader);
    append("0000000000 65535 f \n");
    for (int i = 1; i <= objectCount; i++) {
        char row[32] = {};
        sprintf_s(row, "%010zu 00000 n \n", offsets[(size_t)i]);
        append(row);
    }
    char trailer[128] = {};
    sprintf_s(
        trailer,
        "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n",
        objectCount + 1,
        xrefOffset);
    append(trailer);

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) return false;
    size_t written = fwrite(pdf.data(), 1, pdf.size(), file);
    fclose(file);
    return written == pdf.size();
}

static bool DashboardWindowTestWriteSimplePdf(const std::wstring& path) {
    return DashboardWindowTestWriteSimplePdf(path, { L"ZEN CROP 123" });
}

static bool DashboardWindowTestLooksLikeExpectedOcr(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    return lower.find(L"zen") != std::wstring::npos ||
        lower.find(L"crop") != std::wstring::npos ||
        lower.find(L"123") != std::wstring::npos ||
        lower.find(L"456") != std::wstring::npos ||
        lower.find(L"789") != std::wstring::npos;
}

static COLORREF DashboardWindowTestDibPixelToColorRef(DWORD pixel) {
    BYTE b = static_cast<BYTE>(pixel & 0xff);
    BYTE g = static_cast<BYTE>((pixel >> 8) & 0xff);
    BYTE r = static_cast<BYTE>((pixel >> 16) & 0xff);
    return RGB(r, g, b);
}

static bool DashboardWindowTestCaptureClientPixels(
    HWND hwnd,
    int& width,
    int& height,
    std::vector<COLORREF>& pixels)
{
    width = 0;
    height = 0;
    pixels.clear();
    if (!hwnd || !IsWindow(hwnd)) return false;

    RECT rc = {};
    GetClientRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC sourceDc = GetDC(hwnd);
    if (!sourceDc) return false;

    HDC memDc = CreateCompatibleDC(sourceDc);
    HBITMAP bitmap = CreateDIBSection(sourceDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memDc || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (memDc) DeleteDC(memDc);
        ReleaseDC(hwnd, sourceDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
    BOOL copied = BitBlt(
        memDc,
        0,
        0,
        width,
        height,
        sourceDc,
        0,
        0,
        SRCCOPY);
    GdiFlush();

    pixels.resize((size_t)width * (size_t)height);
    if (copied) {
        const DWORD* raw = static_cast<const DWORD*>(bits);
        for (size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = DashboardWindowTestDibPixelToColorRef(raw[i]);
        }
    }

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(hwnd, sourceDc);
    return copied != FALSE;
}

static bool DashboardWindowTestColorNear(COLORREF actual, COLORREF expected, int tolerance) {
    return abs((int)GetRValue(actual) - (int)GetRValue(expected)) <= tolerance &&
        abs((int)GetGValue(actual) - (int)GetGValue(expected)) <= tolerance &&
        abs((int)GetBValue(actual) - (int)GetBValue(expected)) <= tolerance;
}

static bool DashboardWindowTestIsTrackerPixel(COLORREF pixel) {
    constexpr int kTolerance = 30;
    COLORREF swappedAccent = RGB(GetBValue(Theme::accent), GetGValue(Theme::accent), GetRValue(Theme::accent));
    COLORREF swappedAccentHover = RGB(GetBValue(Theme::accentHover), GetGValue(Theme::accentHover), GetRValue(Theme::accentHover));
    return DashboardWindowTestColorNear(pixel, Theme::accent, kTolerance) ||
        DashboardWindowTestColorNear(pixel, Theme::accentHover, kTolerance) ||
        DashboardWindowTestColorNear(pixel, swappedAccent, kTolerance) ||
        DashboardWindowTestColorNear(pixel, swappedAccentHover, kTolerance);
}

static bool DashboardWindowTestHasLongTrackerStripe(
    const std::vector<COLORREF>& pixels,
    int width,
    int height,
    int splitterLeftX,
    int splitterWidth,
    int topY,
    int bottomY)
{
    if (pixels.empty() || width <= 0 || height <= 0) return false;
    int stripeLeft = max(0, splitterLeftX - 3);
    int stripeRight = min(width, splitterLeftX + max(2, splitterWidth) + 3);
    int scanTop = max(0, min(topY, height - 1));
    int scanBottom = max(scanTop + 1, min(bottomY, height));
    int requiredRun = max(72, (scanBottom - scanTop) / 2);

    for (int x = stripeLeft; x < stripeRight; ++x) {
        int run = 0;
        for (int y = scanTop; y < scanBottom; ++y) {
            COLORREF pixel = pixels[(size_t)y * (size_t)width + (size_t)x];
            if (DashboardWindowTestIsTrackerPixel(pixel)) {
                ++run;
                if (run >= requiredRun) return true;
            } else {
                run = 0;
            }
        }
    }
    return false;
}

bool OcrDashboardWindow::RunWindowContractForTests(
    const std::wstring& pdfPath,
    const std::wstring& outputRoot,
    std::wstring& error)
{
    error.clear();
    if (!DashboardWindowTestPdfCoverDispatchPrecedesFullRender(error)) {
        return false;
    }
    wchar_t fullPdfPath[MAX_PATH] = {};
    DWORD pdfFullLen = GetFullPathNameW(pdfPath.c_str(), MAX_PATH, fullPdfPath, nullptr);
    std::wstring resolvedPdfPath = (pdfFullLen > 0 && pdfFullLen < MAX_PATH) ? std::wstring(fullPdfPath) : pdfPath;
    if (resolvedPdfPath.empty() || !PathFileExistsW(resolvedPdfPath.c_str())) {
        error = L"PDF fixture does not exist.";
        return false;
    }

    wchar_t fullOutputRoot[MAX_PATH] = {};
    DWORD fullLen = GetFullPathNameW(outputRoot.c_str(), MAX_PATH, fullOutputRoot, nullptr);
    std::wstring runRoot = (fullLen > 0 && fullLen < MAX_PATH) ? std::wstring(fullOutputRoot) : outputRoot;
    if (!runRoot.empty() && (runRoot.back() == L'\\' || runRoot.back() == L'/')) {
        runRoot.pop_back();
    }
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        error = L"Failed to create Dashboard window contract base output root.";
        return false;
    }
    wchar_t suffix[128] = {};
    swprintf_s(suffix, L"\\dashboard_window_contract_%lu_%llu",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()));
    runRoot += suffix;
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        error = L"Failed to create Dashboard window contract output root.";
        return false;
    }

    BatchOcrController controller;
    BatchOcrPdfJob pdfJob;
    std::wstring setupError;
    if (!controller.CreatePdfJob(resolvedPdfPath, runRoot, pdfJob, setupError)) {
        error = setupError.empty() ? L"Failed to create PDF job." : setupError;
        return false;
    }
    pdfJob.pageRange = L"1,2";
    pdfJob.pdfRenderDpi = 144;

    PdfRenderSettings renderSettings;
    renderSettings.pageRange = pdfJob.pageRange;
    renderSettings.dpi = pdfJob.pdfRenderDpi;
    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(
        pdfJob.sourcePath,
        pdfJob.pageImagesDir,
        renderSettings);
    if (!render.success || render.pages.empty()) {
        error = render.error.empty() ? L"Failed to render PDF fixture page images." : render.error;
        return false;
    }

    std::vector<int> pageIndices;
    pageIndices.reserve(render.pages.size());
    for (const auto& page : render.pages) {
        if (page.error.empty() && !page.imagePath.empty() && PathFileExistsW(page.imagePath.c_str())) {
            pageIndices.push_back(page.pageIndex);
        }
    }
    if (pageIndices.empty()) {
        error = L"PDF fixture rendered no usable page images.";
        return false;
    }

    pdfJob.sourcePageCount = render.pageCount;
    if (!controller.InitializePdfPages(pdfJob, pageIndices, setupError)) {
        error = setupError.empty() ? L"Failed to initialize PDF pages." : setupError;
        return false;
    }
    for (auto& page : pdfJob.pages) {
        auto rendered = std::find_if(render.pages.begin(), render.pages.end(),
            [&](const PdfRenderedPage& candidate) {
                return candidate.pageIndex == page.pageIndex;
            });
        if (rendered != render.pages.end()) {
            page.width = rendered->width;
            page.height = rendered->height;
            page.scaledDown = rendered->scaledDown;
            page.skippedTooLarge = rendered->skippedTooLarge;
        }
    }

    const int selectedPageIndex = pdfJob.pages.front().pageIndex;
    const int selectedVisiblePageIndex = selectedPageIndex == 1 ? 0 : selectedPageIndex;
    for (int pageIndex : pageIndices) {
        std::wstring markdown = pageIndex == selectedPageIndex
            ? L"# Dashboard window contract page\n\nWindow contract markdown page."
            : (L"# Dashboard window contract page " + std::to_wstring(pageIndex) +
                L"\n\nWindow contract markdown page " + std::to_wstring(pageIndex) + L".");
        std::wstring plainText = pageIndex == selectedPageIndex
            ? L"Window contract plain page."
            : (L"Window contract plain page " + std::to_wstring(pageIndex) + L".");
        OcrLayoutBlock pdfBlock;
        pdfBlock.id = L"page_1:pdf_stub";
        pdfBlock.pageIndex = 0;
        pdfBlock.order = 1;
        pdfBlock.label = L"text";
        pdfBlock.content = L"PDF block page " + std::to_wstring(pageIndex);
        pdfBlock.bbox = RECT{ 24, 24, 240, 72 };
        pdfBlock.polygon = {
            { 24.0f, 24.0f },
            { 240.0f, 24.0f },
            { 240.0f, 72.0f },
            { 24.0f, 72.0f }
        };
        BatchOcrWriteResult pageWrite = BatchOcrWriter::WritePdfPageSuccess(
            pdfJob,
            pageIndex,
            markdown,
            plainText,
            L"window_contract",
            321,
            { pdfBlock });
        if (!pageWrite.success) {
            error = pageWrite.error.empty() ? L"Failed to write PDF page success." : pageWrite.error;
            return false;
        }
    }
    BatchOcrWriteResult finalWrite = BatchOcrWriter::FinalizePdfJob(pdfJob);
    if (!finalWrite.success) {
        error = finalWrite.error.empty() ? L"Failed to finalize PDF job." : finalWrite.error;
        return false;
    }

    std::wstring testPositionPath = GetWindowPositionFilePath();
    std::wstring testDismissedPath = DashboardWindowTestDismissedFilePath();
    DeleteFileW(testPositionPath.c_str());
    DeleteFileW(testDismissedPath.c_str());

    OcrDashboardWindow* window = new OcrDashboardWindow();
    s_instance = window;
    if (!window->Create(nullptr)) {
        s_instance = nullptr;
        delete window;
        error = L"Failed to create Dashboard window.";
        return false;
    }
    HWND hwnd = window->m_hwnd;
    ShowWindow(hwnd, SW_HIDE);

    auto fail = [&](const std::wstring& message) {
        DashboardWindowTestSetError(error, message);
        DashboardWindowTestClearOverrides();
        DeleteFileW(testPositionPath.c_str());
        DeleteFileW(testDismissedPath.c_str());
        if (IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
        return false;
    };

    if (!window->m_splitterTracker || !IsWindow(window->m_splitterTracker)) {
        return fail(L"Dashboard splitter tracker window was not created.");
    }
    LONG_PTR trackerStyle = GetWindowLongPtrW(window->m_splitterTracker, GWL_STYLE);
    LONG_PTR trackerExStyle = GetWindowLongPtrW(window->m_splitterTracker, GWL_EXSTYLE);
    if (GetParent(window->m_splitterTracker) != hwnd ||
        (trackerStyle & WS_CHILD) == 0 ||
        (trackerStyle & WS_POPUP) != 0 ||
        (trackerExStyle & WS_EX_LAYERED) != 0 ||
        (trackerExStyle & WS_EX_TOPMOST) != 0 ||
        IsWindowVisible(window->m_splitterTracker)) {
        return fail(L"Dashboard splitter tracker must be a hidden child overlay, not a layered top-level popup.");
    }

    const RECT splitterRects[3] = {
        window->m_resolvedLayout.sourceSplitterRc,
        window->m_resolvedLayout.resultSplitterRc,
        window->m_resolvedLayout.translationSplitterRc
    };
    const bool splitterVisible[3] = {
        window->m_resolvedLayout.sourceVisible,
        window->m_resolvedLayout.resultVisible,
        window->m_resolvedLayout.translationVisible
    };
    for (int i = 0; i < 3; ++i) {
        HWND hitTarget = window->m_splitterHitTargets[i];
        if (!hitTarget || !IsWindow(hitTarget) || GetParent(hitTarget) != hwnd) {
            return fail(L"Dashboard splitter hit target child window is missing.");
        }
        LONG_PTR hitStyle = GetWindowLongPtrW(hitTarget, GWL_STYLE);
        LONG_PTR hitExStyle = GetWindowLongPtrW(hitTarget, GWL_EXSTYLE);
        if ((hitStyle & WS_CHILD) == 0 || (hitStyle & WS_POPUP) != 0 ||
            (hitExStyle & WS_EX_TRANSPARENT) == 0 ||
            (hitExStyle & WS_EX_NOACTIVATE) == 0) {
            return fail(L"Dashboard splitter hit target must be a transparent, non-activating child.");
        }

        const bool visible = IsWindowVisible(hitTarget) != FALSE;
        if (visible != splitterVisible[i]) {
            return fail(L"Dashboard splitter hit target visibility did not follow pane visibility.");
        }
        if (!visible) continue;

        RECT actualRc = {};
        if (!GetWindowRect(hitTarget, &actualRc)) {
            return fail(L"Dashboard splitter hit target geometry could not be read.");
        }
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&actualRc), 2);
        RECT expectedRc = window->GetSplitterHitRect(splitterRects[i]);
        if (actualRc.right - actualRc.left != window->m_metrics.splitterHitW ||
            actualRc.right - actualRc.left != expectedRc.right - expectedRc.left ||
            actualRc.top != expectedRc.top || actualRc.bottom != expectedRc.bottom) {
            return fail(L"Dashboard splitter hit target did not preserve the fixed legacy hit width.");
        }
    }
    if (!window->m_outputFolderBtn || !IsWindow(window->m_outputFolderBtn)) {
        return fail(L"Dashboard preferred output folder button is missing.");
    }
    std::wstring chosenPreferredRoot = runRoot + L"_chosen_preferred_output";
    if (!BatchOcrWriter::EnsureDirectory(chosenPreferredRoot)) {
        return fail(L"Failed to create chosen preferred output root.");
    }
    g_dashboardWindowTestBatchOutputRoot = chosenPreferredRoot;
    SendMessageW(
        hwnd,
        WM_COMMAND,
        MAKEWPARAM(ID_DASH_OUTPUT_FOLDER, BN_CLICKED),
        reinterpret_cast<LPARAM>(window->m_outputFolderBtn));
    DashboardWindowTestClearOverrides();
    if (NormalizePathForCompare(DashboardStatePreferredBatchOutputRoot(window->m_dashboardState)) != NormalizePathForCompare(chosenPreferredRoot) ||
        NormalizePathForCompare(window->GetCurrentOutputFolder()) != NormalizePathForCompare(chosenPreferredRoot) ||
        (window->m_openOutputBtn && !IsWindowEnabled(window->m_openOutputBtn))) {
        return fail(L"Dashboard preferred output folder button did not persist and expose the chosen folder.");
    }
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        L"",
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    window->SaveBatchSessionState();

    auto resetWindowState = [&]() {
        window->m_history.model.items.clear();
        window->m_historyRanges.clear();
        DashboardStateSetVisibleHistoryIndices(window->m_dashboardState, {});
        window->m_batch.batchTasks.clear();
        window->m_batch.failedBatchJobs.clear();
        window->m_batch.failedPdfJobs.clear();
        window->m_batch.failedPdfPages.clear();
        window->m_batch.activePdfJobs.clear();
        window->m_batch.pdfRenderTasks.clear();
        window->m_dashboardState.expandedPdfJobKeys.clear();
        window->m_dashboardState.pausedPdfJobKeys.clear();
        window->m_dashboardState.pausedPdfPageKeys.clear();
        window->m_batch.dropQueue.clear();
        DashboardStateSyncBatchProgress(window->m_dashboardState, false, 0, 0, 0);
        window->m_closeAfterCancel = false;
        DashboardStateSetBatchPaused(window->m_dashboardState, false);
        window->m_activeWorkTimerRunning = false;
        window->m_batch.externalOcrJobs.clear();
        window->m_batch.externalOcrRuntimes.clear();
        window->RefreshExternalOcrPresentation();
        window->m_hasCachedActivityProjection = false;
        window->m_cachedSourceOverlays.clear();
        window->m_cachedSourceHeaderActivity.clear();
        window->m_sourcePanelHasActivityBadge = false;
        window->m_sourcePanelHasErrorBadge = false;
        window->m_activeWorkSummary.clear();
        window->m_activeWorkSummaryUntilTick = 0;
        DashboardStateSetActiveWorkHadFailure(window->m_dashboardState, false);
        KillTimer(window->m_hwnd, TIMER_ACTIVE_WORK);
        window->ClearImageTaskSelection();
        window->ClearPdfSelection();
        window->LoadImageIntoCanvas(L"", false);
        window->ApplyFilter(L"");
        window->UpdateCloseCancelButtonText();
    };
    std::wstring sourcePageImage = pdfJob.pages.front().sourceImagePath;

    // Canvas activation is idempotent for the same stable Source, but not for
    // a different Source that happens to resolve to the same file path. Keep
    // this focused contract before the longer Import scenarios so a later,
    // unrelated workflow failure cannot hide the activation regression.
    auto setManualCanvas = [&](float zoom, float panX, float panY) {
        window->m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
        window->m_dashboardState.canvasView.zoom = zoom;
        window->m_dashboardState.canvasView.panX = panX;
        window->m_dashboardState.canvasView.panY = panY;
    };
    auto manualCanvasEquals = [&](float zoom, float panX, float panY) {
        return window->m_dashboardState.canvasView.viewMode == ImageViewMode::Manual &&
            window->m_dashboardState.canvasView.zoom == zoom &&
            window->m_dashboardState.canvasView.panX == panX &&
            window->m_dashboardState.canvasView.panY == panY;
    };

    resetWindowState();
    DashboardBatchTaskItem canvasImageA;
    canvasImageA.job.sourceInstanceId = L"{10101010-1010-4010-8010-101010101010}";
    canvasImageA.job.sourcePath = sourcePageImage;
    canvasImageA.status = BatchOcrTaskStatus::Completed;
    DashboardBatchTaskItem canvasImageB = canvasImageA;
    canvasImageB.job.sourceInstanceId = L"{20202020-2020-4020-8020-202020202020}";
    window->m_batch.batchTasks = { canvasImageA, canvasImageB };
    window->ActivateSourceRailImageTask(0);
    setManualCanvas(2.125f, 17.0f, -11.0f);
    window->ActivateSourceRailImageTask(0);
    if (!window->m_gdiplusImage || !manualCanvasEquals(2.125f, 17.0f, -11.0f)) {
        return fail(L"Repeated Image Source activation did not preserve the Canvas bitmap/view.");
    }
    window->ActivateSourceRailImageTask(1);
    if (window->m_dashboardState.canvasView.viewMode != ImageViewMode::Fit) {
        return fail(L"Different Image Sources sharing one path incorrectly reused Canvas identity.");
    }

    resetWindowState();
    OcrDashboardHistoryItem canvasHistoryA;
    canvasHistoryA.timestamp = L"2026-07-17 18:00:00";
    canvasHistoryA.imagePath = sourcePageImage;
    canvasHistoryA.text = L"Canvas history A";
    OcrDashboardHistoryItem canvasHistoryB = canvasHistoryA;
    canvasHistoryB.timestamp = L"2026-07-17 18:00:01";
    canvasHistoryB.text = L"Canvas history B";
    window->m_history.model.items = { canvasHistoryA, canvasHistoryB };
    window->SelectHistoryItem(0, false);
    setManualCanvas(1.875f, -19.0f, 29.0f);
    window->SelectHistoryItem(0, false);
    if (!window->m_gdiplusImage || !manualCanvasEquals(1.875f, -19.0f, 29.0f)) {
        return fail(L"Repeated History activation did not preserve the Canvas bitmap/view.");
    }
    window->SelectHistoryItem(1, false);
    if (window->m_dashboardState.canvasView.viewMode != ImageViewMode::Fit) {
        return fail(L"Different History Sources sharing one path incorrectly reused Canvas identity.");
    }

    resetWindowState();
    window->m_batch.activePdfJobs = { pdfJob };
    window->ActivateSourceRailPdfItem(0, 0, true);
    setManualCanvas(1.625f, 31.0f, -7.0f);
    window->ActivateSourceRailPdfItem(0, 0, true);
    if (!window->m_gdiplusImage || !manualCanvasEquals(1.625f, 31.0f, -7.0f)) {
        return fail(L"Repeated PDF root activation did not preserve the Canvas bitmap/view.");
    }
    window->TogglePdfJobExpanded(pdfJob);
    if (!manualCanvasEquals(1.625f, 31.0f, -7.0f)) {
        return fail(L"PDF disclosure changed the unchanged root Canvas view.");
    }
    auto canvasPdfPage = std::find_if(pdfJob.pages.begin(), pdfJob.pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
    if (canvasPdfPage != pdfJob.pages.end()) {
        window->ActivateSourceRailPdfItem(0, canvasPdfPage->pageIndex, false);
        if (window->m_dashboardState.canvasView.viewMode != ImageViewMode::Fit) {
            return fail(L"Switching from the PDF root to another page did not refresh the Canvas.");
        }
    }
    if (SendMessageW(window->m_imageArea, WM_ERASEBKGND, 0, 0) != 1) {
        return fail(L"ImageArea did not suppress background erase despite complete double-buffered paint.");
    }

    resetWindowState();

    // Source Rail date sort is a view-only projection: mixed roots share one
    // timeline, unknown dates remain last, and PDF children stay with their
    // parent in page order regardless of sort direction.
    DashboardBatchTaskItem sortedImage;
    sortedImage.job.index = 1;
    sortedImage.job.baseName = L"sorted-image";
    sortedImage.job.sourcePath = sourcePageImage;
    sortedImage.job.createdAt = L"2026-07-16 10:30:45";
    sortedImage.status = BatchOcrTaskStatus::Completed;
    sortedImage.elapsedMs = 23000;
    window->m_batch.batchTasks.push_back(sortedImage);

    BatchOcrPdfJob sortedPdf;
    sortedPdf.index = 1;
    sortedPdf.baseName = L"sorted-pdf";
    sortedPdf.sourcePath = resolvedPdfPath;
    sortedPdf.outputDir = runRoot + L"\\sorted_pdf";
    sortedPdf.createdAt = L"2026-07-14T03:00:00Z";
    sortedPdf.thumbnailPath = sourcePageImage;
    sortedPdf.status = BatchOcrTaskStatus::Completed;
    sortedPdf.elapsedMs = 12600;
    BatchOcrPdfPageJob sortedPageOne;
    sortedPageOne.pageIndex = 1;
    sortedPageOne.sourceImagePath = sourcePageImage;
    sortedPageOne.status = BatchOcrTaskStatus::Completed;
    BatchOcrPdfPageJob sortedPageTwo = sortedPageOne;
    sortedPageTwo.pageIndex = 2;
    sortedPdf.pages = { sortedPageOne, sortedPageTwo };
    window->m_batch.activePdfJobs.push_back(sortedPdf);
    window->SetPdfJobExpanded(window->m_batch.activePdfJobs.front(), true);

    OcrDashboardHistoryItem sortedCapture;
    sortedCapture.originKind = L"Capture";
    sortedCapture.imagePath = runRoot + L"\\sorted_capture.png";
    sortedCapture.timestamp = L"2026-07-15 09:15:00.250";
    sortedCapture.elapsedMs = 922;
    sortedCapture.text = L"sorted capture";
    window->m_history.model.items.push_back(sortedCapture);
    OcrDashboardHistoryItem unknownDate = sortedCapture;
    unknownDate.imagePath = runRoot + L"\\unknown_date.png";
    unknownDate.timestamp.clear();
    unknownDate.text = L"unknown date";
    window->m_history.model.items.push_back(unknownDate);
    window->ApplyFilter(L"");

    auto sortedRows = window->BuildSourceRailViewRows();
    if (sortedRows.size() != 5 ||
        sortedRows[0].selection.kind != DashboardSourceRailRowKind::ImageTask ||
        sortedRows[1].selection.kind != DashboardSourceRailRowKind::History ||
        sortedRows[2].selection.kind != DashboardSourceRailRowKind::PdfJob ||
        sortedRows[3].selection.kind != DashboardSourceRailRowKind::PdfPage ||
        sortedRows[3].selection.pageIndex != 2 ||
        sortedRows[4].selection.kind != DashboardSourceRailRowKind::History ||
        sortedRows[4].hasSortTime ||
        !sortedRows[0].statusText.empty() ||
        sortedRows[0].metaText.find(L"Done") != std::wstring::npos) {
        return fail(L"Source Rail newest-first projection did not preserve mixed root and PDF child order.");
    }
    DashboardStateSetSourceSortNewestFirst(window->m_dashboardState, false);
    sortedRows = window->BuildSourceRailViewRows();
    if (sortedRows.size() != 5 ||
        sortedRows[0].selection.kind != DashboardSourceRailRowKind::PdfJob ||
        sortedRows[1].selection.kind != DashboardSourceRailRowKind::PdfPage ||
        sortedRows[1].selection.pageIndex != 2 ||
        sortedRows[4].hasSortTime) {
        return fail(L"Source Rail oldest-first projection did not keep PDF children attached or unknown dates last.");
    }
    DashboardStateSetSourceSortNewestFirst(window->m_dashboardState, true);
    RECT sourceThumbFixture = { 0, 0, window->m_metrics.sourceW, window->m_metrics.sourceListItemH };
    RECT sourceThumb = window->GetSourceRailThumbnailRect(sourceThumbFixture);
    RECT sourceHeaderRc = {};
    RECT sourceListRc = {};
    GetWindowRect(window->m_sourceHeaderText, &sourceHeaderRc);
    GetWindowRect(window->m_sourceList, &sourceListRc);
    LOGFONTW sourceTitleFont = {};
    LOGFONTW sourceMetaFont = {};
    const bool hasSourceTitleFont = window->m_hSourceTitleFont &&
        GetObjectW(window->m_hSourceTitleFont, sizeof(sourceTitleFont), &sourceTitleFont) != 0;
    const bool hasSourceMetaFont = window->m_hSourceMetaFont &&
        GetObjectW(window->m_hSourceMetaFont, sizeof(sourceMetaFont), &sourceMetaFont) != 0;
    const int sourceTitleFontHeight = sourceTitleFont.lfHeight < 0
        ? -sourceTitleFont.lfHeight : sourceTitleFont.lfHeight;
    const int sourceMetaFontHeight = sourceMetaFont.lfHeight < 0
        ? -sourceMetaFont.lfHeight : sourceMetaFont.lfHeight;
    if (!window->m_sourceHeaderText || !window->m_sourceSortBtn) {
        return fail(L"Source Rail fixed header controls are missing.");
    }
    if (window->m_metrics.sourceListItemH != window->Scale(84) ||
        window->m_metrics.sourceThumbH != window->Scale(72) ||
        sourceThumb.top != window->m_metrics.sourceItemPadY ||
        sourceThumb.right - sourceThumb.left != window->m_metrics.sourceThumbH) {
        return fail(L"Source Rail 84/72 thumbnail geometry contract failed.");
    }
    if (window->m_metrics.sourceTitleLineH != window->Scale(22) ||
        window->m_metrics.sourceMetaLineH != window->Scale(20) ||
        window->m_metrics.sourceTitleToMetaGap != window->Scale(6) ||
        window->m_metrics.sourceMetaLineGap != window->Scale(4)) {
        return fail(L"Source Rail title and metadata line-spacing contract failed: "
            L"actual=" + std::to_wstring(window->m_metrics.sourceTitleLineH) + L"," +
            std::to_wstring(window->m_metrics.sourceMetaLineH) + L"," +
            std::to_wstring(window->m_metrics.sourceTitleToMetaGap) + L"," +
            std::to_wstring(window->m_metrics.sourceMetaLineGap) + L" expected=" +
            std::to_wstring(window->Scale(22)) + L"," +
            std::to_wstring(window->Scale(20)) + L"," +
            std::to_wstring(window->Scale(6)) + L"," +
            std::to_wstring(window->Scale(4)) + L".");
    }
    {
        const UINT dpiBeforeFontMetricMatrix = window->m_dpi;
        const UINT fontMetricDpiMatrix[] = { 96, 120, 144, 168, 192 };
        for (UINT matrixDpi : fontMetricDpiMatrix) {
            window->UpdateDpi(matrixDpi);
            if (!window->m_sourceTitleFontMetrics.IsUsable() ||
                !window->m_sourceMetaFontMetrics.IsUsable()) {
                return fail(L"Source Rail font-metrics cache did not rebuild for the DPI matrix.");
            }

            const int rootH = window->m_metrics.sourceListItemH;
            const int titleH = window->m_metrics.sourceTitleLineH;
            const int metaH = window->m_metrics.sourceMetaLineH;
            const int titleGap = window->m_metrics.sourceTitleToMetaGap;
            const int metaGap = window->m_metrics.sourceMetaLineGap;
            const int textBlockH = titleH + titleGap + metaH + metaGap + metaH;
            const int textTop = max(window->m_metrics.sourceItemPadY, (rootH - textBlockH) / 2);
            const auto measuredLineFitsRoot = [rootH](int nominalTop, int nominalH, int measuredH) {
                const int drawH = max(nominalH, measuredH);
                const int drawTop = (nominalTop + nominalTop + nominalH - drawH) / 2;
                return drawTop >= 0 && drawTop + drawH <= rootH;
            };
            const int statusTop = textTop + titleH + titleGap;
            const int metaTop = statusTop + metaH + metaGap;
            if (!measuredLineFitsRoot(textTop, titleH, window->m_sourceTitleFontMetrics.height) ||
                !measuredLineFitsRoot(statusTop, metaH, window->m_sourceMetaFontMetrics.height) ||
                !measuredLineFitsRoot(metaTop, metaH, window->m_sourceMetaFontMetrics.height)) {
                return fail(L"Source Rail measured text cells escaped an 84-design-pixel root card in the DPI matrix.");
            }
        }
        window->UpdateDpi(dpiBeforeFontMetricMatrix);
        window->LayoutControls();
    }
    if (!hasSourceTitleFont || !hasSourceMetaFont ||
        sourceTitleFont.lfWeight < FW_MEDIUM ||
        sourceMetaFontHeight >= sourceTitleFontHeight) {
        return fail(L"Source Rail Medium title or smaller metadata font contract failed.");
    }
    if (sourceListRc.top < sourceHeaderRc.bottom) {
        return fail(L"Source Rail list overlaps its fixed header.");
    }
    {
        RECT headerClient = {};
        RECT sortClient = {};
        GetWindowRect(window->m_sourceHeaderText, &headerClient);
        GetWindowRect(window->m_sourceSortBtn, &sortClient);
        if (headerClient.right > sortClient.left) {
            return fail(L"Source header STATIC overlaps Sort button hit target.");
        }
        // Activity refresh must not grow the header back over Sort.
        window->m_cachedSourceHeaderActivity =
            L"OCR 1/1 · 00:08 · Cloud PDF x2 · Queue 99";
        window->m_cachedVisibleRootCount = 1;
        window->UpdateSourceRailHeader();
        window->LayoutControls();
        GetWindowRect(window->m_sourceHeaderText, &headerClient);
        GetWindowRect(window->m_sourceSortBtn, &sortClient);
        // The contract intentionally keeps the top-level Dashboard hidden.
        // IsWindowVisible() therefore reports false for every child even when
        // its own WS_VISIBLE layout intent is correct.
        const bool sortLayoutVisible =
            (GetWindowLongPtrW(window->m_sourceSortBtn, GWL_STYLE) & WS_VISIBLE) != 0;
        if (!sortLayoutVisible ||
            headerClient.right > sortClient.left) {
            return fail(L"Activity text update covered or hid the Source Sort button.");
        }
    }

    resetWindowState();

    g_dashboardWindowTestDrivePdfOptionsDialog = true;
    g_dashboardWindowTestCancelPdfOptionsDialog = true;
    DashboardPdfImportOptions cancelOptions;
    cancelOptions.pageRange = L"1";
    cancelOptions.pdfRenderDpi = 144;
    if (window->PromptForPdfImportOptions({ resolvedPdfPath }, runRoot, cancelOptions)) {
        return fail(L"Dashboard PDF options dialog Cancel should reject the import.");
    }
    DashboardWindowTestClearOverrides();

    g_dashboardWindowTestDrivePdfOptionsDialog = true;
    g_dashboardWindowTestSavePdfOptionsDialog = true;
    g_dashboardWindowTestDrivenPdfImportOptions = DashboardPdfImportOptions{};
    g_dashboardWindowTestDrivenPdfImportOptions.pageRange = L"1";
    g_dashboardWindowTestDrivenPdfImportOptions.pdfRenderDpi = 175;
    g_dashboardWindowTestDrivenPdfImportOptions.pdfMaxPixelEdge = 3600;
    g_dashboardWindowTestDrivenPdfImportOptions.pdfMaxMegapixels = 10;
    g_dashboardWindowTestDrivenPdfImportOptions.pdfImageFormat = PdfRenderImageFormat::WebP;
    g_dashboardWindowTestDrivenPdfImportOptions.pdfImageQuality = 88;
    g_dashboardWindowTestDrivenPdfImportOptions.ocrMode = L"paddle_local";
    DashboardPdfImportOptions savedOnlyOptions;
    savedOnlyOptions.pageRange = L"1";
    if (window->PromptForPdfImportOptions({ resolvedPdfPath }, runRoot, savedOnlyOptions) ||
        !g_dashboardWindowTestSavePdfOptionsDialogStayedOpen ||
        // D-B-1: assert PDF session prefs via DashboardState sole authority.
        DashboardStateLastPdfRenderDpi(window->m_dashboardState) != 175 ||
        DashboardStateLastPdfMaxPixelEdge(window->m_dashboardState) != 3600 ||
        DashboardStateLastPdfMaxMegapixels(window->m_dashboardState) != 10 ||
        static_cast<PdfRenderImageFormat>(
            DashboardStateLastPdfImageFormat(window->m_dashboardState)) != PdfRenderImageFormat::WebP ||
        DashboardStateLastPdfImageQuality(window->m_dashboardState) != 88 ||
        window->GetDashboardOcrMode() != L"paddle_local") {
        return fail(L"Dashboard PDF Save settings did not persist options while keeping the dialog open.");
    }
    {
        const OcrSettings savedOcrSettings = LoadOcrSettings();
        if (savedOcrSettings.localRasterMaxPixelEdge != 3600 ||
            savedOcrSettings.localRasterMaxMegapixels != 10) {
            return fail(L"Dashboard PDF Save settings did not persist shared Local raster limits.");
        }
    }
    DashboardWindowTestClearOverrides();

    g_dashboardWindowTestDrivePdfOptionsDialog = true;
    g_dashboardWindowTestDrivenPdfImportOptions = DashboardPdfImportOptions{};
    g_dashboardWindowTestDrivenPdfImportOptions.pageRange = L"1";
    g_dashboardWindowTestDrivenPdfImportOptions.pdfRenderDpi = 144;
    g_dashboardWindowTestDrivenPdfImportOptions.ocrMode = L"paddle_cloud";
    g_dashboardWindowTestDrivenPdfImportOptions.rememberCloudFullPdfConsent = true;
    DashboardPdfImportOptions rememberedConsentOptions;
    rememberedConsentOptions.pageRange = L"1";
    rememberedConsentOptions.pdfRenderDpi = 144;
    if (!window->PromptForPdfImportOptions(
            { resolvedPdfPath },
            runRoot,
            rememberedConsentOptions) ||
        !rememberedConsentOptions.rememberCloudFullPdfConsent ||
        !rememberedConsentOptions.cloudFullPdfConsentGranted ||
        !DashboardStateIsPdfCloudRememberFullPdfConsent(window->m_dashboardState)) {
        return fail(L"Dashboard PDF options did not persist remembered Cloud full-PDF consent or skip the duplicate prompt.");
    }
    DashboardStateSetPdfCloudRememberFullPdfConsent(window->m_dashboardState, false);
    window->SaveBatchSessionState();
    DashboardWindowTestClearOverrides();

    std::wstring dropRoot = runRoot + L"_drop_entry";
    if (!BatchOcrWriter::EnsureDirectory(dropRoot)) {
        return fail(L"Failed to create drop entry contract output root.");
    }
    g_dashboardWindowTestBatchOutputRoot = dropRoot;
    g_dashboardWindowTestDrivenPdfImportOptions = DashboardPdfImportOptions{};
    g_dashboardWindowTestDrivenPdfImportOptions.pageRange = L"1";
    g_dashboardWindowTestDrivenPdfImportOptions.pdfRenderDpi = 144;
    g_dashboardWindowTestDrivePdfOptionsDialog = true;

    int ocrDropCountBeforePdfImport =
        DashboardWindowTestCountFilesRecursive(GetOcrImageDir(), L"ocr_drop_*.png");

    HDROP dropHandle = DashboardWindowTestCreateDropHandle({ resolvedPdfPath });
    if (!dropHandle) {
        return fail(L"Failed to create synthetic HDROP for Dashboard drop contract.");
    }
    SendMessageW(hwnd, WM_DROPFILES, reinterpret_cast<WPARAM>(dropHandle), 0);

    bool dropFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        if (DashboardStatePdfRenderInFlight(window->m_dashboardState) != 0 ||
            DashboardStateIsOcrBusy(window->m_dashboardState) ||
            !window->m_batch.dropQueue.empty()) {
            return false;
        }
        if (window->m_batch.activePdfJobs.size() != 1) return false;
        const BatchOcrPdfJob& active = window->m_batch.activePdfJobs.front();
        if (active.pages.empty()) return false;
        return std::all_of(active.pages.begin(), active.pages.end(),
            [](const BatchOcrPdfPageJob& page) {
                return page.status == BatchOcrTaskStatus::Completed ||
                    page.status == BatchOcrTaskStatus::Failed ||
                    page.status == BatchOcrTaskStatus::Canceled;
            });
    });
    if (!dropFinished) {
        return fail(L"Dashboard WM_DROPFILES PDF import did not finish in the window contract.");
    }
    if (window->m_batch.activePdfJobs.size() != 1) {
        return fail(L"Dashboard WM_DROPFILES PDF import did not create exactly one active PDF job.");
    }
    BatchOcrPdfJob dropActive = window->m_batch.activePdfJobs.front();
    if (DashboardStateLastBatchOutputRoot(window->m_dashboardState) != dropRoot ||
        dropActive.pageRange != L"1" ||
        dropActive.pdfRenderDpi != 144 ||
        dropActive.pages.size() != 1) {
        return fail(L"Dashboard WM_DROPFILES PDF import did not preserve output/options contract.");
    }
    if (!std::all_of(dropActive.pages.begin(), dropActive.pages.end(),
            [](const BatchOcrPdfPageJob& page) {
                return page.status == BatchOcrTaskStatus::Completed;
            })) {
        return fail(L"Dashboard WM_DROPFILES PDF import did not complete all selected pages.");
    }
    const BatchOcrPdfPageJob& dropPage = dropActive.pages.front();
    if (!window->m_history.model.items.empty()) {
        return fail(L"Dashboard WM_DROPFILES PDF import should not append PDF pages to image history.");
    }
    int ocrDropCountAfterPdfImport =
        DashboardWindowTestCountFilesRecursive(GetOcrImageDir(), L"ocr_drop_*.png");
    if (ocrDropCountAfterPdfImport != ocrDropCountBeforePdfImport) {
        return fail(L"Dashboard WM_DROPFILES PDF import should not create duplicate ocr_drop cache images.");
    }
    if (!PathFileExistsW(dropPage.sourceImagePath.c_str()) ||
        !PathFileExistsW(dropPage.markdownPath.c_str()) ||
        !PathFileExistsW(dropPage.contentJsonPath.c_str()) ||
        !PathFileExistsW(dropActive.markdownPath.c_str()) ||
        !PathFileExistsW(dropActive.contentJsonPath.c_str())) {
        return fail(L"Dashboard WM_DROPFILES PDF import did not write expected page/document outputs.");
    }
    std::wstring dropPageMarkdown;
    if (!DashboardWindowTestReadUtf8File(dropPage.markdownPath, dropPageMarkdown) ||
        !DashboardWindowTestContains(dropPageMarkdown, L"stub")) {
        return fail(L"Dashboard WM_DROPFILES PDF import page markdown did not contain stub OCR text.");
    }
    window->ActivateSourceRailPdfItem(0, dropPage.pageIndex, false);
    const int expectedDropSelectionPage = dropPage.pageIndex == 1 ? 0 : dropPage.pageIndex;
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != expectedDropSelectionPage ||
        !window->m_gdiplusImage ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Dashboard WM_DROPFILES PDF import did not refresh Source Rail, Canvas and Result selection.");
    }
    if (!window->RerunCurrentPdfSelection()) {
        return fail(L"Dashboard PDF page rerun command did not start from Source Rail selection.");
    }
    bool pdfPageRerunFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            !window->m_batch.activePdfJobs.empty() &&
            !window->m_batch.activePdfJobs.front().pages.empty() &&
            window->m_batch.activePdfJobs.front().pages.front().status == BatchOcrTaskStatus::Completed;
    });
    if (!pdfPageRerunFinished ||
        !DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != expectedDropSelectionPage ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Dashboard PDF page rerun did not preserve selection and refreshed output.");
    }

    DashboardWindowTestClearOverrides();
    resetWindowState();

    std::wstring virtualDropPayloadPath = runRoot + L"\\virtual_drop_payload.png";
    {
        HBITMAP bitmap = DashboardWindowTestCreateTextBitmap();
        if (!bitmap) return fail(L"Failed to create virtual OLE drop bitmap fixture.");
        bool saved = DashboardWindowTestSaveBitmapAsPng(bitmap, virtualDropPayloadPath);
        DeleteObject(bitmap);
        if (!saved) return fail(L"Failed to save virtual OLE drop bitmap fixture.");
    }
    std::vector<BYTE> virtualDropBytes;
    if (!DashboardWindowTestReadBinaryFile(virtualDropPayloadPath, virtualDropBytes)) {
        return fail(L"Failed to read virtual OLE drop payload bytes.");
    }
    auto* virtualDropObject = new DashboardWindowTestVirtualFileDataObject({
        DashboardWindowTestVirtualFile{ L"virtual_clipboard_image.png", virtualDropBytes }
    });
    if (!window->CanAcceptOleDropDataObject(virtualDropObject)) {
        virtualDropObject->Release();
        return fail(L"OLE virtual file drop target did not advertise support for virtual image descriptors.");
    }
    bool virtualDropAccepted = window->HandleOleDropDataObject(virtualDropObject);
    virtualDropObject->Release();
    bool virtualDropFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            window->m_history.model.items.size() == 1;
    });
    if (!virtualDropAccepted ||
        !virtualDropFinished ||
        window->m_history.model.items.empty() ||
        !IsPathInOcrImageCache(window->m_history.model.items.front().imagePath) ||
        window->m_history.model.items.front().text.find(L"stub") == std::wstring::npos) {
        return fail(L"OLE virtual file drop did not cache and OCR the virtual image source.");
    }

    DashboardWindowTestClearOverrides();
    resetWindowState();

    std::wstring dualFormatImportPath = runRoot + L"\\dual_format_drop.png";
    {
        HBITMAP bitmap = DashboardWindowTestCreateTextBitmap();
        if (!bitmap) return fail(L"Failed to create dual-format OLE drop bitmap fixture.");
        bool saved = DashboardWindowTestSaveBitmapAsPng(bitmap, dualFormatImportPath);
        DeleteObject(bitmap);
        if (!saved) return fail(L"Failed to save dual-format OLE drop bitmap fixture.");
    }
    auto* dualFormatDropObject = new DashboardWindowTestVirtualFileDataObject(
        { DashboardWindowTestVirtualFile{ L"dual_format_virtual.png", virtualDropBytes } },
        { dualFormatImportPath });
    if (!window->CanAcceptOleDropDataObject(dualFormatDropObject)) {
        dualFormatDropObject->Release();
        return fail(L"OLE dual-format drop target did not advertise support for filesystem image paths.");
    }
    bool dualFormatDropAccepted = window->HandleOleDropDataObject(dualFormatDropObject);
    dualFormatDropObject->Release();
    bool dualFormatDropFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            window->m_history.model.items.size() == 1 &&
            window->m_batch.batchTasks.size() == 1;
    });
    if (!dualFormatDropAccepted ||
        !dualFormatDropFinished ||
        !IsPathInOcrImageCache(window->m_history.model.items.front().imagePath) ||
        NormalizePathForCompare(window->m_batch.batchTasks.front().job.sourcePath) !=
            NormalizePathForCompare(dualFormatImportPath)) {
        return fail(
            L"OLE dual-format drop did not prefer the filesystem image source. accepted=" +
            std::to_wstring(dualFormatDropAccepted ? 1 : 0) +
            L" finished=" + std::to_wstring(dualFormatDropFinished ? 1 : 0) +
            L" historyCount=" + std::to_wstring(window->m_history.model.items.size()) +
            L" taskCount=" + std::to_wstring(window->m_batch.batchTasks.size()) +
            L" expected=" + dualFormatImportPath +
            L" actualHistory=" + (window->m_history.model.items.empty() ? L"<none>" : window->m_history.model.items.front().imagePath) +
            L" actualTask=" + (window->m_batch.batchTasks.empty() ? L"<none>" : window->m_batch.batchTasks.front().job.sourcePath));
    }

    DashboardWindowTestClearOverrides();
    resetWindowState();

    auto* virtualFallbackDropObject = new DashboardWindowTestVirtualFileDataObject(
        { DashboardWindowTestVirtualFile{ L"virtual_fallback_image.png", virtualDropBytes } },
        { runRoot + L"\\unsupported_placeholder.txt" });
    bool virtualFallbackAccepted = window->HandleOleDropDataObject(virtualFallbackDropObject);
    virtualFallbackDropObject->Release();
    bool virtualFallbackFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            window->m_history.model.items.size() == 1 &&
            window->m_batch.batchTasks.size() == 1;
    });
    if (!virtualFallbackAccepted ||
        !virtualFallbackFinished ||
        !IsPathInOcrImageCache(window->m_history.model.items.front().imagePath) ||
        !IsPathInOcrImageCache(window->m_batch.batchTasks.front().job.sourcePath)) {
        return fail(L"OLE drop did not fall back to virtual image contents when CF_HDROP paths were not importable.");
    }

    DashboardWindowTestClearOverrides();
    resetWindowState();

    std::wstring singleImportPath = runRoot + L"\\single_transient_import.png";
    {
        HBITMAP bitmap = DashboardWindowTestCreateTextBitmap();
        if (!bitmap) return fail(L"Failed to create transient image import fixture.");
        bool saved = DashboardWindowTestSaveBitmapAsPng(bitmap, singleImportPath);
        DeleteObject(bitmap);
        if (!saved) return fail(L"Failed to save transient image import fixture.");
    }

    // Startup restores recent roots before durable History roots. The same
    // root must not be loaded a second time when History points at it too.
    std::wstring restartDedupRoot = runRoot + L"_restart_dedup";
    std::vector<BatchOcrImageJob> restartDedupJobs;
    std::wstring restartDedupError;
    if (!window->m_batchController.CreateImageJobs(
            { singleImportPath },
            restartDedupRoot,
            restartDedupJobs,
            restartDedupError,
            window->GetDashboardOcrMode()) ||
        restartDedupJobs.size() != 1 ||
        !BatchOcrWriter::WriteImagePending(restartDedupJobs.front()).success) {
        return fail(restartDedupError.empty()
            ? L"Failed to prepare Dashboard restart deduplication fixture."
            : restartDedupError);
    }
    OcrDashboardHistoryItem restartDedupHistory;
    restartDedupHistory.sourceInstanceId = restartDedupJobs.front().sourceInstanceId;
    restartDedupHistory.recordKind = L"DurableOutputLink";
    restartDedupHistory.originManifestPath = restartDedupJobs.front().manifestPath;
    restartDedupHistory.imagePath = restartDedupJobs.front().sourceImagePath;
    window->m_history.model.items = { restartDedupHistory };
    // Match startup ordering: History is visible before its Output root has
    // been restored and linked into the Source projection.
    window->ApplyFilter(L"");
    if (DashboardStateVisibleHistoryIndices(window->m_dashboardState) != std::vector<int>{0} ||
        window->BuildSourceRailSelectableRows().size() != 1 ||
        window->BuildSourceRailSelectableRows().front().kind !=
            DashboardSourceRailRowKind::History) {
        return fail(L"Dashboard restart fixture did not begin with one standalone source.png History row.");
    }
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        restartDedupRoot,
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        { restartDedupRoot });
    window->AutoResumeLastBatchOutputRoot();
    const bool durableRootWouldReload = !window->IsBatchOutputRootInUse(restartDedupRoot);
    const auto restartedRows = window->BuildSourceRailSelectableRows();
    if (window->m_batch.batchTasks.size() != 1 ||
        durableRootWouldReload ||
        !DashboardStateVisibleHistoryIndices(window->m_dashboardState).empty() ||
        restartedRows.size() != 1 ||
        restartedRows.front().kind != DashboardSourceRailRowKind::ImageTask ||
        restartedRows.front().linkedHistoryIndex != 0 ||
        !window->LoadBatchOutputSnapshot(restartDedupRoot, false, false, true, false) ||
        window->m_batch.batchTasks.size() != 1 ||
        BuildDashboardSourceProjection(
            window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items).size() != 1) {
        return fail(L"Dashboard restart retained a stale source.png row or reloaded its durable output root.");
    }
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        L"",
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        {});
    resetWindowState();

    // Removing one Source from a shared output root must survive restart even
    // though the root still has to be scanned for its retained sibling.
    std::wstring dismissedRestartRoot = runRoot + L"_dismissed_restart";
    std::vector<BatchOcrImageJob> dismissedRestartJobs;
    std::wstring dismissedRestartError;
    if (!window->m_batchController.CreateImageJobs(
            {singleImportPath, singleImportPath},
            dismissedRestartRoot,
            dismissedRestartJobs,
            dismissedRestartError,
            window->GetDashboardOcrMode()) ||
        dismissedRestartJobs.size() != 2 ||
        !window->LoadBatchOutputSnapshot(
            dismissedRestartRoot, false, false, false, false) ||
        window->m_batch.batchTasks.size() != 2) {
        return fail(dismissedRestartError.empty()
            ? L"Failed to prepare persistent Source-removal restart fixture."
            : dismissedRestartError);
    }
    const std::wstring removedManifest = dismissedRestartJobs[0].manifestPath;
    const std::wstring retainedManifest = dismissedRestartJobs[1].manifestPath;
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        dismissedRestartRoot,
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        {dismissedRestartRoot});
    window->ActivateSourceRailImageTask(0);
    window->m_testAutoConfirmDelete = true;
    window->DeleteSelectedSources();
    window->m_testAutoConfirmDelete = false;
    if (window->m_batch.batchTasks.size() != 1 ||
        !DashboardHistoryIsImageJobDismissed(
            DashboardStateDismissedBatchManifestKeys(window->m_dashboardState),
            dismissedRestartJobs[0].manifestPath,
            dismissedRestartJobs[0].sourceInstanceId,
            dismissedRestartJobs[0].createdAt,
            dismissedRestartJobs[0].sourcePath) ||
        !PathFileExistsW(removedManifest.c_str()) ||
        !PathFileExistsW(retainedManifest.c_str()) ||
        window->GetAutoResumeOutputRoots().empty()) {
        return fail(L"Source removal did not persist its manifest tombstone while retaining shared-root outputs.");
    }

    resetWindowState();
    DashboardStateSetDismissedBatchManifestKeys(window->m_dashboardState, {});
    DashboardStateApplyPersistenceFlags(window->m_dashboardState, DashboardStateIsHistoryPersistenceSuspended(window->m_dashboardState), false);
    if (!DashboardHistorySessionLoadDismissed(window->m_history, window->m_dashboardState)) {
        return fail(L"Restart could not reload persisted Dashboard Source removals.");
    }
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        dismissedRestartRoot,
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        {dismissedRestartRoot});
    window->AutoResumeLastBatchOutputRoot();
    if (window->m_batch.batchTasks.size() != 1 ||
        NormalizePathForCompare(window->m_batch.batchTasks.front().job.manifestPath) !=
            NormalizePathForCompare(retainedManifest) ||
        std::any_of(window->m_batch.batchTasks.begin(), window->m_batch.batchTasks.end(),
            [&](const DashboardBatchTaskItem& task) {
                return NormalizePathForCompare(task.job.manifestPath) ==
                    NormalizePathForCompare(removedManifest);
            })) {
        return fail(L"A removed Source reappeared when restart rescanned its shared output root.");
    }

    // D-C-5: capture pure dismissed keys for corrupt-ledger restore fixture.
    const std::vector<std::wstring> validDismissedKeys =
        DashboardStateDismissedBatchManifestKeys(window->m_dashboardState);
    auto rejectCorruptDismissedLedger = [&](const std::wstring& contents) {
        if (!DashboardWindowTestWriteUtf8File(testDismissedPath, contents)) return false;
        DashboardStateSetDismissedBatchManifestKeys(window->m_dashboardState, {});
        DashboardStateApplyPersistenceFlags(window->m_dashboardState, DashboardStateIsHistoryPersistenceSuspended(window->m_dashboardState), false);
        bool rejected = !DashboardHistorySessionLoadDismissed(window->m_history, window->m_dashboardState) &&
            DashboardStateIsDismissedManifestPersistenceSuspended(window->m_dashboardState) &&
            DashboardStateDismissedBatchManifestKeys(window->m_dashboardState).empty();

        WIN32_FIND_DATAW backupData = {};
        HANDLE backupFind = FindFirstFileW(
            (testDismissedPath + L".bad.*").c_str(), &backupData);
        bool backupExists = backupFind != INVALID_HANDLE_VALUE;
        if (backupFind != INVALID_HANDLE_VALUE) FindClose(backupFind);
        if (backupExists) {
            wchar_t backupDir[MAX_PATH] = {};
            wcsncpy_s(backupDir, testDismissedPath.c_str(), _TRUNCATE);
            PathRemoveFileSpecW(backupDir);
            DeleteFileW(JoinPathWide(backupDir, backupData.cFileName).c_str());
        }
        return rejected && backupExists;
    };
    if (!rejectCorruptDismissedLedger(L"") ||
        !rejectCorruptDismissedLedger(L"[123]")) {
        return fail(L"Empty or semantically invalid removal metadata did not fail closed with a backup.");
    }
    DashboardStateSetDismissedBatchManifestKeys(window->m_dashboardState, validDismissedKeys);
    DashboardStateApplyPersistenceFlags(window->m_dashboardState, DashboardStateIsHistoryPersistenceSuspended(window->m_dashboardState), false);
    if (!DashboardHistorySessionSaveDismissed(window->m_history, window->m_dashboardState)) {
        return fail(L"Persistent Source-removal fixture could not restore its valid ledger.");
    }
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        L"",
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        {});
    resetWindowState();

    window->QueueImageFiles({ singleImportPath });
    if (window->m_batch.batchTasks.size() != 1 ||
        !DashboardStateHasImageTaskSelection(window->m_dashboardState) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) >= 0 ||
        !window->m_gdiplusImage ||
        !window->m_activeWorkTimerRunning ||
        window->BuildActiveWorkText().empty()) {
        return fail(L"Single image import did not immediately create/select a transient task, preview image, and active work state.");
    }
    const DashboardBatchTaskItem& initialTransientTask = window->m_batch.batchTasks.front();
    if (initialTransientTask.job.sourcePath != singleImportPath ||
        !IsValidBatchOcrSourceInstanceId(initialTransientTask.job.sourceInstanceId) ||
        !initialTransientTask.job.outputDir.empty() ||
        !initialTransientTask.job.manifestPath.empty() ||
        (initialTransientTask.status != BatchOcrTaskStatus::Pending &&
         initialTransientTask.status != BatchOcrTaskStatus::Recognizing)) {
        return fail(L"Single image import task was not a sourcePath-only Pending/Recognizing transient task.");
    }

    bool singleImportFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            window->m_history.model.items.size() == 1 &&
            window->m_batch.batchTasks.size() == 1 &&
            window->m_batch.batchTasks.front().status == BatchOcrTaskStatus::Completed;
    });
    if (!singleImportFinished ||
        window->m_batch.batchTasks.front().job.outputDir.size() != 0 ||
        window->m_batch.batchTasks.front().job.manifestPath.size() != 0 ||
        window->m_history.model.items.front().text.find(L"stub") == std::wstring::npos) {
        return fail(L"Single transient image import did not finish as Done without batch writer output.");
    }
    const std::wstring transientSourceId = window->m_batch.batchTasks.front().job.sourceInstanceId;
    auto transientProjection = BuildDashboardSourceProjection(
        window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items);
    if (window->m_history.model.items.front().sourceInstanceId != transientSourceId ||
        window->m_history.model.items.front().originKind != L"ImportedImage" ||
        transientProjection.size() != 1 ||
        transientProjection.front().refs.imageTaskIndex != 0 ||
        transientProjection.front().refs.historyIndex != 0 ||
        !DashboardStateVisibleHistoryIndices(window->m_dashboardState).empty() ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Unified Source projection did not merge transient task/History while preserving the full result provider.");
    }
    window->RefreshCurrentBlocks();
    if (window->m_canvas.currentBlocks.empty()) {
        return fail(L"Transient History provider did not expose editable OCR blocks.");
    }
    const std::wstring transientEditBlockId = window->m_canvas.currentBlocks.front().id;
    if (!window->ApplyPreviewBlockEdit(
            transientEditBlockId,
            L"transient provider edit",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(),
                L"stub")) ||
        !DashboardWindowTestContains(
            window->m_history.model.items.front().text,
            L"transient provider edit") ||
        !window->RestorePreviewBlockOriginal(
            transientEditBlockId,
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(),
                L"transient provider edit")) ||
        !DashboardWindowTestContains(window->m_history.model.items.front().text, L"stub")) {
        return fail(L"Transient image Source did not persist Preview edits through linked History.");
    }
    if (!DashboardHistorySessionSaveItems(window->m_history, window->m_dashboardState)) {
        return fail(L"History provenance save failed.");
    }
    window->m_history.model.items.clear();
    window->LoadHistory();
    if (window->m_history.model.items.size() != 1 ||
        window->m_history.model.items.front().sourceInstanceId != transientSourceId ||
        window->m_history.model.items.front().originKind != L"ImportedImage" ||
        window->m_history.model.items.front().recordKind != L"TransientPayload" ||
        window->m_history.model.items.front().ownedCacheFiles.empty() ||
        std::find(
            window->m_history.model.items.front().ownedCacheFiles.begin(),
            window->m_history.model.items.front().ownedCacheFiles.end(),
            window->m_history.model.items.front().imagePath) ==
            window->m_history.model.items.front().ownedCacheFiles.end()) {
        return fail(L"Transient History provenance/ownership did not round-trip through the additive JSON fields.");
    }
    std::wstring invalidOptionalHistory =
        L"[{\"sourceInstanceId\":\"not-a-guid\","
        L"\"originKind\":\"FutureOrigin\","
        L"\"originManifestPath\":\"C:\\\\outside\\\\manifest.json\","
        L"\"timestamp\":\"2026-07-13 12:00:00\","
        L"\"imagePath\":\"" + EscapeJsonString(singleImportPath) + L"\","
        L"\"text\":\"optional provenance survived\","
        L"\"futureUnknown\":{\"nested\":true}}]";
    if (!DashboardWindowTestWriteUtf8File(DashboardWindowTestHistoryFilePath(), invalidOptionalHistory)) {
        return fail(L"Failed to prepare invalid optional History provenance fixture.");
    }
    window->LoadHistory();
    if (window->m_history.model.items.size() != 1 ||
        !window->m_history.model.items.front().sourceInstanceId.empty() ||
        !window->m_history.model.items.front().originKind.empty() ||
        window->m_history.model.items.front().text != L"optional provenance survived") {
        return fail(L"Invalid optional/unknown History provenance discarded the record instead of degrading fields.");
    }

    const std::wstring corruptHistoryPath = DashboardWindowTestHistoryFilePath();
    if (!DashboardWindowTestWriteUtf8File(
            corruptHistoryPath,
            L"[{\"timestamp\":\"2026-07-14 00:00:00\",\"text\":\"truncated")) {
        return fail(L"Failed to prepare a truncated History fixture.");
    }
    window->LoadHistory();
    WIN32_FIND_DATAW corruptBackupData = {};
    HANDLE corruptBackupFind = FindFirstFileW(
        (corruptHistoryPath + L".bad.*").c_str(),
        &corruptBackupData);
    const bool corruptBackupExists = corruptBackupFind != INVALID_HANDLE_VALUE;
    if (corruptBackupFind != INVALID_HANDLE_VALUE) FindClose(corruptBackupFind);
    if (!window->m_history.model.items.empty() ||
        !DashboardStateIsHistoryPersistenceSuspended(window->m_dashboardState) ||
        DashboardHistorySessionSaveItems(window->m_history, window->m_dashboardState) ||
        !corruptBackupExists) {
        return fail(L"Truncated History did not degrade to an empty, write-suspended state with a diagnostic backup.");
    }

    if (corruptBackupExists) {
        wchar_t corruptBackupDir[MAX_PATH] = {};
        wcsncpy_s(corruptBackupDir, corruptHistoryPath.c_str(), _TRUNCATE);
        PathRemoveFileSpecW(corruptBackupDir);
        DeleteFileW(JoinPathWide(corruptBackupDir, corruptBackupData.cFileName).c_str());
    }

    const std::wstring validPrefixTruncatedHistory =
        L"[{\"timestamp\":\"2026-07-14 00:00:01\"," 
        L"\"imagePath\":\"" + EscapeJsonString(singleImportPath) + L"\"," 
        L"\"text\":\"valid prefix\"},"
        L"{\"timestamp\":\"2026-07-14 00:00:02\"," 
        L"\"imagePath\":\"unterminated";
    if (!DashboardWindowTestWriteUtf8File(
            corruptHistoryPath,
            validPrefixTruncatedHistory)) {
        return fail(L"Failed to prepare a valid-prefix/truncated-tail History fixture.");
    }
    window->LoadHistory();
    WIN32_FIND_DATAW prefixBackupData = {};
    HANDLE prefixBackupFind = FindFirstFileW(
        (corruptHistoryPath + L".bad.*").c_str(),
        &prefixBackupData);
    const bool prefixBackupExists = prefixBackupFind != INVALID_HANDLE_VALUE;
    if (prefixBackupFind != INVALID_HANDLE_VALUE) FindClose(prefixBackupFind);
    std::wstring preservedTruncatedHistory;
    if (!window->m_history.model.items.empty() ||
        !DashboardStateIsHistoryPersistenceSuspended(window->m_dashboardState) ||
        DashboardHistorySessionSaveItems(window->m_history, window->m_dashboardState) ||
        !prefixBackupExists ||
        !DashboardWindowTestReadUtf8File(corruptHistoryPath, preservedTruncatedHistory) ||
        preservedTruncatedHistory != validPrefixTruncatedHistory) {
        return fail(L"History accepted or overwrote a valid prefix followed by a truncated record.");
    }
    if (prefixBackupExists) {
        wchar_t prefixBackupDir[MAX_PATH] = {};
        wcsncpy_s(prefixBackupDir, corruptHistoryPath.c_str(), _TRUNCATE);
        PathRemoveFileSpecW(prefixBackupDir);
        DeleteFileW(JoinPathWide(prefixBackupDir, prefixBackupData.cFileName).c_str());
    }
    DeleteFileW(corruptHistoryPath.c_str());
    window->LoadHistory();
    if (DashboardStateIsHistoryPersistenceSuspended(window->m_dashboardState) || !window->m_history.model.items.empty()) {
        return fail(L"History persistence did not recover after the corrupt fixture was removed.");
    }

    DashboardWindowTestClearOverrides();
    resetWindowState();

    window->QueueImageFiles({ singleImportPath });
    bool firstRepeatedImportFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) && window->m_batch.dropQueue.empty() &&
            window->m_batch.batchTasks.size() == 1 && window->m_history.model.items.size() == 1;
    });
    if (!firstRepeatedImportFinished) {
        return fail(L"First import in the repeated same-path contract did not finish.");
    }
    const std::wstring firstRepeatedSourceId = window->m_batch.batchTasks.front().job.sourceInstanceId;

    window->QueueImageFiles({ singleImportPath });
    bool repeatedImportFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) && window->m_batch.dropQueue.empty() &&
            window->m_batch.batchTasks.size() == 2 && window->m_history.model.items.size() == 2;
    });
    auto repeatedRuntimeProjection = BuildDashboardSourceProjection(
        window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items);
    if (!repeatedImportFinished || repeatedRuntimeProjection.size() != 2 ||
        repeatedRuntimeProjection[0].stableSourceKey == repeatedRuntimeProjection[1].stableSourceKey ||
        window->m_batch.batchTasks[1].job.sourceInstanceId == firstRepeatedSourceId ||
        DashboardStateImageTaskSelectionSourceInstanceId(window->m_dashboardState) != window->m_batch.batchTasks[1].job.sourceInstanceId ||
        !DashboardStateVisibleHistoryIndices(window->m_dashboardState).empty()) {
        return fail(L"Repeated import of the same path did not remain two stable, non-duplicated Source roots.");
    }
    auto drawSourceRailFocusFixture = [&]() {
        HDC referenceDc = GetDC(hwnd);
        HDC memoryDc = referenceDc ? CreateCompatibleDC(referenceDc) : nullptr;
        HBITMAP bitmap = referenceDc
            ? CreateCompatibleBitmap(referenceDc, 640, 480)
            : nullptr;
        HGDIOBJ oldBitmap = memoryDc && bitmap ? SelectObject(memoryDc, bitmap) : nullptr;
        if (memoryDc && bitmap) {
            window->DrawBatchTaskSection(memoryDc, 640, 480, 0);
        }
        if (oldBitmap) SelectObject(memoryDc, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (memoryDc) DeleteDC(memoryDc);
        if (referenceDc) ReleaseDC(hwnd, referenceDc);
        return referenceDc && memoryDc && bitmap;
    };
    HWND previousFocus = GetFocus();
    SetFocus(window->m_sourceList);
    window->m_testSourceRailFocusRectCount = 0;
    if (!drawSourceRailFocusFixture() || window->m_testSourceRailFocusRectCount <= 0) {
        return fail(L"The active unified Source root did not draw a keyboard focus rectangle; count=" +
            std::to_wstring(window->m_testSourceRailFocusRectCount) +
            L", focused=" + std::to_wstring(reinterpret_cast<uintptr_t>(GetFocus())) +
            L", source=" + std::to_wstring(reinterpret_cast<uintptr_t>(window->m_sourceList)) + L".");
    }
    SetFocus(hwnd);
    window->m_testSourceRailFocusRectCount = 0;
    if (!drawSourceRailFocusFixture() || window->m_testSourceRailFocusRectCount != 0) {
        return fail(L"Source Rail drew a focus rectangle while keyboard focus was elsewhere.");
    }
    if (previousFocus && IsWindow(previousFocus)) SetFocus(previousFocus);

    DashboardBatchTaskItem& outputFailedTask = window->m_batch.batchTasks[1];
    outputFailedTask.status = BatchOcrTaskStatus::Failed;
    outputFailedTask.error = L"output write failed contract warning";
    outputFailedTask.job.outputDir = runRoot;
    outputFailedTask.job.markdownPath = JoinPathWide(runRoot, L"stale_output_failure.md");
    outputFailedTask.job.textPath = JoinPathWide(runRoot, L"stale_output_failure.txt");
    outputFailedTask.job.contentJsonPath = JoinPathWide(runRoot, L"stale_output_failure.content.json");
    if (!DashboardWindowTestWriteUtf8File(outputFailedTask.job.markdownPath, L"stale task markdown") ||
        !DashboardWindowTestWriteUtf8File(outputFailedTask.job.textPath, L"stale task text") ||
        !DashboardWindowTestWriteUtf8File(
            outputFailedTask.job.contentJsonPath,
            L"{\"markdown\":\"stale task content JSON\"}")) {
        return fail(L"Failed to prepare output-write-failure provider fixtures.");
    }
    const int outputFailureHistoryIndex = 1;
    const std::wstring outputFailureHistoryText =
        window->m_history.model.items[(size_t)outputFailureHistoryIndex].text;
    window->ActivateSourceRailImageTask(1);
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardStateTextModePreferred(window->m_dashboardState), DashboardTextMode::Source);
    if (window->GetCurrentResultText() != outputFailureHistoryText ||
        window->GetCurrentPreviewSourceMarkdown() != outputFailureHistoryText) {
        return fail(L"Output-write-failure Source read stale task output instead of linked History Markdown.");
    }
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardStateTextModePreferred(window->m_dashboardState), DashboardTextMode::Text);
    if (!DashboardWindowTestContains(window->GetCurrentResultText(), L"stub") ||
        DashboardWindowTestContains(window->GetCurrentResultText(), L"stale task")) {
        return fail(L"Output-write-failure Text provider did not fall back to linked History.");
    }
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardStateTextModePreferred(window->m_dashboardState), DashboardTextMode::Json);
    if (!DashboardWindowTestContains(window->GetCurrentResultText(), L"stub") ||
        DashboardWindowTestContains(window->GetCurrentResultText(), L"stale task content")) {
        return fail(L"Output-write-failure JSON provider did not fall back to linked History payload.");
    }
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardStateTextModePreferred(window->m_dashboardState), DashboardTextMode::Source);
    window->RefreshCurrentBlocks();
    if (window->m_canvas.currentBlocks.empty()) {
        return fail(L"Output-write-failure History provider did not expose OCR blocks.");
    }
    const std::wstring outputFailureEditBlockId = window->m_canvas.currentBlocks.front().id;
    if (!window->ApplyPreviewBlockEdit(
            outputFailureEditBlockId,
            L"output failure provider edit",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(),
                L"stub")) ||
        !DashboardWindowTestContains(
            window->m_history.model.items[(size_t)outputFailureHistoryIndex].text,
            L"output failure provider edit") ||
        !window->RestorePreviewBlockOriginal(
            outputFailureEditBlockId,
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(),
                L"output failure provider edit")) ||
        window->GetCurrentPreviewSourceMarkdown() != outputFailureHistoryText) {
        return fail(L"Output-write-failure Source did not edit/restore through linked History.");
    }
    std::wstring staleTaskMarkdownAfterProviderEdit;
    if (!DashboardWindowTestReadUtf8File(
            outputFailedTask.job.markdownPath,
            staleTaskMarkdownAfterProviderEdit) ||
        staleTaskMarkdownAfterProviderEdit != L"stale task markdown") {
        return fail(L"History-provider edit unexpectedly modified stale task artifacts.");
    }
    window->m_testAutoConfirmDelete = true;
    window->DeleteSelectedSources();
    window->m_testAutoConfirmDelete = false;
    if (window->m_batch.batchTasks.size() != 1 || window->m_history.model.items.size() != 1 ||
        window->m_batch.batchTasks.front().job.sourceInstanceId != firstRepeatedSourceId ||
        window->m_history.model.items.front().sourceInstanceId != firstRepeatedSourceId ||
        !PathFileExistsW(singleImportPath.c_str())) {
        return fail(L"Deleting one repeated same-path Source removed/cross-wired its sibling or original file.");
    }
    resetWindowState();

    std::wstring preferredOutputRoot = runRoot + L"_preferred_single_output";
    if (!BatchOcrWriter::EnsureDirectory(preferredOutputRoot)) {
        return fail(L"Failed to create preferred output root.");
    }
    std::wstring preferredImportPath = runRoot + L"\\single_preferred_import.png";
    {
        HBITMAP bitmap = DashboardWindowTestCreateTextBitmap();
        if (!bitmap) return fail(L"Failed to create preferred image import fixture.");
        bool saved = DashboardWindowTestSaveBitmapAsPng(bitmap, preferredImportPath);
        DeleteObject(bitmap);
        if (!saved) return fail(L"Failed to save preferred image import fixture.");
    }

    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        preferredOutputRoot,
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    window->SaveBatchSessionState();
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        L"",
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    window->LoadBatchSessionState();
    if (NormalizePathForCompare(DashboardStatePreferredBatchOutputRoot(window->m_dashboardState)) !=
        NormalizePathForCompare(preferredOutputRoot)) {
        return fail(L"Preferred output root was not persisted in Dashboard session state.");
    }

    std::wstring resolvedPreferredRoot;
    if (!window->ResolvePreferredBatchOutputRoot(resolvedPreferredRoot) ||
        NormalizePathForCompare(resolvedPreferredRoot) != NormalizePathForCompare(preferredOutputRoot)) {
        return fail(L"Preferred output root did not resolve from Dashboard session state.");
    }

    window->QueueImageFiles({ preferredImportPath });
    bool preferredImportFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            window->m_history.model.items.size() == 1 &&
            window->m_batch.batchTasks.size() == 1 &&
            window->m_batch.batchTasks.front().status == BatchOcrTaskStatus::Completed;
    });
    if (!preferredImportFinished) {
        return fail(L"Preferred-root single image import did not finish.");
    }
    const DashboardBatchTaskItem& preferredTask = window->m_batch.batchTasks.front();
    std::wstring normalizedPreferredRoot = NormalizePathForCompare(preferredOutputRoot);
    const std::wstring normalizedPreferredOutputDir =
        NormalizePathForCompare(preferredTask.job.outputDir);
    const bool outputDirEqualsPreferredRoot =
        !normalizedPreferredRoot.empty() &&
        normalizedPreferredOutputDir == normalizedPreferredRoot;
    if (!normalizedPreferredRoot.empty() &&
        normalizedPreferredRoot.back() != L'\\' &&
        normalizedPreferredRoot.back() != L'/') {
        normalizedPreferredRoot += L"\\";
    }
    const bool outputDirIsUnderPreferredRoot =
        !normalizedPreferredRoot.empty() &&
        normalizedPreferredOutputDir.rfind(normalizedPreferredRoot, 0) == 0;
    if (NormalizePathForCompare(DashboardStateLastBatchOutputRoot(window->m_dashboardState)) != NormalizePathForCompare(preferredOutputRoot) ||
        NormalizePathForCompare(preferredTask.job.outputRoot) != NormalizePathForCompare(preferredOutputRoot) ||
        (!outputDirEqualsPreferredRoot && !outputDirIsUnderPreferredRoot) ||
        preferredTask.job.manifestPath.empty() ||
        preferredTask.job.markdownPath.empty() ||
        !PathFileExistsW(preferredTask.job.manifestPath.c_str()) ||
        !PathFileExistsW(preferredTask.job.markdownPath.c_str()) ||
        NormalizePathForCompare(window->GetCurrentOutputFolder()) != NormalizePathForCompare(preferredTask.job.outputDir)) {
        return fail(L"Preferred-root single image import did not write/select batch output under the saved folder.");
    }
    if (!IsValidBatchOcrSourceInstanceId(preferredTask.job.sourceInstanceId) ||
        window->m_history.model.items.front().sourceInstanceId != preferredTask.job.sourceInstanceId ||
        window->m_history.model.items.front().recordKind != L"DurableOutputLink" ||
        !window->m_history.model.items.front().text.empty() ||
        !window->m_history.model.items.front().blocks.empty() ||
        !window->m_history.model.items.front().rawOcrJson.empty() ||
        !window->m_history.model.items.front().debugOutputImagesJson.empty() ||
        !window->m_history.model.items.front().ownedCacheFiles.empty() ||
        NormalizePathForCompare(window->m_history.model.items.front().originManifestPath) !=
            NormalizePathForCompare(preferredTask.job.manifestPath) ||
        BuildDashboardSourceProjection(
            window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items).size() != 1 ||
        !DashboardStateVisibleHistoryIndices(window->m_dashboardState).empty()) {
        return fail(L"Durable image task/History provenance did not form one visible Source before restart.");
    }
    const std::wstring preferredSourceId = preferredTask.job.sourceInstanceId;
    resetWindowState();
    if (!window->LoadBatchOutputSnapshot(preferredOutputRoot, false, false)) {
        return fail(L"Durable image Source restart fixture could not reload its output snapshot.");
    }
    window->LoadHistory();
    auto restartedPreferredProjection = BuildDashboardSourceProjection(
        window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items);
    if (window->m_batch.batchTasks.size() != 1 || window->m_history.model.items.size() != 1 ||
        window->m_batch.batchTasks.front().job.sourceInstanceId != preferredSourceId ||
        window->m_history.model.items.front().sourceInstanceId != preferredSourceId ||
        window->m_history.model.items.front().recordKind != L"DurableOutputLink" ||
        !window->m_history.model.items.front().text.empty() ||
        !window->m_history.model.items.front().ownedCacheFiles.empty() ||
        restartedPreferredProjection.size() != 1 ||
        restartedPreferredProjection.front().refs.imageTaskIndex != 0 ||
        restartedPreferredProjection.front().refs.historyIndex != 0) {
        return fail(L"Durable image Source identity did not survive manifest/History restart association.");
    }

    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        L"",
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        L"",
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        {});
    window->SaveBatchSessionState();
    resetWindowState();

    std::wstring webpImportPath = runRoot + L"\\single_transient_preview.webp";
    {
        HBITMAP bitmap = DashboardWindowTestCreateTextBitmap();
        if (!bitmap) return fail(L"Failed to create WebP preview fixture bitmap.");
        ImageCodec::EncodeOptions options;
        std::wstring encodeError;
        bool saved = ImageCodec::SaveHBitmapToFile(
            bitmap,
            webpImportPath,
            ImageCodec::ImageFileFormat::WebP,
            options,
            &encodeError);
        DeleteObject(bitmap);
        if (!saved || !PathFileExistsW(webpImportPath.c_str())) {
            return fail(encodeError.empty()
                ? L"Failed to save WebP preview fixture."
                : encodeError);
        }
    }
    window->LoadImageIntoCanvas(webpImportPath, false);
    if (!window->m_gdiplusImage) {
        return fail(L"LoadImageIntoCanvas did not use the shared ImageCodec decoder for WebP preview.");
    }

    size_t historyBeforeTransientRerun = window->m_history.model.items.size();
    BatchOcrImageJob transientFailedJob;
    transientFailedJob.sourcePath = singleImportPath;
    transientFailedJob.baseName = L"single_transient_import.png";
    transientFailedJob.engineMode = window->GetDashboardOcrMode();
    window->UpsertBatchTask(
        transientFailedJob,
        BatchOcrTaskStatus::Failed,
        0,
        L"forced transient failure");
    window->RememberFailedBatchJob(transientFailedJob);
    int transientFailedIndex = window->FindImageTaskIndex(transientFailedJob);
    if (transientFailedIndex < 0) {
        return fail(L"Failed to locate transient task fixture for rerun.");
    }
    window->ActivateSourceRailImageTask(transientFailedIndex);
    if (!window->RerunCurrentImageTask()) {
        return fail(L"Transient image task rerun did not start.");
    }
    if (const DashboardBatchTaskItem* rerunSelection = window->GetSelectedImageTask()) {
        transientFailedJob.sourceInstanceId = rerunSelection->job.sourceInstanceId;
    }
    bool transientRerunFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        int index = window->FindImageTaskIndex(transientFailedJob);
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            index >= 0 &&
            window->m_batch.batchTasks[(size_t)index].status == BatchOcrTaskStatus::Completed &&
            window->m_history.model.items.size() > historyBeforeTransientRerun;
    });
    int rerunTaskIndex = window->FindImageTaskIndex(transientFailedJob);
    if (!transientRerunFinished ||
        rerunTaskIndex < 0 ||
        !window->m_batch.batchTasks[(size_t)rerunTaskIndex].job.outputDir.empty() ||
        !window->m_batch.batchTasks[(size_t)rerunTaskIndex].job.manifestPath.empty()) {
        return fail(L"Transient image rerun did not complete without invoking batch writer output.");
    }

    std::wstring invalidTransientPath = runRoot + L"\\invalid_transient_import.png";
    {
        const char invalidBytes[] = "not an image";
        FILE* file = nullptr;
        if (_wfopen_s(&file, invalidTransientPath.c_str(), L"wb") != 0 || !file) {
            return fail(L"Failed to prepare invalid transient image fixture.");
        }
        size_t written = fwrite(invalidBytes, 1, sizeof(invalidBytes) - 1, file);
        fclose(file);
        if (written != sizeof(invalidBytes) - 1) {
            return fail(L"Failed to write invalid transient image fixture.");
        }
    }
    window->QueueImageFiles({ invalidTransientPath });
    bool invalidImportFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        auto taskIt = std::find_if(window->m_batch.batchTasks.begin(), window->m_batch.batchTasks.end(),
            [&](const DashboardBatchTaskItem& task) {
                return _wcsicmp(task.job.sourcePath.c_str(), invalidTransientPath.c_str()) == 0;
            });
        int index = taskIt == window->m_batch.batchTasks.end()
            ? -1
            : static_cast<int>(taskIt - window->m_batch.batchTasks.begin());
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            index >= 0 &&
            window->m_batch.batchTasks[(size_t)index].status == BatchOcrTaskStatus::Failed;
    });
    auto invalidTaskIt = std::find_if(window->m_batch.batchTasks.begin(), window->m_batch.batchTasks.end(),
        [&](const DashboardBatchTaskItem& task) {
            return _wcsicmp(task.job.sourcePath.c_str(), invalidTransientPath.c_str()) == 0;
        });
    int invalidTaskIndex = invalidTaskIt == window->m_batch.batchTasks.end()
        ? -1
        : static_cast<int>(invalidTaskIt - window->m_batch.batchTasks.begin());
    std::wstring failureSummary = window->BuildActiveWorkText();
    std::wstring failureSummaryLower = failureSummary;
    std::transform(failureSummaryLower.begin(), failureSummaryLower.end(), failureSummaryLower.begin(), towlower);
    bool failureSummaryMentionsFailure =
        failureSummary.find(L"失败") != std::wstring::npos ||
        failureSummaryLower.find(L"fail") != std::wstring::npos;
    if (!invalidImportFinished ||
        invalidTaskIndex < 0 ||
        !failureSummaryMentionsFailure ||
        failureSummaryLower.find(L"batch recognition complete") != std::wstring::npos) {
        return fail(L"Transient decode failure did not preserve a failed final active-work summary.");
    }

    DashboardWindowTestClearOverrides();
    resetWindowState();

    std::wstring importRoot = runRoot + L"_import_command";
    if (!BatchOcrWriter::EnsureDirectory(importRoot)) {
        return fail(L"Failed to create import command contract output root.");
    }
    g_dashboardWindowTestBatchOutputRoot = importRoot;
    g_dashboardWindowTestImportFiles = { resolvedPdfPath };
    g_dashboardWindowTestUsePdfImportOptions = true;
    g_dashboardWindowTestPdfImportOptions = DashboardPdfImportOptions{};
    g_dashboardWindowTestPdfImportOptions.pageRange = L"1";
    g_dashboardWindowTestPdfImportOptions.pdfRenderDpi = 144;
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_DASH_IMPORT, BN_CLICKED), reinterpret_cast<LPARAM>(window->m_importBtn));
    bool importFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        if (DashboardStatePdfRenderInFlight(window->m_dashboardState) != 0 ||
            DashboardStateIsOcrBusy(window->m_dashboardState) ||
            !window->m_batch.dropQueue.empty()) {
            return false;
        }
        if (window->m_batch.activePdfJobs.size() != 1) return false;
        const BatchOcrPdfJob& active = window->m_batch.activePdfJobs.front();
        if (active.pages.size() != 1) return false;
        return active.pages.front().status == BatchOcrTaskStatus::Completed;
    });
    DashboardWindowTestClearOverrides();
    if (!importFinished) {
        return fail(L"Dashboard Import command PDF path did not finish through picker override.");
    }
    BatchOcrPdfJob importActive = window->m_batch.activePdfJobs.front();
    std::wstring importManifest;
    if (DashboardStateLastBatchOutputRoot(window->m_dashboardState) != importRoot ||
        importActive.pageRange != L"1" ||
        importActive.pdfRenderDpi != 144 ||
        importActive.pages.empty() ||
        importActive.thumbnailPath.empty() ||
        !PathFileExistsW(importActive.thumbnailPath.c_str()) ||
        !DashboardWindowTestReadUtf8File(importActive.manifestPath, importManifest) ||
        !DashboardWindowTestContains(importManifest, L"\"thumbnailPath\": \"thumbnail.png\"") ||
        !PathFileExistsW(importActive.pages.front().markdownPath.c_str()) ||
        !window->m_history.model.items.empty() ||
        BuildDashboardSourceProjection(
            window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items).size() != 1) {
        return fail(L"Dashboard Import command did not preserve PDF output/options or write page output.");
    }
    std::vector<BYTE> stableCoverBefore;
    if (!DashboardWindowTestReadBinaryFile(importActive.thumbnailPath, stableCoverBefore)) {
        return fail(L"Failed to read stable PDF cover for stale-generation contract.");
    }
    uint64_t staleCoverGeneration = DashboardStateOcrGeneration(window->m_dashboardState) > 0
        ? DashboardStateOcrGeneration(window->m_dashboardState) - 1
        : DashboardStateOcrGeneration(window->m_dashboardState) + 1;
    std::wstring staleCoverCandidate = JoinPathWide(
        importActive.outputDir,
        L"thumbnail.g" + std::to_wstring(staleCoverGeneration) + L".contract.candidate.png");
    if (!CopyFileW(sourcePageImage.c_str(), staleCoverCandidate.c_str(), FALSE)) {
        return fail(L"Failed to prepare stale PDF cover candidate.");
    }
    auto* staleCover = new DashboardPdfCoverResult();
    staleCover->generation = staleCoverGeneration;
    staleCover->jobKey = DashboardPdfJobTreeKey(importActive);
    staleCover->manifestPath = importActive.manifestPath;
    staleCover->sourcePath = importActive.sourcePath;
    staleCover->outputDir = importActive.outputDir;
    staleCover->candidatePath = staleCoverCandidate;
    staleCover->render.success = true;
    staleCover->render.candidatePath = staleCoverCandidate;
    window->HandlePdfCoverComplete(staleCover);
    std::vector<BYTE> stableCoverAfterStale;
    if (PathFileExistsW(staleCoverCandidate.c_str()) ||
        !DashboardWindowTestReadBinaryFile(importActive.thumbnailPath, stableCoverAfterStale) ||
        stableCoverAfterStale != stableCoverBefore) {
        return fail(L"Stale PDF cover completion modified the stable cover or leaked its candidate.");
    }

    std::wstring wrongGenerationCandidate = JoinPathWide(
        importActive.outputDir,
        L"thumbnail.g" + std::to_wstring(DashboardStateOcrGeneration(window->m_dashboardState) + 1) +
            L".wrong-name.candidate.png");
    if (!CopyFileW(sourcePageImage.c_str(), wrongGenerationCandidate.c_str(), FALSE)) {
        return fail(L"Failed to prepare wrong-generation-name PDF cover candidate.");
    }
    auto* wrongNameCover = new DashboardPdfCoverResult();
    wrongNameCover->generation = DashboardStateOcrGeneration(window->m_dashboardState);
    wrongNameCover->jobKey = DashboardPdfJobTreeKey(importActive);
    wrongNameCover->manifestPath = importActive.manifestPath;
    wrongNameCover->sourcePath = importActive.sourcePath;
    wrongNameCover->outputDir = importActive.outputDir;
    wrongNameCover->candidatePath = wrongGenerationCandidate;
    wrongNameCover->render.success = true;
    wrongNameCover->render.candidatePath = wrongGenerationCandidate;
    window->HandlePdfCoverComplete(wrongNameCover);
    std::vector<BYTE> stableCoverAfterWrongName;
    if (!PathFileExistsW(wrongGenerationCandidate.c_str()) ||
        !DashboardWindowTestReadBinaryFile(importActive.thumbnailPath, stableCoverAfterWrongName) ||
        stableCoverAfterWrongName != stableCoverBefore) {
        return fail(L"PDF cover handler accepted or modified an unowned candidate with the wrong generation name.");
    }
    DeleteFileW(wrongGenerationCandidate.c_str());
    const BatchOcrTaskStatus statusBeforeCoverFailure = window->m_batch.activePdfJobs.front().status;
    const std::wstring thumbnailBeforeCoverFailure = window->m_batch.activePdfJobs.front().thumbnailPath;
    std::wstring failedCoverCandidate = JoinPathWide(
        importActive.outputDir,
        L"thumbnail.g" + std::to_wstring(DashboardStateOcrGeneration(window->m_dashboardState)) +
            L".failed.contract.candidate.png");
    if (!CopyFileW(sourcePageImage.c_str(), failedCoverCandidate.c_str(), FALSE)) {
        return fail(L"Failed to prepare non-fatal PDF cover failure candidate.");
    }
    auto* failedCover = new DashboardPdfCoverResult();
    failedCover->generation = DashboardStateOcrGeneration(window->m_dashboardState);
    failedCover->jobKey = DashboardPdfJobTreeKey(importActive);
    failedCover->manifestPath = importActive.manifestPath;
    failedCover->sourcePath = importActive.sourcePath;
    failedCover->outputDir = importActive.outputDir;
    failedCover->candidatePath = failedCoverCandidate;
    failedCover->render.success = false;
    failedCover->render.candidatePath = failedCoverCandidate;
    failedCover->render.error = L"injected cover failure";
    window->HandlePdfCoverComplete(failedCover);
    if (PathFileExistsW(failedCoverCandidate.c_str()) ||
        window->m_batch.activePdfJobs.front().status != statusBeforeCoverFailure ||
        window->m_batch.activePdfJobs.front().thumbnailPath != thumbnailBeforeCoverFailure) {
        return fail(L"PDF cover failure changed OCR status/cover state or leaked its candidate.");
    }

    DashboardWindowTestClearOverrides();
    resetWindowState();

    std::wstring folderSourceRoot = runRoot + L"_folder_import_source";
    std::wstring folderNested = folderSourceRoot + L"\\nested";
    std::wstring folderExcluded = folderSourceRoot + L"\\skip_me";
    if (!BatchOcrWriter::EnsureDirectory(folderNested) ||
        !BatchOcrWriter::EnsureDirectory(folderExcluded)) {
        return fail(L"Failed to create folder import source tree.");
    }
    auto writeFolderImportPng = [&](const std::wstring& path) -> bool {
        HBITMAP bitmap = DashboardWindowTestCreateTextBitmap();
        if (!bitmap) return false;
        bool saved = DashboardWindowTestSaveBitmapAsPng(bitmap, path);
        DeleteObject(bitmap);
        return saved && PathFileExistsW(path.c_str());
    };
    if (!writeFolderImportPng(folderSourceRoot + L"\\root.png") ||
        !writeFolderImportPng(folderNested + L"\\nested.png") ||
        !writeFolderImportPng(folderExcluded + L"\\excluded.png")) {
        return fail(L"Failed to write folder import PNG fixtures.");
    }

    g_dashboardWindowTestUseFolderImportOptions = true;
    g_dashboardWindowTestFolderImportRecursive = false;
    g_dashboardWindowTestFolderImportMaxDepth = kFolderImportDefaultMaxDepth;
    g_dashboardWindowTestFolderImportExcludePatterns.clear();
    window->QueueImageFiles({ folderSourceRoot });
    bool nonRecursiveFolderImportFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) && window->m_batch.dropQueue.empty() && window->m_history.model.items.size() == 1;
    });
    DashboardWindowTestClearOverrides();
    if (!nonRecursiveFolderImportFinished ||
        DashboardStateIsFolderImportRecursive(window->m_dashboardState) ||
        DashboardStateFolderImportMaxDepth(window->m_dashboardState) != 0 ||
        window->m_history.model.items.size() != 1) {
        return fail(L"Folder import non-recursive option did not limit scanning to root files.");
    }

    resetWindowState();
    std::wstring folderImportOutputRoot = runRoot + L"_folder_import_output";
    if (!BatchOcrWriter::EnsureDirectory(folderImportOutputRoot)) {
        return fail(L"Failed to create folder import output root.");
    }
    g_dashboardWindowTestBatchOutputRoot = folderImportOutputRoot;
    g_dashboardWindowTestUseFolderImportOptions = true;
    g_dashboardWindowTestFolderImportRecursive = true;
    g_dashboardWindowTestFolderImportMaxDepth = 4;
    g_dashboardWindowTestFolderImportExcludePatterns = L"skip_me";
    window->QueueImageFiles({ folderSourceRoot });
    bool recursiveFolderImportFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) && window->m_batch.dropQueue.empty() && window->m_history.model.items.size() == 2;
    });
    DashboardWindowTestClearOverrides();
    if (!recursiveFolderImportFinished ||
        !DashboardStateIsFolderImportRecursive(window->m_dashboardState) ||
        DashboardStateFolderImportMaxDepth(window->m_dashboardState) != 4 ||
        DashboardStateFolderImportExcludePatterns(window->m_dashboardState) != L"skip_me" ||
        window->m_history.model.items.size() != 2 ||
        BuildDashboardSourceProjection(
            window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items).size() != 2 ||
        !DashboardStateVisibleHistoryIndices(window->m_dashboardState).empty()) {
        return fail(L"Folder import recursive/exclude options did not produce the expected scan result.");
    }

    resetWindowState();
    std::wstring folderDropOutputRoot = runRoot + L"_folder_drop_output";
    if (!BatchOcrWriter::EnsureDirectory(folderDropOutputRoot)) {
        return fail(L"Failed to create folder drop contract output root.");
    }
    g_dashboardWindowTestBatchOutputRoot = folderDropOutputRoot;
    g_dashboardWindowTestUseFolderImportOptions = true;
    g_dashboardWindowTestFolderImportRecursive = true;
    g_dashboardWindowTestFolderImportMaxDepth = 4;
    g_dashboardWindowTestFolderImportExcludePatterns = L"skip_me";
    window->QueueImageFiles({ folderSourceRoot });
    bool folderDropFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            window->m_history.model.items.size() == 2 &&
            window->m_batch.batchTasks.size() == 2;
    });
    DashboardWindowTestClearOverrides();
    if (!folderDropFinished ||
        DashboardStateLastBatchOutputRoot(window->m_dashboardState) != folderDropOutputRoot ||
        window->m_history.model.items.size() != 2 ||
        window->m_batch.batchTasks.size() != 2 ||
        BuildDashboardSourceProjection(
            window->m_batch.batchTasks, window->m_batch.activePdfJobs, window->m_history.model.items).size() != 2 ||
        !DashboardStateVisibleHistoryIndices(window->m_dashboardState).empty()) {
        return fail(L"Folder drop path did not scan the directory through the unified queue.");
    }
    for (const auto& task : window->m_batch.batchTasks) {
        if (task.status != BatchOcrTaskStatus::Completed ||
            task.job.markdownPath.empty() ||
            !PathFileExistsW(task.job.markdownPath.c_str())) {
            return fail(L"Folder drop path did not write completed batch outputs.");
        }
    }
    window->ApplyFilter(L"root");
    std::vector<SourceRailTaskRow> importTaskRows = window->BuildSourceRailTaskRows();
    if (importTaskRows.empty() ||
        importTaskRows.front().kind != SourceRailTaskRowKind::ImageTask) {
        return fail(L"Source Rail search did not expose the matching image batch task row.");
    }
    int imageTaskY = max(1, window->m_metrics.railHeaderH) +
        max(1, window->m_metrics.batchTaskItemH) / 2;
    SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, 0, MAKELPARAM(60, imageTaskY));
    const DashboardBatchTaskItem* selectedImageTask = window->GetSelectedImageTask();
    if (!DashboardStateHasImageTaskSelection(window->m_dashboardState) ||
        !selectedImageTask ||
        DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) >= 0 ||
        !window->m_gdiplusImage ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub") ||
        window->GetCurrentOutputFolder() != selectedImageTask->job.outputDir) {
        return fail(L"Source Rail image batch task row did not activate Canvas, Result and output folder selection.");
    }
    if (_wcsicmp(window->GetCurrentRevealPath().c_str(), selectedImageTask->job.markdownPath.c_str()) != 0) {
        return fail(L"Source Rail image batch task reveal path should target its markdown output.");
    }
    if (selectedImageTask->job.blocks.size() != 2 ||
        selectedImageTask->job.blocks[0].id != L"stub:block:title") {
        return fail(L"Source Rail image batch task did not preserve OCR layout blocks.");
    }
    std::wstring imageTaskContentJson;
    if (!DashboardWindowTestReadUtf8File(selectedImageTask->job.contentJsonPath, imageTaskContentJson) ||
        !DashboardWindowTestContains(imageTaskContentJson, L"\"blocks\"") ||
        !DashboardWindowTestContains(imageTaskContentJson, L"\"rawOcrJson\"") ||
        !DashboardWindowTestContains(imageTaskContentJson, L"layoutParsingResults") ||
        !DashboardWindowTestContains(imageTaskContentJson, L"\"blocksJsonPath\"") ||
        !DashboardWindowTestContains(imageTaskContentJson, L"\"layoutImagePath\"") ||
        !DashboardWindowTestContains(imageTaskContentJson, L"\"debugOutputImagesPath\"") ||
        !DashboardWindowTestContains(imageTaskContentJson, L"stub:block:title") ||
        !DashboardWindowTestContains(imageTaskContentJson, L"doc_title")) {
        return fail(L"Source Rail image batch task content JSON did not persist layout blocks/raw OCR JSON.");
    }
    if (!PathFileExistsW(DashboardWindowTestPathWithSuffix(selectedImageTask->job.contentJsonPath, L".blocks.json").c_str()) ||
        !PathFileExistsW(DashboardWindowTestPathWithSuffix(selectedImageTask->job.contentJsonPath, L".layout.png").c_str()) ||
        !PathFileExistsW(DashboardWindowTestPathWithSuffix(selectedImageTask->job.contentJsonPath, L".output_images.json").c_str())) {
        return fail(L"Source Rail image batch task did not write layout blocks JSON, visualized PNG, and outputImages sidecar.");
    }
    std::vector<OcrLayoutBlock> parsedContentBlocks = ParseOcrLayoutBlocks(imageTaskContentJson, 0);
    if (parsedContentBlocks.size() != 2 ||
        parsedContentBlocks[0].bbox.left != 32 ||
        parsedContentBlocks[0].bbox.top != 28 ||
        parsedContentBlocks[0].content.find(L"\"bbox\"") == std::wstring::npos) {
        return fail(L"Source Rail image batch task content JSON block parser confused content text with geometry fields.");
    }
    OcrDashboardHistoryItem sparseOrderHistory;
    for (int i = 0; i < 4; ++i) {
        OcrLayoutBlock block;
        block.id = L"legacy-order:" + std::to_wstring(i + 1);
        block.pageIndex = i == 3 ? 1 : 0;
        block.order = i == 0 ? 17 : (i == 1 ? 214 : (i == 2 ? 216 : 294));
        block.label = i == 1 ? L"image" : (i == 2 ? L"figure_title" : L"text");
        block.bbox = i == 0
            ? RECT{0, 0, 0, 0}
            : RECT{10, 10 + i * 30, 200, 35 + i * 30};
        sparseOrderHistory.blocks.push_back(std::move(block));
    }
    std::vector<DashboardOcrBlock> normalizedLegacyBlocks =
        window->BuildBlocksForHistoryItem(sparseOrderHistory);
    if (normalizedLegacyBlocks.size() != 3 ||
        normalizedLegacyBlocks[0].order != 1 ||
        normalizedLegacyBlocks[1].order != 2 ||
        normalizedLegacyBlocks[2].order != 1) {
        return fail(L"Dashboard did not normalize sparse persisted block orders per page after filtering invalid rectangles.");
    }
    window->RefreshCurrentBlocks();
    if (window->m_canvas.currentBlocks.size() != 2 ||
        window->m_canvas.currentBlocks[0].id != L"stub:block:title" ||
        window->m_canvas.currentBlocks[0].displayLabel != L"Title") {
        return fail(L"Preview block model did not load image task layout blocks.");
    }
    std::wstring imagePreviewSource = window->GetCurrentPreviewSourceMarkdown();
    if (!DashboardWindowTestContains(imagePreviewSource, L"stub") ||
        DashboardWindowTestContains(imagePreviewSource, L"<!-- source:")) {
        return fail(L"Image Preview source must use content JSON body Markdown without generated headers.");
    }
    if (!window->PersistPreviewMarkdownEdit(L"stub body sync contract",
            DashboardWindowTestSourceEdit(imagePreviewSource, L"stub"))) {
        return fail(L"Image Preview body edit did not persist transactionally.");
    }
    std::wstring syncedImageMarkdown;
    std::wstring syncedImageJson;
    std::wstring syncedImageBlocksJson;
    if (!DashboardWindowTestReadUtf8File(selectedImageTask->job.markdownPath, syncedImageMarkdown) ||
        !DashboardWindowTestReadUtf8File(selectedImageTask->job.contentJsonPath, syncedImageJson) ||
        !DashboardWindowTestReadUtf8File(
            DashboardWindowTestPathWithSuffix(selectedImageTask->job.contentJsonPath, L".blocks.json"),
            syncedImageBlocksJson)) {
        return fail(L"Image Preview body edit artifacts could not be reread.");
    }
    std::wstring syncedImageJsonBody = UnescapeJsonString(ExtractJsonField(syncedImageJson, L"markdown"));
    if (!DashboardWindowTestContains(syncedImageMarkdown, L"<!-- source:") ||
        !DashboardWindowTestContains(syncedImageMarkdown, L"stub body sync contract") ||
        DashboardWindowTestContains(syncedImageJsonBody, L"<!-- source:") ||
        !DashboardWindowTestContains(syncedImageJsonBody, L"stub body sync contract") ||
        !DashboardWindowTestContains(syncedImageJson, window->m_canvas.currentBlocks[0].id) ||
        !DashboardWindowTestContains(syncedImageBlocksJson, window->m_canvas.currentBlocks[0].id) ||
        PathFileExistsW((selectedImageTask->job.contentJsonPath + L".preview-edit.journal").c_str())) {
        return fail(L"Image Preview edit mixed generated Markdown headers into content JSON.");
    }
    if (!window->PersistPreviewMarkdownEdit(L"stub",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"stub body sync contract"))) {
        return fail(L"Image Preview body edit fixture could not be restored.");
    }
    {
        const std::wstring markdownPath = selectedImageTask->job.markdownPath;
        const std::wstring contentJsonPath = selectedImageTask->job.contentJsonPath;
        const std::wstring blocksJsonPath =
            DashboardWindowTestPathWithSuffix(contentJsonPath, L".blocks.json");
        const std::wstring journalPath = contentJsonPath + L".preview-edit.journal";
        std::wstring committedMarkdown;
        std::wstring committedJson;
        std::wstring committedBlocksJson;
        if (!DashboardWindowTestReadUtf8File(markdownPath, committedMarkdown) ||
            !DashboardWindowTestReadUtf8File(contentJsonPath, committedJson) ||
            !DashboardWindowTestReadUtf8File(blocksJsonPath, committedBlocksJson)) {
            return fail(L"Preview journal recovery fixture could not read committed artifacts.");
        }
        std::wstring journal = L"{\n  \"version\": 1,\n";
        journal += L"  \"markdownDocument\": \"" + EscapeJsonString(committedMarkdown) + L"\",\n";
        journal += L"  \"contentJson\": \"" + EscapeJsonString(committedJson) + L"\",\n";
        journal += L"  \"blocksJson\": \"" + EscapeJsonString(committedBlocksJson) + L"\"\n}\n";
        if (!DashboardWindowTestWriteUtf8File(journalPath, journal) ||
            !DashboardWindowTestWriteUtf8File(markdownPath, L"stale markdown") ||
            !DashboardWindowTestWriteUtf8File(contentJsonPath, L"{\"markdown\":\"stale\",\"blocks\":[]}") ||
            !DashboardWindowTestWriteUtf8File(blocksJsonPath, L"{\"blocks\":[]}")) {
            return fail(L"Preview journal recovery fixture could not simulate an interrupted commit.");
        }
        window->RefreshCurrentBlocks();
        std::wstring recoveredMarkdown;
        std::wstring recoveredJson;
        std::wstring recoveredBlocksJson;
        if (PathFileExistsW(journalPath.c_str()) || DashboardStateIsPreviewPersistenceBlocked(window->m_dashboardState) ||
            !DashboardWindowTestReadUtf8File(markdownPath, recoveredMarkdown) ||
            !DashboardWindowTestReadUtf8File(contentJsonPath, recoveredJson) ||
            !DashboardWindowTestReadUtf8File(blocksJsonPath, recoveredBlocksJson) ||
            recoveredMarkdown != committedMarkdown || recoveredJson != committedJson ||
            recoveredBlocksJson != committedBlocksJson || window->m_canvas.currentBlocks.empty()) {
            return fail(L"Preview journal did not recover an interrupted cross-file commit.");
        }
    }

    auto mutableImageTask = std::find_if(window->m_batch.batchTasks.begin(), window->m_batch.batchTasks.end(),
        [&](const DashboardBatchTaskItem& task) {
            return window->IsImageTaskSelectionForTask(task);
        });
    if (mutableImageTask == window->m_batch.batchTasks.end()) {
        return fail(L"Could not locate mutable image task for Preview rollback contract.");
    }
    std::wstring validImageContentJsonPath = mutableImageTask->job.contentJsonPath;
    std::wstring originalPreviewBlockContent = window->m_canvas.currentBlocks[0].content;
    mutableImageTask->job.contentJsonPath = JoinPathWide(runRoot, L"missing_preview_transaction\\content.json");
    bool invalidSaveAccepted = window->ApplyPreviewBlockEdit(
        window->m_canvas.currentBlocks[0].id,
        L"must be rolled back",
        DashboardWindowTestSourceEdit(window->GetCurrentPreviewSourceMarkdown(), L"stub"));
    mutableImageTask->job.contentJsonPath = validImageContentJsonPath;
    const DashboardOcrBlock* rolledBackBlock = window->FindCurrentBlockById(L"stub:block:title");
    if (invalidSaveAccepted || !rolledBackBlock || rolledBackBlock->content != originalPreviewBlockContent) {
        return fail(L"Preview edit failure did not roll back the in-memory block content.");
    }
    if (!window->ApplyPreviewBlockEdit(
            L"stub:block:title",
            L"## edited image-task title",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"stub"))) {
        return fail(L"Image-task Preview edit did not capture and persist an OCR baseline.");
    }
    const DashboardOcrBlock* imageBaselineBlock =
        window->FindCurrentBlockById(L"stub:block:title");
    std::wstring imageBaselineJson;
    if (!imageBaselineBlock || !imageBaselineBlock->editBaseline.has_value() ||
        imageBaselineBlock->editBaseline->content != originalPreviewBlockContent ||
        imageBaselineBlock->editBaseline->sourceSegment != L"stub" ||
        !DashboardWindowTestReadUtf8File(
            mutableImageTask->job.contentJsonPath, imageBaselineJson) ||
        !DashboardWindowTestContains(imageBaselineJson, L"\"editBaseline\"") ||
        !DashboardWindowTestContains(imageBaselineJson, L"\"sourceSegment\": \"stub\"")) {
        return fail(L"Image-task OCR baseline was not written to content JSON.");
    }
    if (!window->ApplyPreviewBlockEdit(
            L"stub:block:title",
            L"### edited image-task title twice",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"## edited image-task title"))) {
        return fail(L"Second image-task Preview edit did not persist.");
    }
    const DashboardOcrBlock* imageSecondEdit =
        window->FindCurrentBlockById(L"stub:block:title");
    if (!imageSecondEdit || !imageSecondEdit->editBaseline.has_value() ||
        imageSecondEdit->editBaseline->content != originalPreviewBlockContent ||
        imageSecondEdit->editBaseline->sourceSegment != L"stub") {
        return fail(L"Second image-task edit overwrote the immutable OCR baseline.");
    }
    mutableImageTask->job.contentJsonPath =
        JoinPathWide(runRoot, L"missing_preview_restore_transaction\\content.json");
    bool invalidRestoreAccepted = window->RestorePreviewBlockOriginal(
        L"stub:block:title",
        DashboardWindowTestSourceEdit(
            window->GetCurrentPreviewSourceMarkdown(), L"### edited image-task title twice"));
    mutableImageTask->job.contentJsonPath = validImageContentJsonPath;
    const DashboardOcrBlock* restoreRolledBackBlock =
        window->FindCurrentBlockById(L"stub:block:title");
    if (invalidRestoreAccepted || !restoreRolledBackBlock || !restoreRolledBackBlock->edited ||
        !restoreRolledBackBlock->editBaseline.has_value() ||
        restoreRolledBackBlock->content != L"### edited image-task title twice") {
        return fail(L"Failed Restore OCR did not preserve the edited block and its baseline.");
    }
    if (!window->RestorePreviewBlockOriginal(
            L"stub:block:title",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"### edited image-task title twice"))) {
        return fail(L"Image-task Restore OCR transaction failed.");
    }
    const DashboardOcrBlock* imageRestoredBlock =
        window->FindCurrentBlockById(L"stub:block:title");
    std::wstring imageRestoredJson;
    if (!imageRestoredBlock || imageRestoredBlock->edited ||
        imageRestoredBlock->editBaseline.has_value() ||
        imageRestoredBlock->content != originalPreviewBlockContent ||
        !DashboardWindowTestReadUtf8File(
            mutableImageTask->job.contentJsonPath, imageRestoredJson) ||
        DashboardWindowTestContains(imageRestoredJson, L"\"editBaseline\"") ||
        !DashboardWindowTestContains(
            UnescapeJsonString(ExtractJsonField(imageRestoredJson, L"markdown")), L"stub")) {
        return fail(L"Image-task Restore OCR did not recover artifacts and clear edit state.");
    }
    for (auto& block : window->m_canvas.currentBlocks) {
        if (block.id == L"stub:block:title") {
            block.edited = true;
            block.editBaseline.reset();
        }
    }
    if (window->RestorePreviewBlockOriginal(
            L"stub:block:title",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"stub"))) {
        return fail(L"Legacy edited block without a retained baseline was incorrectly restorable.");
    }
    for (auto& block : window->m_canvas.currentBlocks) {
        if (block.id == L"stub:block:title") block.edited = false;
    }
    window->RebuildBlockRuntimeIndex();

    window->m_dashboardState.canvasView.showLayoutOverlay = false;
    if (window->HitTestImageBlock(40, 40) != -1) {
        return fail(L"Image block hit-test should be disabled when layout overlay is off.");
    }
    window->m_dashboardState.canvasView.showLayoutOverlay = true;
    window->m_dashboardState.canvasView.zoom = 1.0f;
    window->m_dashboardState.canvasView.panX = 0.0f;
    window->m_dashboardState.canvasView.panY = 0.0f;
    int imageBlockHit = window->HitTestImageBlock(40, 40);
    if (imageBlockHit < 0 ||
        window->m_canvas.currentBlocks[(size_t)imageBlockHit].id != L"stub:block:title") {
        return fail(L"Image layout overlay hit-test did not resolve the title block.");
    }
    window->SetHoveredBlock(window->m_canvas.currentBlocks[(size_t)imageBlockHit].id);
    RECT imageRc = {};
    GetClientRect(window->m_imageArea, &imageRc);
    int imageW = max(1, imageRc.right - imageRc.left);
    int imageH = max(1, imageRc.bottom - imageRc.top);
    RECT copyRc = window->GetImageBlockCopyButtonRect(window->m_canvas.currentBlocks[(size_t)imageBlockHit], imageW, imageH);
    if (copyRc.left < 0 || copyRc.top < 0 || copyRc.right > imageW || copyRc.bottom > imageH ||
        copyRc.right <= copyRc.left || copyRc.bottom <= copyRc.top) {
        return fail(L"Image block floating Copy button rect was not clamped inside the image area.");
    }
    int copyCenterX = (copyRc.left + copyRc.right) / 2;
    int copyCenterY = (copyRc.top + copyRc.bottom) / 2;
    int copyButtonHit = window->HitTestImageBlockCopyButton(copyCenterX, copyCenterY);
    if (copyButtonHit < 0 ||
        window->m_canvas.currentBlocks[(size_t)copyButtonHit].id != L"stub:block:title") {
        return fail(L"Image block floating Copy button hit-test did not resolve the hovered block.");
    }
    RECT blockScreenRc = {
        (int)(window->m_dashboardState.canvasView.panX + window->m_canvas.currentBlocks[(size_t)imageBlockHit].bbox.left * window->m_dashboardState.canvasView.zoom),
        (int)(window->m_dashboardState.canvasView.panY + window->m_canvas.currentBlocks[(size_t)imageBlockHit].bbox.top * window->m_dashboardState.canvasView.zoom),
        (int)(window->m_dashboardState.canvasView.panX + window->m_canvas.currentBlocks[(size_t)imageBlockHit].bbox.right * window->m_dashboardState.canvasView.zoom),
        (int)(window->m_dashboardState.canvasView.panY + window->m_canvas.currentBlocks[(size_t)imageBlockHit].bbox.bottom * window->m_dashboardState.canvasView.zoom)
    };
    int bridgeX = ((blockScreenRc.left + blockScreenRc.right) / 2 + copyCenterX) / 2;
    int bridgeY = ((blockScreenRc.top + blockScreenRc.bottom) / 2 + copyCenterY) / 2;
    if (!window->ShouldPreserveImageBlockCopyHover(bridgeX, bridgeY)) {
        return fail(L"Image block floating Copy hover bridge did not preserve hover between block and button.");
    }
    DashboardOcrBlock edgeBlock = window->m_canvas.currentBlocks[(size_t)imageBlockHit];
    RECT edgeBoxes[] = {
        {0, 0, 18, 18},
        {imageW - 18, 0, imageW, 18},
        {0, imageH - 18, 18, imageH},
        {imageW - 18, imageH - 18, imageW, imageH}
    };
    for (const RECT& edgeBox : edgeBoxes) {
        edgeBlock.bbox = edgeBox;
        RECT edgeCopyRc = window->GetImageBlockCopyButtonRect(edgeBlock, imageW, imageH);
        if (edgeCopyRc.left < 0 || edgeCopyRc.top < 0 ||
            edgeCopyRc.right > imageW || edgeCopyRc.bottom > imageH ||
            edgeCopyRc.right <= edgeCopyRc.left || edgeCopyRc.bottom <= edgeCopyRc.top) {
            return fail(L"Image block floating Copy button failed edge clamping.");
        }
    }
    RECT stripRc = window->ImageControlStripRect(imageW, imageH);
    if (window->HitTestImageControl((stripRc.left + stripRc.right) / 2, (stripRc.top + stripRc.bottom) / 2) <= 0) {
        return fail(L"Image preview control strip did not expose zoom controls.");
    }
    float zoomBeforeControl = window->m_dashboardState.canvasView.zoom;
    if (!window->HandleImageControlClick(5) || window->m_dashboardState.canvasView.zoom <= zoomBeforeControl) {
        return fail(L"Image preview zoom-in control did not update zoom.");
    }
    if (!window->HandleImageControlClick(4) || window->m_dashboardState.canvasView.viewMode != ImageViewMode::Fit) {
        return fail(L"Image preview fit control did not reset view mode.");
    }
    window->SetSelectedBlock(window->m_canvas.currentBlocks[(size_t)imageBlockHit].id, true);
    if (DashboardStateHoveredBlockId(window->m_dashboardState) != L"stub:block:title" ||
        DashboardStateSelectedBlockId(window->m_dashboardState) != L"stub:block:title") {
        return fail(L"Image layout overlay hover/click selection did not update block linkage state.");
    }
    if (window->m_canvas.currentBlocks.size() > 1) {
        window->m_canvas.currentBlocks[1].content.clear();
        window->m_canvas.currentBlocks[1].confidence = 0.2;
        window->RebuildBlockRuntimeIndex();
        if (!window->BlockHasIssue(window->m_canvas.currentBlocks[1])) {
            return fail(L"Image overlay issue detection did not flag empty low-confidence content.");
        }
    }
    window->CopySelectedBlockToClipboard();
    window->SetTextMode(DashboardTextMode::Text);
    if (!DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Source Rail image batch task Text mode did not read task output.");
    }
    window->SetTextMode(DashboardTextMode::Json);
    if (window->GetCurrentResultText().find(L"image") == std::wstring::npos) {
        return fail(L"Source Rail image batch task JSON mode did not expose task metadata.");
    }
    size_t historyBeforeImageTaskRerun = window->m_history.model.items.size();
    if (!window->RerunCurrentImageTask()) {
        return fail(L"Source Rail image batch task rerun command did not start.");
    }
    bool imageTaskRerunFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        const DashboardBatchTaskItem* task = window->GetSelectedImageTask();
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            task &&
            task->status == BatchOcrTaskStatus::Completed;
    });
    selectedImageTask = window->GetSelectedImageTask();
    window->SetTextMode(DashboardTextMode::Source);
    if (!imageTaskRerunFinished ||
        !DashboardStateHasImageTaskSelection(window->m_dashboardState) ||
        DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) >= 0 ||
        !selectedImageTask ||
        window->m_history.model.items.size() <= historyBeforeImageTaskRerun ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Source Rail image batch task rerun did not preserve task selection and refreshed output.");
    }
    window->ApplyFilter(L"");

    resetWindowState();

    std::wstring tiffSourcePath = runRoot + L"\\dashboard_multipage_source.tiff";
    std::wstring tiffOutputRoot = runRoot + L"_multipage_tiff_output";
    if (!DashboardWindowTestWriteMultiFrameTiff(tiffSourcePath) ||
        !BatchOcrWriter::EnsureDirectory(tiffOutputRoot)) {
        return fail(L"Failed to prepare multi-page TIFF import contract fixtures.");
    }
    g_dashboardWindowTestBatchOutputRoot = tiffOutputRoot;
    window->QueueImageFiles({ tiffSourcePath });
    bool multiPageTiffFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            window->m_history.model.items.size() == 2 &&
            window->m_batch.batchTasks.size() == 2;
    });
    DashboardWindowTestClearOverrides();
    if (!multiPageTiffFinished) {
        return fail(L"Multi-page TIFF import did not expand into two OCR image tasks.");
    }
    for (const auto& item : window->m_history.model.items) {
        if (item.imagePath.empty() ||
            !PathFileExistsW(item.imagePath.c_str()) ||
            !IsPathInOcrImageCache(item.imagePath) ||
            item.text.find(L"stub") == std::wstring::npos) {
            return fail(L"Multi-page TIFF import did not create cache-backed history rows.");
        }
    }
    for (const auto& task : window->m_batch.batchTasks) {
        if (task.status != BatchOcrTaskStatus::Completed ||
            task.job.sourcePath.empty() ||
            !IsPathInOcrImageCache(task.job.sourcePath) ||
            !PathFileExistsW(task.job.markdownPath.c_str()) ||
            !PathFileExistsW(task.job.contentJsonPath.c_str())) {
            return fail(L"Multi-page TIFF import did not create completed batch output for each frame.");
        }
    }

    resetWindowState();

    std::wstring workerRoot = runRoot + L"_worker_callback";
    if (!BatchOcrWriter::EnsureDirectory(workerRoot)) {
        return fail(L"Failed to create worker callback contract output root.");
    }
    BatchOcrPdfJob workerPdfJob;
    if (!controller.CreatePdfJob(resolvedPdfPath, workerRoot, workerPdfJob, setupError)) {
        return fail(setupError.empty()
            ? L"Failed to create worker callback PDF job."
            : setupError);
    }
    workerPdfJob.pageRange = L"1";
    workerPdfJob.pdfRenderDpi = 144;

    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        workerRoot,
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    window->StartPdfRenderJob(workerPdfJob);

    bool workerFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        if (DashboardStatePdfRenderInFlight(window->m_dashboardState) != 0 ||
            DashboardStateIsOcrBusy(window->m_dashboardState) ||
            !window->m_batch.dropQueue.empty()) {
            return false;
        }
        BatchOcrPdfJob* active = window->FindActivePdfJob(workerPdfJob);
        if (!active || active->pages.empty()) return false;
        return std::all_of(active->pages.begin(), active->pages.end(),
            [](const BatchOcrPdfPageJob& page) {
                return page.status == BatchOcrTaskStatus::Completed ||
                    page.status == BatchOcrTaskStatus::Failed ||
                    page.status == BatchOcrTaskStatus::Canceled;
            });
    });
    if (!workerFinished) {
        return fail(L"Dashboard PDF worker/OCR callback did not finish in the window contract.");
    }

    BatchOcrPdfJob* workerActive = window->FindActivePdfJob(workerPdfJob);
    if (!workerActive || workerActive->pages.empty()) {
        return fail(L"Dashboard PDF worker did not restore an active PDF job.");
    }
    if (!std::all_of(workerActive->pages.begin(), workerActive->pages.end(),
            [](const BatchOcrPdfPageJob& page) {
                return page.status == BatchOcrTaskStatus::Completed;
            })) {
        return fail(L"Dashboard PDF worker/OCR callback did not complete all rendered pages.");
    }
    const BatchOcrPdfPageJob& workerPage = workerActive->pages.front();
    if (window->m_history.model.items.size() != 0) {
        return fail(L"PDF worker/OCR callback should not append PDF pages to image history.");
    }
    if (!PathFileExistsW(workerPage.sourceImagePath.c_str()) ||
        !PathFileExistsW(workerPage.markdownPath.c_str()) ||
        !PathFileExistsW(workerPage.contentJsonPath.c_str()) ||
        !PathFileExistsW(workerActive->markdownPath.c_str()) ||
        !PathFileExistsW(workerActive->contentJsonPath.c_str())) {
        return fail(L"Dashboard PDF worker/OCR callback did not write expected page/document outputs.");
    }
    std::wstring workerPageMarkdown;
    if (!DashboardWindowTestReadUtf8File(workerPage.markdownPath, workerPageMarkdown) ||
        !DashboardWindowTestContains(workerPageMarkdown, L"stub")) {
        return fail(L"Dashboard PDF worker/OCR callback page markdown did not contain stub OCR text.");
    }
    std::wstring workerDocumentMarkdown;
    if (!DashboardWindowTestReadUtf8File(workerActive->markdownPath, workerDocumentMarkdown) ||
        !DashboardWindowTestContains(workerDocumentMarkdown, L"stub")) {
        return fail(L"Dashboard PDF worker/OCR callback document markdown did not contain stub OCR text.");
    }
    window->ActivateSourceRailPdfItem(0, workerPage.pageIndex, false);
    const int expectedWorkerSelectionPage = workerPage.pageIndex == 1 ? 0 : workerPage.pageIndex;
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != expectedWorkerSelectionPage ||
        !window->m_gdiplusImage ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Dashboard PDF worker/OCR callback did not refresh Source Rail, Canvas and Result selection.");
    }

    resetWindowState();

    if (!window->LoadBatchOutputSnapshot(runRoot, false, false)) {
        return fail(L"Dashboard window could not load the generated PDF batch snapshot.");
    }
    if (window->m_batch.activePdfJobs.empty() || window->m_batch.activePdfJobs.front().pages.empty()) {
        return fail(L"Dashboard window did not restore PDF job/page rows.");
    }
    BatchOcrPdfPageJob* restoredSecondPdfPage = nullptr;
    for (auto& page : window->m_batch.activePdfJobs.front().pages) {
        if (page.pageIndex == 2) {
            restoredSecondPdfPage = &page;
            break;
        }
    }
    if (!restoredSecondPdfPage ||
        restoredSecondPdfPage->blocks.size() != 1 ||
        restoredSecondPdfPage->blocks[0].pageIndex != 1 ||
        restoredSecondPdfPage->blocks[0].id.rfind(L"page_2:", 0) != 0 ||
        restoredSecondPdfPage->blocks[0].content != L"PDF block page 2") {
        return fail(L"Dashboard window did not normalize restored PDF page layout blocks.");
    }
    if (!PathFileExistsW(DashboardWindowTestPathWithSuffix(restoredSecondPdfPage->contentJsonPath, L".blocks.json").c_str()) ||
        !PathFileExistsW(DashboardWindowTestPathWithSuffix(restoredSecondPdfPage->contentJsonPath, L".layout.png").c_str())) {
        return fail(L"Dashboard window did not restore PDF page layout output artifacts.");
    }
    window->ActivateSourceRailPdfItem(0, 2, false);
    window->RefreshCurrentBlocks();
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 2 ||
        window->m_canvas.currentBlocks.size() != 1 ||
        window->m_canvas.currentBlocks[0].id.rfind(L"page_2:", 0) != 0 ||
        window->m_canvas.currentBlocks[0].pageIndex != 1) {
        return fail(L"Source Rail PDF page activation did not load the page-specific layout blocks.");
    }

    window->ActivateSourceRailPdfItem(0, selectedPageIndex, false);
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) || DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != selectedVisiblePageIndex) {
        return fail(L"Source Rail PDF page activation did not set PDF selection.");
    }
    if (!window->m_gdiplusImage) {
        return fail(L"Source Rail PDF page activation did not load the page image into Canvas.");
    }
    if (!DashboardWindowTestContains(window->GetCurrentResultText(), L"Window contract markdown page")) {
        return fail(L"Result Inspector did not show the selected PDF page markdown.");
    }
    std::wstring pdfPreviewSource = window->GetCurrentPreviewSourceMarkdown();
    if (!DashboardWindowTestContains(pdfPreviewSource, L"Window contract markdown page") ||
        DashboardWindowTestContains(pdfPreviewSource, L"<!-- page:")) {
        return fail(L"PDF Preview source must use page body Markdown without generated headers.");
    }
    if (!window->PersistPreviewMarkdownEdit(
            L"Window contract markdown page body sync",
            DashboardWindowTestSourceEdit(pdfPreviewSource, L"Window contract markdown page"))) {
        return fail(L"PDF Preview body edit did not persist transactionally.");
    }
    BatchOcrPdfPageJob* syncedPdfPage = nullptr;
    for (auto& page : window->m_batch.activePdfJobs.front().pages) {
        if (page.pageIndex == selectedPageIndex) {
            syncedPdfPage = &page;
            break;
        }
    }
    std::wstring syncedPdfMarkdown;
    std::wstring syncedPdfJson;
    if (!syncedPdfPage ||
        !DashboardWindowTestReadUtf8File(syncedPdfPage->markdownPath, syncedPdfMarkdown) ||
        !DashboardWindowTestReadUtf8File(syncedPdfPage->contentJsonPath, syncedPdfJson)) {
        return fail(L"PDF Preview body edit artifacts could not be reread.");
    }
    std::wstring syncedPdfJsonBody = UnescapeJsonString(ExtractJsonField(syncedPdfJson, L"markdown"));
    if (!DashboardWindowTestContains(syncedPdfMarkdown, L"<!-- page:") ||
        !DashboardWindowTestContains(syncedPdfMarkdown, L"Window contract markdown page body sync") ||
        DashboardWindowTestContains(syncedPdfJsonBody, L"<!-- page:") ||
        !DashboardWindowTestContains(syncedPdfJsonBody, L"Window contract markdown page body sync")) {
        return fail(L"PDF Preview edit removed page headers or polluted content JSON Markdown.");
    }
    if (!window->PersistPreviewMarkdownEdit(
            L"Window contract markdown page",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(),
                L"Window contract markdown page body sync"))) {
        return fail(L"PDF Preview body edit fixture could not be restored.");
    }
    if (window->CurrentPdfPageCount() < 2) {
        return fail(L"Image preview page control did not see the restored PDF page count.");
    }
    if (!window->ActivateAdjacentPdfPage(true) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 2 ||
        window->m_canvas.currentBlocks.size() != 1 ||
        window->m_canvas.currentBlocks[0].id.rfind(L"page_2:", 0) != 0 ||
        window->m_canvas.currentBlocks[0].content != L"PDF block page 2") {
        return fail(L"Image preview next-page control did not activate page-specific PDF blocks.");
    }
    if (!window->ActivateAdjacentPdfPage(false) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != selectedVisiblePageIndex ||
        window->m_canvas.currentBlocks.size() != 1 ||
        window->m_canvas.currentBlocks[0].content.find(L"PDF block page") == std::wstring::npos) {
        return fail(L"Image preview previous-page control did not restore the original PDF page blocks.");
    }

    BatchOcrPdfJob& previewAssetJob = window->m_batch.activePdfJobs.front();
    BatchOcrPdfPageJob* previewAssetPage = nullptr;
    for (auto& page : previewAssetJob.pages) {
        if (page.pageIndex == selectedPageIndex) {
            previewAssetPage = &page;
            break;
        }
    }
    if (!previewAssetPage) {
        return fail(L"Could not locate selected PDF page for preview asset proxy contract.");
    }
    if (!BatchOcrWriter::EnsureDirectory(previewAssetJob.assetsDir)) {
        return fail(L"Failed to create PDF preview asset directory.");
    }
    std::wstring previewAssetPath = JoinPathWide(previewAssetJob.assetsDir, L"page_0001_img_001.png");
    {
        HBITMAP bitmap = DashboardWindowTestCreateTextBitmap();
        if (!bitmap) return fail(L"Failed to create PDF preview asset bitmap fixture.");
        bool saved = DashboardWindowTestSaveBitmapAsPng(bitmap, previewAssetPath);
        DeleteObject(bitmap);
        if (!saved || !PathFileExistsW(previewAssetPath.c_str())) {
            return fail(L"Failed to save PDF preview asset bitmap fixture.");
        }
    }
    previewAssetPage->markdown =
        L"<div>"
        L"<img src=\"assets/page_0001_img_001.png\" alt=\"Seal\" width=\"21%\" />"
        L"<img src=\"../assets/page_0001_img_001.png\" alt=\"Standalone Seal\" width=\"21%\" />"
        L"</div>\r\n"
        L"![broken markdown image with no close\r\n"
        L"![Markdown Seal](assets/page_0001_img_001.png)";
    std::wstring previewMarkdown = window->GetCurrentPreviewMarkdown();
    if (!DashboardWindowTestContains(previewMarkdown, L"src=\"https://zencrop-preview-output.invalid/assets/page_0001_img_001.png?v=") ||
        DashboardWindowTestContains(previewMarkdown, L"src=\"assets/page_0001_img_001.png\"") ||
        DashboardWindowTestContains(previewMarkdown, L"src=\"../assets/page_0001_img_001.png\"") ||
        !DashboardWindowTestContains(previewMarkdown, L"![broken markdown image with no close") ||
        !DashboardWindowTestContains(previewMarkdown, L"![Markdown Seal](https://zencrop-preview-output.invalid/assets/page_0001_img_001.png?v=") ||
        !DashboardWindowTestContains(previewMarkdown, L"alt=\"Seal\"")) {
        return fail(L"PDF preview did not rewrite and version relative asset images through the preview output virtual host.");
    }
    HANDLE previewAssetHandle = CreateFileW(
        previewAssetPath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    FILETIME previewAssetWriteTime = {};
    if (previewAssetHandle == INVALID_HANDLE_VALUE ||
        !GetFileTime(previewAssetHandle, nullptr, nullptr, &previewAssetWriteTime)) {
        if (previewAssetHandle != INVALID_HANDLE_VALUE) CloseHandle(previewAssetHandle);
        return fail(L"Failed to read PDF preview asset timestamp for URL versioning.");
    }
    ULARGE_INTEGER advancedWriteTime = {};
    advancedWriteTime.LowPart = previewAssetWriteTime.dwLowDateTime;
    advancedWriteTime.HighPart = previewAssetWriteTime.dwHighDateTime;
    advancedWriteTime.QuadPart += 10000000ULL;
    previewAssetWriteTime.dwLowDateTime = advancedWriteTime.LowPart;
    previewAssetWriteTime.dwHighDateTime = advancedWriteTime.HighPart;
    bool updatedPreviewAssetTime =
        SetFileTime(previewAssetHandle, nullptr, nullptr, &previewAssetWriteTime) != FALSE;
    CloseHandle(previewAssetHandle);
    if (!updatedPreviewAssetTime || window->GetCurrentPreviewMarkdown() == previewMarkdown) {
        return fail(L"PDF preview asset URL version did not change after the asset changed.");
    }

    if (window->GetCurrentOutputFolder() != pdfJob.outputDir ||
        !window->m_openOutputBtn ||
        !IsWindowEnabled(window->m_openOutputBtn)) {
        return fail(L"Open Output should target and enable for the selected PDF job output folder.");
    }
    std::wstring activeRootRevealPath = window->m_batch.activePdfJobs.front().markdownPath;
    if (_wcsicmp(window->GetCurrentRevealPath().c_str(), activeRootRevealPath.c_str()) != 0) {
        return fail(L"Reveal path should keep the hidden Page 1 selection on the PDF document output.");
    }
    auto revealChild = std::find_if(
        window->m_batch.activePdfJobs.front().pages.begin(),
        window->m_batch.activePdfJobs.front().pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
    if (revealChild == window->m_batch.activePdfJobs.front().pages.end()) {
        return fail(L"Reveal path fixture has no visible Page 2+ child.");
    }
    const int revealChildIndex = revealChild->pageIndex;
    const std::wstring revealChildMarkdownPath = revealChild->markdownPath;
    window->ActivateSourceRailPdfItem(0, revealChildIndex, false);
    if (_wcsicmp(window->GetCurrentRevealPath().c_str(), revealChildMarkdownPath.c_str()) != 0) {
        return fail(L"Reveal path should target the selected visible PDF child markdown file.");
    }
    window->ActivateSourceRailPdfItem(0, 0, true);
    std::wstring activeJobRevealPath = window->m_batch.activePdfJobs.front().markdownPath;
    if (_wcsicmp(window->GetCurrentRevealPath().c_str(), activeJobRevealPath.c_str()) != 0) {
        return fail(L"Reveal path should target the selected PDF document markdown file.");
    }
    if (!window->RerunCurrentPdfSelection()) {
        return fail(L"Dashboard PDF job rerun command did not start from job selection.");
    }
    bool pdfJobRerunFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        if (DashboardStateIsOcrBusy(window->m_dashboardState) || !window->m_batch.dropQueue.empty()) return false;
        if (window->m_batch.activePdfJobs.empty() || window->m_batch.activePdfJobs.front().pages.empty()) return false;
        return std::all_of(
            window->m_batch.activePdfJobs.front().pages.begin(),
            window->m_batch.activePdfJobs.front().pages.end(),
            [](const BatchOcrPdfPageJob& page) {
                return page.status == BatchOcrTaskStatus::Completed;
            });
    });
    if (!pdfJobRerunFinished ||
        !DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Dashboard PDF job rerun did not preserve job selection and refreshed document output.");
    }
    window->ActivateSourceRailPdfItem(0, selectedPageIndex, false);
    if (!IsWindowEnabled(window->m_copyBtn)) {
        return fail(L"Copy button state is wrong for a PDF selection.");
    }

    DashboardStateSyncTextMode(window->m_dashboardState, DashboardTextMode::Preview, DashboardStateTextModeEffective(window->m_dashboardState));
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardStateTextModePreferred(window->m_dashboardState), DashboardTextMode::Preview);
    if (window->m_edit) SetWindowTextW(window->m_edit, L"stale preview text");
    window->FallbackPreviewToSource(L"Preview fallback contract");
    if (DashboardStateTextModeEffective(window->m_dashboardState) != DashboardTextMode::Source ||
        DashboardStateTextModePreferred(window->m_dashboardState) != DashboardTextMode::Preview) {
        return fail(L"Preview fallback must set effective Source while keeping preferred Preview.");
    }
    std::wstring fallbackEditText = GetWindowTextWide(window->m_edit);
    std::wstring fallbackResultText = window->GetCurrentResultText();
    bool fallbackHasCurrentContent =
        (DashboardWindowTestContains(fallbackResultText, L"Window contract markdown page") ||
         DashboardWindowTestContains(fallbackResultText, L"stub")) &&
        (DashboardWindowTestContains(fallbackEditText, L"Window contract markdown page") ||
         DashboardWindowTestContains(fallbackEditText, L"stub"));
    if (!fallbackHasCurrentContent) {
        return fail(L"Preview fallback did not expose Source content in the edit control.");
    }

    // Preferred mode is persisted independently of transient Preview fallback.
    // OCR-complete and reopen both default to Preview when no key exists; an
    // explicit Text mode must survive restore after SaveWindowPosition.
    window->SetTextMode(DashboardTextMode::Text);
    if (DashboardStateTextModePreferred(window->m_dashboardState) != DashboardTextMode::Text ||
        DashboardStateTextModeEffective(window->m_dashboardState) != DashboardTextMode::Text) {
        return fail(L"SetTextMode must update both preferred and effective modes.");
    }
    {
        wchar_t modeBuf[32] = {};
        GetPrivateProfileStringW(
            L"Dashboard",
            L"TextMode",
            L"",
            modeBuf,
            32,
            testPositionPath.c_str());
        if (_wcsicmp(modeBuf, L"text") != 0) {
            return fail(L"SetTextMode did not persist Result Inspector Text mode.");
        }
    }
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardTextMode::Source, DashboardStateTextModeEffective(window->m_dashboardState));
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardStateTextModePreferred(window->m_dashboardState), DashboardTextMode::Source);
    window->RestoreWindowPosition();
    if (DashboardStateTextModePreferred(window->m_dashboardState) != DashboardTextMode::Text) {
        return fail(L"RestoreWindowPosition did not restore preferred Result Inspector Text mode.");
    }
    // Critical regression: host fallback + SaveWindowPosition must not rewrite
    // preferred Preview into Source in ocr_dashboard_pos.ini.
    window->SetTextMode(DashboardTextMode::Preview);
    window->FallbackPreviewToSource(L"fallback must not persist Source");
    if (DashboardStateTextModeEffective(window->m_dashboardState) != DashboardTextMode::Source ||
        DashboardStateTextModePreferred(window->m_dashboardState) != DashboardTextMode::Preview) {
        return fail(L"Fallback must keep preferred Preview while effective is Source.");
    }
    window->SaveWindowPosition();
    {
        wchar_t modeBuf[32] = {};
        GetPrivateProfileStringW(
            L"Dashboard",
            L"TextMode",
            L"",
            modeBuf,
            32,
            testPositionPath.c_str());
        if (_wcsicmp(modeBuf, L"preview") != 0) {
            return fail(L"SaveWindowPosition after FallbackPreviewToSource must still write preferred Preview.");
        }
    }
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardTextMode::Source, DashboardStateTextModeEffective(window->m_dashboardState));
    DashboardStateSyncTextMode(window->m_dashboardState, DashboardStateTextModePreferred(window->m_dashboardState), DashboardTextMode::Json);
    window->RestoreWindowPosition();
    if (DashboardStateTextModePreferred(window->m_dashboardState) != DashboardTextMode::Preview) {
        return fail(L"Restore after fallback+Save must reload preferred Preview, not effective Source.");
    }
    // Restore default for subsequent contract steps.
    window->SetTextMode(DashboardTextMode::Source);

    OcrDashboardHistoryItem imageHistory;
    imageHistory.timestamp = L"2026-07-02 00:00:00";
    imageHistory.imagePath = pdfJob.pages.front().sourceImagePath;
    imageHistory.text = L"Plain image history text";
    imageHistory.elapsedMs = 111;
    OcrLayoutBlock historyBlock;
    historyBlock.id = L"history:block:title";
    historyBlock.pageIndex = 0;
    historyBlock.order = 1;
    historyBlock.label = L"doc_title";
    historyBlock.content = L"Plain image history title";
    historyBlock.bbox = RECT{ 18, 22, 220, 64 };
    historyBlock.polygon = {
        { 18.0f, 22.0f },
        { 220.0f, 22.0f },
        { 220.0f, 64.0f },
        { 18.0f, 64.0f }
    };
    imageHistory.blocks = { historyBlock };
    window->AddHistoryItem(imageHistory);
    if (DashboardStateHasPdfSelection(window->m_dashboardState) || DashboardStateSelectedHistoryIndex(window->m_dashboardState) < 0) {
        return fail(L"Adding/selecting image history did not clear PDF selection.");
    }
    if (_wcsicmp(window->GetCurrentRevealPath().c_str(), imageHistory.imagePath.c_str()) != 0) {
        return fail(L"Reveal path should target the selected history image file.");
    }

    OcrDashboardHistoryItem previewEditHistory;
    previewEditHistory.timestamp = L"2026-07-02 00:00:00.500";
    previewEditHistory.imagePath = pdfJob.pages.front().sourceImagePath;
    previewEditHistory.text =
        L"# Repeat title\r\n\r\n"
        L"# Repeat title\r\n\r\n"
        L"![scan](assets/scan.png)";
    previewEditHistory.elapsedMs = 112;
    for (int i = 0; i < 3; ++i) {
        OcrLayoutBlock block;
        block.id = i == 0
            ? L"preview-edit:title:1"
            : (i == 1 ? L"preview-edit:title:2" : L"preview-edit:image");
        block.pageIndex = 0;
        block.order = i + 1;
        block.label = i < 2 ? L"doc_title" : L"image";
        block.content = i < 2 ? L"Repeat title" : L"";
        block.bbox = RECT{ 20, 20 + i * 60, 280, 64 + i * 60 };
        block.polygon = {
            { 20.0f, (float)(20 + i * 60) },
            { 280.0f, (float)(20 + i * 60) },
            { 280.0f, (float)(64 + i * 60) },
            { 20.0f, (float)(64 + i * 60) }
        };
        previewEditHistory.blocks.push_back(std::move(block));
    }
    window->AddHistoryItem(previewEditHistory);
    {
        const int historyIndex = DashboardStateSelectedHistoryIndex(window->m_dashboardState);
        const OcrDashboardHistoryItem beforeSuspendedSave =
            window->m_history.model.items[(size_t)historyIndex];
        DashboardStateApplyPersistenceFlags(window->m_dashboardState, true, DashboardStateIsDismissedManifestPersistenceSuspended(window->m_dashboardState));
        bool suspendedSaveAccepted = window->ApplyPreviewBlockEdit(
            L"preview-edit:title:2",
            L"## must not persist while suspended",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"# Repeat title", 1));
        DashboardStateApplyPersistenceFlags(window->m_dashboardState, false, DashboardStateIsDismissedManifestPersistenceSuspended(window->m_dashboardState));
        const auto& afterSuspendedSave = window->m_history.model.items[(size_t)historyIndex];
        const DashboardOcrBlock* suspendedBlock =
            window->FindCurrentBlockById(L"preview-edit:title:2");
        if (suspendedSaveAccepted ||
            afterSuspendedSave.text != beforeSuspendedSave.text ||
            afterSuspendedSave.blocks.size() != beforeSuspendedSave.blocks.size() ||
            !suspendedBlock || suspendedBlock->content != L"Repeat title") {
            return fail(L"Suspended History persistence reported success or leaked partial blocks/text state.");
        }
    }
    if (!window->ApplyPreviewBlockEdit(
            L"preview-edit:title:2",
            L"### Repeat title",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"# Repeat title", 1))) {
        return fail(L"Preview WYSIWYG heading edit did not persist.");
    }
    const auto& headingEditedHistory = window->m_history.model.items[(size_t)DashboardStateSelectedHistoryIndex(window->m_dashboardState)];
    std::wstring headingEditedLf = DashboardSourceMap::NormalizeLf(headingEditedHistory.text);
    const DashboardOcrBlock* firstEditedHeading =
        window->FindCurrentBlockById(L"preview-edit:title:2");
    if (!DashboardWindowTestContains(
            headingEditedLf,
            L"# Repeat title\n\n### Repeat title") ||
        headingEditedLf.find(L"### Repeat title") !=
            headingEditedLf.rfind(L"### Repeat title") ||
        !firstEditedHeading || !firstEditedHeading->edited ||
        !firstEditedHeading->editBaseline.has_value() ||
        firstEditedHeading->editBaseline->content != L"Repeat title" ||
        firstEditedHeading->editBaseline->sourceSegment != L"# Repeat title") {
        return fail(L"Preview WYSIWYG duplicate heading replacement did not target the selected occurrence.");
    }
    if (!window->ApplyPreviewBlockEdit(
            L"preview-edit:title:2",
            L"#### Changed twice",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"### Repeat title"))) {
        return fail(L"Second Preview edit did not persist.");
    }
    const DashboardOcrBlock* secondEditedHeading =
        window->FindCurrentBlockById(L"preview-edit:title:2");
    if (!secondEditedHeading || !secondEditedHeading->editBaseline.has_value() ||
        secondEditedHeading->editBaseline->content != L"Repeat title" ||
        secondEditedHeading->editBaseline->sourceSegment != L"# Repeat title") {
        return fail(L"A later Preview edit overwrote the immutable OCR baseline.");
    }
    if (!window->RestorePreviewBlockOriginal(
            L"preview-edit:title:2",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"#### Changed twice"))) {
        return fail(L"Restore OCR did not persist the original heading.");
    }
    const DashboardOcrBlock* restoredHeading =
        window->FindCurrentBlockById(L"preview-edit:title:2");
    std::wstring restoredHeadingLf = DashboardSourceMap::NormalizeLf(
        window->m_history.model.items[(size_t)DashboardStateSelectedHistoryIndex(window->m_dashboardState)].text);
    if (!restoredHeading || restoredHeading->edited || restoredHeading->editBaseline.has_value() ||
        restoredHeading->content != L"Repeat title" ||
        restoredHeadingLf != L"# Repeat title\n\n# Repeat title\n\n![scan](assets/scan.png)") {
        return fail(L"Restore OCR did not clear edit state and recover the exact original source segment.");
    }
    if (!window->ApplyPreviewBlockEdit(
            L"preview-edit:image",
            L"![Updated diagram](assets/scan.png)\n\nEdited caption",
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(), L"![scan](assets/scan.png)"))) {
        return fail(L"Preview WYSIWYG empty image block edit did not persist.");
    }
    const auto& imageEditedHistory = window->m_history.model.items[(size_t)DashboardStateSelectedHistoryIndex(window->m_dashboardState)];
    const DashboardOcrBlock* imageEditedBlock = window->FindCurrentBlockById(L"preview-edit:image");
    if (!DashboardWindowTestContains(imageEditedHistory.text, L"![Updated diagram](assets/scan.png)") ||
        !DashboardWindowTestContains(imageEditedHistory.text, L"Edited caption") ||
        !imageEditedBlock ||
        !imageEditedBlock->edited ||
        !DashboardWindowTestContains(imageEditedBlock->content, L"Updated diagram") ||
        DashboardStateSelectedBlockId(window->m_dashboardState) != L"preview-edit:image") {
        return fail(L"Preview WYSIWYG image edit did not preserve Markdown, block content, and selection.");
    }

    OcrDashboardHistoryItem perfHistory;
    perfHistory.timestamp = L"2026-07-02 00:00:01";
    perfHistory.imagePath = pdfJob.pages.front().sourceImagePath;
    perfHistory.text = L"Large layout performance contract";
    perfHistory.elapsedMs = 222;
    perfHistory.blocks.reserve(300);
    for (int i = 0; i < 300; ++i) {
        int col = i % 20;
        int row = i / 20;
        int left = 16 + col * 68;
        int top = 20 + row * 42;
        int right = left + 48;
        int bottom = top + 28;
        OcrLayoutBlock block;
        block.id = L"perf:block:" + std::to_wstring(i + 1);
        block.pageIndex = 0;
        block.order = i + 1;
        if (i % 17 == 0) block.label = L"table";
        else if (i % 19 == 0) block.label = L"image";
        else if (i % 23 == 0) block.label = L"display_formula";
        else block.label = L"text";
        block.content = L"performance block " + std::to_wstring(i + 1);
        if (i == 256) block.content += L" needle_257";
        block.bbox = RECT{ left, top, right, bottom };
        block.polygon = {
            { (float)left, (float)top },
            { (float)right, (float)top },
            { (float)right, (float)bottom },
            { (float)left, (float)bottom }
        };
        block.confidence = 0.8;
        block.source = L"perf_contract";
        perfHistory.blocks.push_back(std::move(block));
    }
    window->AddHistoryItem(perfHistory);
    ULONGLONG perfStart = GetTickCount64();
    window->m_dashboardState.canvasView.showLayoutOverlay = true;
    window->m_dashboardState.canvasView.zoom = 1.0f;
    window->m_dashboardState.canvasView.panX = 0.0f;
    window->m_dashboardState.canvasView.panY = 0.0f;
    window->RefreshCurrentBlocks();
    if (window->m_canvas.currentBlocks.size() != 300) {
        return fail(L"Large layout contract did not load all 300 blocks.");
    }
    int hitX = 16 + 2 * 68 + 4;
    int hitY = 20 + 2 * 42 + 4;
    int visibleHit = window->HitTestImageBlock(hitX, hitY);
    if (visibleHit < 0) {
        return fail(L"Large layout hit-test did not resolve a visible block.");
    }
    ULONGLONG perfElapsed = GetTickCount64() - perfStart;
    if (perfElapsed > 3000) {
        return fail(L"Large layout load/hit-test contract was too slow: " + std::to_wstring(perfElapsed) + L"ms.");
    }
    std::vector<double> paintMilliseconds;
    paintMilliseconds.reserve(100);
    for (int sample = 0; sample < 110; ++sample) {
        LARGE_INTEGER frequency = {};
        LARGE_INTEGER startCounter = {};
        LARGE_INTEGER endCounter = {};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&startCounter);
        SendMessageW(window->m_imageArea, WM_PAINT, 0, 0);
        QueryPerformanceCounter(&endCounter);
        if (sample >= 10 && frequency.QuadPart > 0) {
            paintMilliseconds.push_back(
                (endCounter.QuadPart - startCounter.QuadPart) * 1000.0 /
                static_cast<double>(frequency.QuadPart));
        }
    }
    std::sort(paintMilliseconds.begin(), paintMilliseconds.end());
    if (paintMilliseconds.size() != 100) {
        return fail(L"Dashboard paint instrumentation did not collect 100 warm samples.");
    }
    double paintMedian = paintMilliseconds[49];
    double paintP95 = paintMilliseconds[94];
    double paintMax = paintMilliseconds.back();
    wprintf(L"Dashboard 300-block HWND paint: median=%.3fms p95=%.3fms max=%.3fms\n",
        paintMedian, paintP95, paintMax);
    if (paintP95 > 100.0) {
        return fail(L"Dashboard 300-block HWND paint gross p95 regression: " +
            std::to_wstring(paintP95) + L"ms.");
    }
    {
        std::vector<DashboardOcrBlock> originalRuntimeBlocks = window->m_canvas.currentBlocks;
        DashboardOcrBlock first = originalRuntimeBlocks.front();
        DashboardOcrBlock duplicate = first;
        DashboardOcrBlock missingId = first;
        first.content = L"first duplicate-id block";
        duplicate.content = L"later duplicate-id block";
        missingId.id.clear();
        window->m_canvas.currentBlocks = {first, duplicate, missingId};
        window->RebuildBlockRuntimeIndex();
        const DashboardOcrBlock* resolved = window->FindCurrentBlockById(first.id);
        if (window->m_canvas.currentBlocks.size() != 1 || !resolved ||
            resolved->content != L"first duplicate-id block" ||
            window->m_canvas.blockRuntimeIndex.HasDuplicateIds()) {
            return fail(L"Dashboard runtime snapshot did not filter empty/later duplicate block IDs.");
        }
        window->m_canvas.currentBlocks = std::move(originalRuntimeBlocks);
        window->RebuildBlockRuntimeIndex();
    }

    window->ActivateSourceRailPdfItem(0, selectedPageIndex, false);
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        { pdfJob.outputRoot });
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        pdfJob.outputRoot,
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    const size_t clearFailureHistoryCount = window->m_history.model.items.size();
    const size_t clearFailureTaskCount = window->m_batch.batchTasks.size();
    const size_t clearFailurePdfCount = window->m_batch.activePdfJobs.size();
    DashboardStateApplyPersistenceFlags(window->m_dashboardState, true, DashboardStateIsDismissedManifestPersistenceSuspended(window->m_dashboardState));
    window->ClearAllHistoryRecords();
    DashboardStateApplyPersistenceFlags(window->m_dashboardState, false, DashboardStateIsDismissedManifestPersistenceSuspended(window->m_dashboardState));
    if (window->m_history.model.items.size() != clearFailureHistoryCount ||
        window->m_batch.batchTasks.size() != clearFailureTaskCount ||
        window->m_batch.activePdfJobs.size() != clearFailurePdfCount ||
        !DashboardStateHasPdfSelection(window->m_dashboardState)) {
        return fail(L"Clear Finished did not roll back backing/selection state after History persistence failure.");
    }
    window->ClearAllHistoryRecords();
    if (!window->m_history.model.items.empty() || !window->m_batch.activePdfJobs.empty()) {
        return fail(L"Clear Finished did not remove terminal image/PDF Sources and linked backing records.");
    }
    if (DashboardStateHasPdfSelection(window->m_dashboardState) || DashboardStateHasImageTaskSelection(window->m_dashboardState) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) >= 0) {
        return fail(L"Clear Finished retained a selection owned by a removed Source.");
    }
    if (window->m_gdiplusImage) {
        return fail(L"Clear Finished retained a Canvas image owned by a removed Source.");
    }
    std::wstring clearHistoryPdfResult = window->GetCurrentResultText();
    if (DashboardWindowTestContains(clearHistoryPdfResult, L"Window contract markdown page") ||
        DashboardWindowTestContains(clearHistoryPdfResult, L"stub")) {
        return fail(L"Clear Finished retained stale Result Inspector content.");
    }
    if (!PathFileExistsW(resolvedPdfPath.c_str()) ||
        GetFileAttributesW(pdfJob.outputDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return fail(L"Clear Finished removed an imported PDF or durable output directory.");
    }
    const std::vector<std::wstring> rootsAfterClear = window->GetAutoResumeOutputRoots();
    const bool clearedRootStillRemembered = std::any_of(
        rootsAfterClear.begin(),
        rootsAfterClear.end(),
        [&](const std::wstring& root) {
            return NormalizePathForCompare(root) == NormalizePathForCompare(pdfJob.outputRoot);
        });
    if (clearedRootStillRemembered) {
        return fail(L"Clear Finished retained a terminal output root for automatic resume.");
    }
    window->AutoResumeLastBatchOutputRoot();
    if (std::any_of(window->m_batch.activePdfJobs.begin(), window->m_batch.activePdfJobs.end(),
            [&](const BatchOcrPdfJob& candidate) {
                return DashboardSamePdfJobIdentity(candidate, pdfJob);
            })) {
        return fail(L"Clear Finished Source reappeared through automatic resume.");
    }

    resetWindowState();
    DashboardBatchTaskItem retainedPendingTask;
    retainedPendingTask.job.sourceInstanceId = L"{33333333-3333-4333-8333-333333333333}";
    retainedPendingTask.job.sourcePath = sourcePageImage;
    retainedPendingTask.status = BatchOcrTaskStatus::Pending;
    OcrDashboardHistoryItem retainedPendingHistory;
    retainedPendingHistory.sourceInstanceId = retainedPendingTask.job.sourceInstanceId;
    retainedPendingHistory.originKind = L"ImportedImage";
    retainedPendingHistory.imagePath = sourcePageImage;
    retainedPendingHistory.text = L"retained pending result";
    BatchOcrPdfJob retainedPausedPdf = pdfJob;
    retainedPausedPdf.status = BatchOcrTaskStatus::Pending;
    for (auto& page : retainedPausedPdf.pages) page.status = BatchOcrTaskStatus::Pending;
    window->m_batch.batchTasks.push_back(retainedPendingTask);
    window->m_history.model.items.push_back(retainedPendingHistory);
    window->m_batch.activePdfJobs.push_back(retainedPausedPdf);
    window->m_dashboardState.pausedPdfJobKeys.push_back(DashboardPdfJobTreeKey(retainedPausedPdf));
    window->ApplyFilter(L"");
    window->ClearAllHistoryRecords();
    if (window->m_batch.batchTasks.size() != 1 || window->m_history.model.items.size() != 1 ||
        window->m_batch.activePdfJobs.size() != 1 || window->m_dashboardState.pausedPdfJobKeys.empty()) {
        return fail(L"Clear Finished removed a pending/paused recoverable Source or its linked History.");
    }

    BatchOcrWriteResult resumeFailureWrite = BatchOcrWriter::WritePdfPageFailure(
        pdfJob,
        selectedPageIndex,
        L"window_contract",
        L"resume contract injected failure",
        0);
    if (!resumeFailureWrite.success) {
        return fail(resumeFailureWrite.error.empty()
            ? L"Failed to prepare PDF resume retry fixture."
            : resumeFailureWrite.error);
    }

    resetWindowState();
    if (!window->LoadBatchOutputSnapshot(runRoot, false, false)) {
        return fail(L"Dashboard PDF Resume snapshot did not load failed page output.");
    }
    bool selectedPageRetryableAfterResume = std::any_of(
        window->m_batch.failedPdfPages.begin(),
        window->m_batch.failedPdfPages.end(),
        [&](const DashboardPdfRetryPage& retry) {
            return retry.page.pageIndex == selectedPageIndex &&
                retry.page.status == BatchOcrTaskStatus::Failed;
        });
    if (window->m_batch.activePdfJobs.empty() ||
        window->m_batch.activePdfJobs.front().pages.empty() ||
        !selectedPageRetryableAfterResume ||
        window->m_batch.activePdfJobs.front().pages.front().status != BatchOcrTaskStatus::Failed) {
        return fail(L"Dashboard PDF Resume did not expose the failed page as retryable.");
    }
    window->RetryFailedBatchJobs();
    bool resumeRetryFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        if (DashboardStatePdfRenderInFlight(window->m_dashboardState) != 0 ||
            DashboardStateIsOcrBusy(window->m_dashboardState) ||
            !window->m_batch.dropQueue.empty() ||
            !window->m_batch.failedPdfPages.empty()) {
            return false;
        }
        if (window->m_batch.activePdfJobs.empty() ||
            window->m_batch.activePdfJobs.front().pages.empty()) {
            return false;
        }
        return window->m_batch.activePdfJobs.front().pages.front().status == BatchOcrTaskStatus::Completed;
    });
    if (!resumeRetryFinished) {
        return fail(L"Dashboard PDF Resume retry did not complete.");
    }
    const BatchOcrPdfJob& resumedPdfJob = window->m_batch.activePdfJobs.front();
    const BatchOcrPdfPageJob& resumedPage = resumedPdfJob.pages.front();
    std::wstring resumedPageMarkdown;
    if (!DashboardWindowTestReadUtf8File(resumedPage.markdownPath, resumedPageMarkdown) ||
        !DashboardWindowTestContains(resumedPageMarkdown, L"stub")) {
        return fail(L"Dashboard PDF Resume retry did not rewrite page OCR output.");
    }
    window->ActivateSourceRailPdfItem(0, selectedPageIndex, false);
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != selectedVisiblePageIndex ||
        !window->m_gdiplusImage ||
        !DashboardWindowTestContains(window->GetCurrentResultText(), L"stub")) {
        return fail(L"Dashboard PDF Resume retry did not restore Source Rail, Canvas and Result selection.");
    }

    resetWindowState();
    SetWindowPos(hwnd, nullptr, 0, 0, 1200, 800,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    // The automation desktop may clamp this request to 640x480. Make Source
    // the explicit responsive side-pane preference so mouse/keyboard routing
    // is tested against a visible rail instead of depending on state left by a
    // preceding Result Inspector scenario.
    window->m_layout.sourceVisible = true;
    window->m_layout.resultVisible = false;
    window->m_responsiveLayout = {};
    window->m_responsiveLayout.preferredPane = DashboardSidePane::Source;
    window->LayoutControls();

    auto makeMouseHistoryItem = [&](int index, const std::wstring& text) {
        OcrDashboardHistoryItem item;
        item.timestamp = L"2026-07-03 02:0" + std::to_wstring(index) + L":00";
        item.imagePath = sourcePageImage;
        item.text = text;
        item.elapsedMs = 10 + index;
        return item;
    };
    window->AddHistoryItem(makeMouseHistoryItem(0, L"stable reorder A"));
    window->AddHistoryItem(makeMouseHistoryItem(1, L"stable reorder B"));
    window->SelectHistoryItem(1);
    // D-D-3: selectedSourceKey sole on DashboardState (was m_sourceSelection.active).
    DashboardItemKey stableHistorySelection =
        DashboardStateSelectedSourceKey(window->m_dashboardState);
    std::swap(window->m_history.model.items[0], window->m_history.model.items[1]);
    window->ApplyFilter(L"");
    if (DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 0 ||
        window->m_history.model.items.front().text != L"stable reorder B" ||
        !(DashboardStateSelectedSourceKey(window->m_dashboardState) == stableHistorySelection)) {
        return fail(L"History-only Source selection did not follow its stable key after backing reorder.");
    }

    resetWindowState();
    window->AddHistoryItem(makeMouseHistoryItem(0, L"mouse row 0"));
    window->AddHistoryItem(makeMouseHistoryItem(1, L"mouse row 1"));
    window->AddHistoryItem(makeMouseHistoryItem(2, L"mouse row 2"));
    window->AddHistoryItem(makeMouseHistoryItem(3, L"mouse row 3"));
    window->LayoutControls();
    window->ScrollSourceRailTo(0);

    auto selectedSourcesEqual = [&](const std::vector<int>& expected) {
        return DashboardHistorySelectedIndices(
            window->m_history.model.items,
            DashboardStateSelectedSourceKeys(window->m_dashboardState),
            DashboardStateSelectedHistoryIndex(window->m_dashboardState)) == expected;
    };
    auto sourceRailHistoryRowTop = [&](int historyIndex) {
        int top = 0;
        for (const auto& row : window->BuildSourceRailViewRows()) {
            if (row.selection.kind == DashboardSourceRailRowKind::History &&
                row.selection.historyIndex == historyIndex) {
                return top;
            }
            top += window->GetSourceRailViewRowHeight(row);
        }
        return -1;
    };
    auto clickHistoryRow = [&](int historyIndex, WPARAM keyState) {
        int top = sourceRailHistoryRowTop(historyIndex);
        if (top < 0 || !window->m_sourceList) return false;
        int y = top + window->m_metrics.sourceListItemH / 2 - window->m_sourceScrollY;
        RECT sourceRc = {};
        GetClientRect(window->m_sourceList, &sourceRc);
        if (y < 0 || y >= sourceRc.bottom - sourceRc.top) return false;
        SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, keyState, MAKELPARAM(12, y));
        return true;
    };

    if (!clickHistoryRow(0, 0) ||
        !selectedSourcesEqual(std::vector<int>{0}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 0 ||
        DashboardStateHasPdfSelection(window->m_dashboardState)) {
        return fail(L"Source Rail mouse click did not select the expected history row.");
    }
    if (!window->m_gdiplusImage) {
        return fail(L"History Canvas-reuse fixture did not load its source image.");
    }
    window->m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
    window->m_dashboardState.canvasView.zoom = 2.125f;
    window->m_dashboardState.canvasView.panX = 17.0f;
    window->m_dashboardState.canvasView.panY = -11.0f;
    if (!clickHistoryRow(0, 0) ||
        window->m_dashboardState.canvasView.viewMode != ImageViewMode::Manual ||
        window->m_dashboardState.canvasView.zoom != 2.125f ||
        window->m_dashboardState.canvasView.panX != 17.0f ||
        window->m_dashboardState.canvasView.panY != -11.0f) {
        return fail(L"Repeated History activation reloaded or refit the unchanged Canvas.");
    }
    if (!clickHistoryRow(2, MK_CONTROL) ||
        !selectedSourcesEqual(std::vector<int>{0, 2}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 2 ||
        window->m_dashboardState.canvasView.viewMode != ImageViewMode::Fit) {
        return fail(L"Source Rail Ctrl-click did not toggle and activate a different History Source.");
    }
    if (!clickHistoryRow(3, MK_SHIFT) ||
        !selectedSourcesEqual(std::vector<int>{2, 3}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 3) {
        return fail(L"Source Rail Shift-click did not select the expected history range.");
    }
    for (int i = 4; i < 60; i++) {
        window->AddHistoryItem(makeMouseHistoryItem(i, L"mouse scroll row " + std::to_wstring(i)));
    }
    window->LayoutControls();
    int scrollTargetTop = sourceRailHistoryRowTop(52);
    window->ScrollSourceRailTo(scrollTargetTop - window->m_metrics.sourceListItemH);
    if (window->m_sourceScrollY <= 0 ||
        !clickHistoryRow(52, 0) ||
        !selectedSourcesEqual(std::vector<int>{52}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 52) {
        return fail(L"Source Rail long-list mouse click after scrolling did not resolve the expected history row.");
    }
    window->ScrollSourceRailTo(0);
    SendMessageW(
        window->m_sourceList,
        WM_MOUSEWHEEL,
        MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
        0);
    int wheelScrollY = window->m_sourceScrollY;
    if (wheelScrollY <= 0) {
        return fail(L"Source Rail mouse wheel did not scroll the long list.");
    }
    SendMessageW(window->m_sourceList, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
    if (window->m_sourceScrollY <= wheelScrollY) {
        return fail(L"Source Rail vertical scrollbar page-down did not advance the long list.");
    }
    int thumbTrackTarget = max(1, window->m_metrics.sourceListItemH * 8);
    SendMessageW(window->m_sourceList, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, thumbTrackTarget), 0);
    if (window->m_sourceScrollY != thumbTrackTarget) {
        return fail(L"Source Rail vertical scrollbar thumb-track did not update the scroll position.");
    }
    int thumbPositionTarget = max(1, window->m_metrics.sourceListItemH * 12);
    SendMessageW(window->m_sourceList, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, thumbPositionTarget), 0);
    if (window->m_sourceScrollY != thumbPositionTarget) {
        return fail(L"Source Rail vertical scrollbar thumb-position did not settle the scroll position.");
    }
    RECT sourceClientRc = {};
    GetClientRect(window->m_sourceList, &sourceClientRc);
    int sourcePageH = max(1, sourceClientRc.bottom - sourceClientRc.top);
    int expectedMaxScroll = max(0, window->GetSourceRailViewContentHeight() - sourcePageH);
    SendMessageW(window->m_sourceList, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
    if (window->m_sourceScrollY != expectedMaxScroll ||
        !clickHistoryRow(59, 0) ||
        !selectedSourcesEqual(std::vector<int>{59}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 59) {
        return fail(L"Source Rail scrollbar bottom did not clamp and preserve hit-testing on the long list.");
    }
    int bottomScrollBeforeResize = window->m_sourceScrollY;
    int sourcePageHBeforeResize = sourcePageH;
    SetWindowPos(hwnd, nullptr, 0, 0, 1200, 1200,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->LayoutControls();
    window->UpdateSourceRailScrollInfo();
    GetClientRect(window->m_sourceList, &sourceClientRc);
    sourcePageH = max(1, sourceClientRc.bottom - sourceClientRc.top);
    if (sourcePageH <= sourcePageHBeforeResize) {
        int forcedGrowH = sourcePageHBeforeResize + max(window->m_metrics.sourceListItemH * 4, 160);
        SetWindowPos(
            window->m_sourceList,
            nullptr,
            0,
            0,
            max(1, sourceClientRc.right - sourceClientRc.left),
            forcedGrowH,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        window->UpdateSourceRailScrollInfo();
        GetClientRect(window->m_sourceList, &sourceClientRc);
        sourcePageH = max(1, sourceClientRc.bottom - sourceClientRc.top);
    }
    int expectedMaxScrollAfterResize = max(0, window->GetSourceRailViewContentHeight() - sourcePageH);
    if (expectedMaxScrollAfterResize >= bottomScrollBeforeResize ||
        window->m_sourceScrollY != expectedMaxScrollAfterResize) {
        std::wstring details = L"Source Rail resize did not clamp long-list scroll position after viewport growth. before=" +
            std::to_wstring(bottomScrollBeforeResize) +
            L" expected=" + std::to_wstring(expectedMaxScrollAfterResize) +
            L" actual=" + std::to_wstring(window->m_sourceScrollY) +
            L" pageH=" + std::to_wstring(sourcePageH) +
            L" rows=" + std::to_wstring(DashboardStateVisibleHistoryIndices(window->m_dashboardState).size()) +
            L" batchH=" + std::to_wstring(window->GetSourceRailBatchSectionHeight());
        return fail(details);
    }
    HDC grownRailDc = GetDC(window->m_sourceList);
    bool grownBackbufferOk = window->EnsureSourceRailBackbuffer(
        grownRailDc,
        max(1, sourceClientRc.right - sourceClientRc.left),
        max(1, sourceClientRc.bottom - sourceClientRc.top));
    if (grownRailDc) ReleaseDC(window->m_sourceList, grownRailDc);
    if (!window->m_sourceRailBufferBitmap ||
        !grownBackbufferOk ||
        window->m_sourceRailBufferW != max(1, sourceClientRc.right - sourceClientRc.left) ||
        window->m_sourceRailBufferH != max(1, sourceClientRc.bottom - sourceClientRc.top)) {
        return fail(L"Source Rail persistent backbuffer did not match the grown viewport.");
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 1200, 800,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->LayoutControls();
    GetClientRect(window->m_sourceList, &sourceClientRc);
    HDC restoredRailDc = GetDC(window->m_sourceList);
    bool restoredBackbufferOk = window->EnsureSourceRailBackbuffer(
        restoredRailDc,
        max(1, sourceClientRc.right - sourceClientRc.left),
        max(1, sourceClientRc.bottom - sourceClientRc.top));
    if (restoredRailDc) ReleaseDC(window->m_sourceList, restoredRailDc);
    if (!window->m_sourceRailBufferBitmap ||
        !restoredBackbufferOk ||
        window->m_sourceRailBufferW != max(1, sourceClientRc.right - sourceClientRc.left) ||
        window->m_sourceRailBufferH != max(1, sourceClientRc.bottom - sourceClientRc.top)) {
        return fail(L"Source Rail persistent backbuffer did not resize back with the viewport.");
    }

    resetWindowState();
    wchar_t warmupSuffix[128] = {};
    swprintf_s(warmupSuffix, L"%lu_%llu",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()));
    std::wstring warmupImagePath = runRoot + L"\\source_rail_thumb_warmup_" + warmupSuffix + L".png";
    if (!CopyFileW(sourcePageImage.c_str(), warmupImagePath.c_str(), FALSE)) {
        return fail(L"Failed to prepare Source Rail thumbnail warmup fixture.");
    }
    OcrDashboardHistoryItem warmupItem;
    warmupItem.timestamp = L"2026-07-04 01:00:00";
    warmupItem.imagePath = warmupImagePath;
    warmupItem.text = L"thumbnail warmup item";
    window->AddHistoryItem(warmupItem);
    window->LayoutControls();
    window->ScrollSourceRailTo(0);
    GetClientRect(window->m_sourceList, &sourceClientRc);
    int warmupTop = sourceRailHistoryRowTop(0);
    if (warmupTop < 0) return fail(L"Source Rail thumbnail warmup fixture row was not visible.");
    RECT warmupItemRc = {
        0,
        warmupTop - window->m_sourceScrollY,
        max(1, sourceClientRc.right - sourceClientRc.left),
        warmupTop - window->m_sourceScrollY + window->m_metrics.sourceListItemH
    };
    RECT warmupThumbRc = window->GetSourceRailThumbnailRect(warmupItemRc);
    int warmupThumbW = max(1, warmupThumbRc.right - warmupThumbRc.left);
    int warmupThumbH = max(1, warmupThumbRc.bottom - warmupThumbRc.top);
    if (GetCachedSourceRailThumbnail(warmupImagePath, warmupThumbW, warmupThumbH, false)) {
        return fail(L"Source Rail thumbnail warmup fixture should not already be cached.");
    }
    window->ScheduleSourceRailThumbnailWarmup();
    if (!window->m_sourceRailThumbnailWarmupPending) {
        return fail(L"Source Rail thumbnail warmup was not scheduled after a paint miss.");
    }
    KillTimer(hwnd, TIMER_SOURCE_THUMBNAIL_WARMUP);
    window->m_sourceRailThumbnailWarmupPending = false;
    window->WarmVisibleSourceRailThumbnails(1);
    bool warmupReady = DashboardWindowTestPumpUntil(hwnd, 3000, [&]() {
        return !!GetCachedSourceRailThumbnail(warmupImagePath, warmupThumbW, warmupThumbH, false);
    });
    if (!warmupReady) {
        return fail(L"Source Rail thumbnail warmup did not populate the visible thumbnail cache.");
    }
    ClearSourceRailThumbnailCacheForTests();
    std::shared_ptr<Gdiplus::Bitmap> retainedEvictedBitmap =
        GetCachedSourceRailThumbnail(warmupImagePath, 8, 8, true);
    if (!retainedEvictedBitmap) {
        return fail(L"Source Rail thumbnail LRU fixture could not decode its retained bitmap.");
    }
    for (int size = 16; size < 16 + 193; ++size) {
        if (!GetCachedSourceRailThumbnail(warmupImagePath, size, size, true)) {
            return fail(L"Source Rail thumbnail LRU fixture failed to populate a cache entry.");
        }
    }
    if (GetSourceRailThumbnailCacheEntryCountForTests() != 192 ||
        GetCachedSourceRailThumbnail(warmupImagePath, 8, 8, false) ||
        retainedEvictedBitmap->GetWidth() != 8 || retainedEvictedBitmap->GetHeight() != 8) {
        return fail(L"Source Rail thumbnail cache did not enforce 192-entry LRU/shared draw lifetime.");
    }
    ClearSourceRailThumbnailCacheForTests();

    if (pdfJob.pages.empty() || pdfJob.pages.front().pageIndex != 1 ||
        pdfJob.pages.front().sourceImagePath.empty() ||
        pdfJob.pages.front().blocks.empty()) {
        return fail(L"PDF root/Page 1 block fixture is incomplete.");
    }

    resetWindowState();
    BatchOcrPdfJob singlePageRoot = pdfJob;
    singlePageRoot.sourcePageCount = 1;
    singlePageRoot.pageRange = L"1";
    singlePageRoot.pages.resize(1);
    window->m_batch.activePdfJobs.push_back(singlePageRoot);
    window->SetPdfJobExpanded(window->m_batch.activePdfJobs.front(), true);
    std::vector<SourceRailTaskRow> singlePageTaskRows = window->BuildSourceRailTaskRows();
    if (singlePageTaskRows.size() != 1 ||
        singlePageTaskRows.front().kind != SourceRailTaskRowKind::PdfJob ||
        window->IsPdfJobExpanded(window->m_batch.activePdfJobs.front())) {
        return fail(L"Single-page PDF exposed a duplicate Page 1 row or disclosure state.");
    }

    // Simulate a legacy/restored Page 1 selection. It must be promoted to the
    // only visible PDF root while the root still consumes the internal Page 1
    // image and block provider.
    window->ActivateSourceRailPdfItem(0, 1, false);
    std::vector<DashboardSourceRailSelectableRow> singlePageSelection =
        window->GetSelectedBatchRows();
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
        singlePageSelection.size() != 1 ||
        singlePageSelection.front().kind != DashboardSourceRailRowKind::PdfJob ||
        _wcsicmp(
            window->m_canvasImagePath.c_str(),
            singlePageRoot.pages.front().sourceImagePath.c_str()) != 0 ||
        window->m_canvas.currentBlocks.empty() ||
        window->m_canvas.currentBlocks.front().id != singlePageRoot.pages.front().blocks.front().id) {
        return fail(L"Single-page PDF root did not absorb Page 1 selection, Canvas image, and blocks.");
    }
    std::wstring singlePagePreviewSource = window->GetCurrentPreviewSourceMarkdown();
    if (singlePagePreviewSource.empty() ||
        DashboardWindowTestContains(singlePagePreviewSource, L"<!-- page:")) {
        return fail(L"Single-page PDF root Preview did not use its editable internal Page 1 body: " +
            singlePagePreviewSource);
    }
    if (!window->PersistPreviewMarkdownEdit(
            L"Single-page root Preview body edit",
            DashboardWindowTestSourceEdit(
                singlePagePreviewSource,
                singlePagePreviewSource)) ||
        !DashboardWindowTestContains(
            window->GetCurrentPreviewSourceMarkdown(),
            L"Single-page root Preview body edit") ||
        !window->PersistPreviewMarkdownEdit(
            singlePagePreviewSource,
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(),
                L"Single-page root Preview body edit"))) {
        return fail(L"Single-page PDF root Preview did not persist edits through internal Page 1 artifacts.");
    }
    window->HandleSourceRailKey(VK_RIGHT, false, false);
    if (window->BuildSourceRailTaskRows().size() != 1 ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
        window->IsPdfJobExpanded(window->m_batch.activePdfJobs.front())) {
        return fail(L"Right Arrow expanded or moved into a hidden single-page Page 1 row.");
    }

    resetWindowState();
    BatchOcrPdfJob multiPageRoot = pdfJob;
    multiPageRoot.sourcePageCount = max(2, pdfJob.sourcePageCount);
    window->m_batch.activePdfJobs.push_back(multiPageRoot);
    window->SetPdfJobExpanded(window->m_batch.activePdfJobs.front(), true);
    std::vector<SourceRailTaskRow> multiPageTaskRows = window->BuildSourceRailTaskRows();
    window->ActivateSourceRailPdfItem(0, 1, false);
    size_t multiPageVisibleChildren = std::count_if(
        multiPageRoot.pages.begin(), multiPageRoot.pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
    if (multiPageTaskRows.size() != multiPageVisibleChildren + 1 ||
        multiPageTaskRows.front().kind != SourceRailTaskRowKind::PdfJob ||
        (multiPageVisibleChildren > 0 &&
            (multiPageTaskRows[1].kind != SourceRailTaskRowKind::PdfPage ||
             multiPageTaskRows[1].pageIndex <= 1)) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
        _wcsicmp(
            window->m_canvasImagePath.c_str(),
            multiPageRoot.pages.front().sourceImagePath.c_str()) != 0 ||
        window->m_canvas.currentBlocks.empty() ||
        window->m_canvas.currentBlocks.front().id != multiPageRoot.pages.front().blocks.front().id) {
        return fail(L"Multi-page PDF root did not absorb the hidden Page 1 selection, Canvas, and blocks.");
    }
    std::wstring multiPageOnePreviewSource = window->GetCurrentPreviewSourceMarkdown();
    const BatchOcrPdfPageJob* activeMultiPageOne = DashboardFindPdfSelectionPage(
        window->m_batch.activePdfJobs.front(), 1);
    std::wstring expectedMultiPageOnePreview = activeMultiPageOne
        ? activeMultiPageOne->markdown
        : L"";
    if (activeMultiPageOne && expectedMultiPageOnePreview.empty()) {
        std::wstring activeMultiPageOneJson;
        if (DashboardWindowTestReadUtf8File(
                activeMultiPageOne->contentJsonPath,
                activeMultiPageOneJson)) {
            expectedMultiPageOnePreview = UnescapeJsonString(
                ExtractJsonField(activeMultiPageOneJson, L"markdown"));
        }
    }
    if (!activeMultiPageOne || multiPageOnePreviewSource.empty() ||
        DashboardSourceMap::NormalizeLf(multiPageOnePreviewSource) !=
            DashboardSourceMap::NormalizeLf(expectedMultiPageOnePreview)) {
        return fail(L"Multi-page PDF root did not use its internal Page 1 Preview body: " +
            window->GetCurrentPreviewSourceMarkdown());
    }
    if (!window->PersistPreviewMarkdownEdit(
            L"Multi-page root Page 1 edit",
            DashboardWindowTestSourceEdit(
                multiPageOnePreviewSource,
                multiPageOnePreviewSource)) ||
        !DashboardWindowTestContains(
            window->GetCurrentPreviewSourceMarkdown(),
            L"Multi-page root Page 1 edit") ||
        !window->PersistPreviewMarkdownEdit(
            multiPageOnePreviewSource,
            DashboardWindowTestSourceEdit(
                window->GetCurrentPreviewSourceMarkdown(),
                L"Multi-page root Page 1 edit"))) {
        return fail(L"Multi-page PDF root did not persist edits through hidden Page 1 artifacts.");
    }
    if (multiPageVisibleChildren > 0) {
        window->HandleSourceRailKey(VK_RIGHT, false, false);
        if (DashboardStatePdfSelectionPageIndex(window->m_dashboardState) <= 1) {
            return fail(L"Right Arrow did not enter the first visible PDF child after hidden Page 1.");
        }
    }

    resetWindowState();
    BatchOcrPdfJob corruptPageOneRoot = multiPageRoot;
    corruptPageOneRoot.thumbnailPath = sourcePageImage;
    corruptPageOneRoot.pages.front().sourceImagePath = invalidTransientPath;
    window->m_batch.activePdfJobs.push_back(corruptPageOneRoot);
    window->ActivateSourceRailPdfItem(0, 0, true);
    if (_wcsicmp(window->m_canvasImagePath.c_str(), sourcePageImage.c_str()) != 0 ||
        !window->m_canvas.currentBlocks.empty()) {
        return fail(L"Corrupt Page 1 image did not fall back to the cover with blocks suppressed.");
    }

    resetWindowState();
    BatchOcrPdfJob rangeExcludesPageOne = pdfJob;
    rangeExcludesPageOne.sourcePageCount = max(2, pdfJob.sourcePageCount);
    rangeExcludesPageOne.pageRange = L"5";
    rangeExcludesPageOne.thumbnailPath.clear();
    rangeExcludesPageOne.pages = { pdfJob.pages.back() };
    rangeExcludesPageOne.pages.front().pageIndex = 5;
    window->m_batch.activePdfJobs.push_back(rangeExcludesPageOne);
    window->SetPdfJobExpanded(window->m_batch.activePdfJobs.front(), true);
    window->ActivateSourceRailPdfItem(0, 0, true);
    if (!window->m_canvasImagePath.empty() ||
        !window->m_canvas.currentBlocks.empty() ||
        DashboardPdfSourceRailThumbnailPath(window->m_batch.activePdfJobs.front()).size() != 0 ||
        window->BuildSourceRailTaskRows().size() != 2) {
        return fail(L"A 5-only OCR range reused Page 5 as a false Page 1 cover or block provider.");
    }
    window->m_batch.activePdfJobs.front().thumbnailPath = sourcePageImage;
    window->RefreshPdfSelectionViews();
    if (_wcsicmp(window->m_canvasImagePath.c_str(), sourcePageImage.c_str()) != 0 ||
        !window->m_canvas.currentBlocks.empty()) {
        return fail(L"A range-excluded Page 1 cover incorrectly exposed later-page OCR blocks.");
    }

    // Exercise the actual unified Source Rail paint path with a workload larger
    // than the release checklist's 500-Source threshold.  The files are
    // deliberately absent: paint must remain a cache-only operation and must
    // not synchronously stat/decode every off-screen thumbnail while jumping
    // between the beginning and end of the rail.
    resetWindowState();
    window->m_batch.batchTasks.reserve(512);
    for (int index = 0; index < 512; ++index) {
        DashboardBatchTaskItem task;
        task.job.sourceInstanceId = L"{source-rail-perf-" + std::to_wstring(index) + L"}";
        task.job.baseName = L"Source Rail 性能 😀 " + std::to_wstring(index);
        task.job.sourcePath = JoinPathWide(
            runRoot,
            L"missing_source_" + std::to_wstring(index) + L".png");
        task.status = BatchOcrTaskStatus::Completed;
        window->m_batch.batchTasks.push_back(std::move(task));
    }
    window->ApplyFilter(L"");
    window->LayoutControls();
    if (window->BuildSourceRailTaskRows().size() != 512) {
        return fail(L"The 512-Source Rail performance fixture did not retain every visible root.");
    }

    constexpr int sourcePerfWidth = 640;
    constexpr int sourcePerfHeight = 480;
    HDC sourcePerfReferenceDc = GetDC(hwnd);
    HDC sourcePerfDc = sourcePerfReferenceDc
        ? CreateCompatibleDC(sourcePerfReferenceDc)
        : nullptr;
    HBITMAP sourcePerfBitmap = sourcePerfReferenceDc
        ? CreateCompatibleBitmap(sourcePerfReferenceDc, sourcePerfWidth, sourcePerfHeight)
        : nullptr;
    HGDIOBJ sourcePerfOldBitmap = sourcePerfDc && sourcePerfBitmap
        ? SelectObject(sourcePerfDc, sourcePerfBitmap)
        : nullptr;
    if (!sourcePerfReferenceDc || !sourcePerfDc || !sourcePerfBitmap || !sourcePerfOldBitmap) {
        if (sourcePerfOldBitmap) SelectObject(sourcePerfDc, sourcePerfOldBitmap);
        if (sourcePerfBitmap) DeleteObject(sourcePerfBitmap);
        if (sourcePerfDc) DeleteDC(sourcePerfDc);
        if (sourcePerfReferenceDc) ReleaseDC(hwnd, sourcePerfReferenceDc);
        return fail(L"Failed to create the 512-Source Rail paint surface.");
    }

    const int sourcePerfBottomScroll = max(
        0,
        window->GetSourceRailBatchSectionHeight() - sourcePerfHeight);
    LARGE_INTEGER sourcePerfFrequency = {};
    QueryPerformanceFrequency(&sourcePerfFrequency);
    std::vector<double> sourcePerfMilliseconds;
    sourcePerfMilliseconds.reserve(80);
    for (int sample = 0; sample < 90; ++sample) {
        LARGE_INTEGER startCounter = {};
        LARGE_INTEGER endCounter = {};
        QueryPerformanceCounter(&startCounter);
        window->DrawBatchTaskSection(
            sourcePerfDc,
            sourcePerfWidth,
            sourcePerfHeight,
            (sample % 2) == 0 ? 0 : sourcePerfBottomScroll);
        QueryPerformanceCounter(&endCounter);
        if (sample >= 10 && sourcePerfFrequency.QuadPart > 0) {
            sourcePerfMilliseconds.push_back(
                (endCounter.QuadPart - startCounter.QuadPart) * 1000.0 /
                static_cast<double>(sourcePerfFrequency.QuadPart));
        }
    }
    SelectObject(sourcePerfDc, sourcePerfOldBitmap);
    DeleteObject(sourcePerfBitmap);
    DeleteDC(sourcePerfDc);
    ReleaseDC(hwnd, sourcePerfReferenceDc);

    std::sort(sourcePerfMilliseconds.begin(), sourcePerfMilliseconds.end());
    if (sourcePerfMilliseconds.size() != 80) {
        return fail(L"The 512-Source Rail paint contract did not collect 80 warm samples.");
    }
    const double sourcePerfMedian = sourcePerfMilliseconds[39];
    const double sourcePerfP95 = sourcePerfMilliseconds[75];
    const double sourcePerfMax = sourcePerfMilliseconds.back();
    wprintf(
        L"Dashboard 512-Source Rail top/bottom paint: median=%.3fms p95=%.3fms max=%.3fms\n",
        sourcePerfMedian,
        sourcePerfP95,
        sourcePerfMax);
    if (sourcePerfP95 > 100.0) {
        return fail(
            L"Dashboard 512-Source Rail top/bottom paint gross p95 regression: " +
            std::to_wstring(sourcePerfP95) + L"ms.");
    }

    resetWindowState();
    window->m_batch.activePdfJobs.push_back(pdfJob);
    window->AddHistoryItem(makeMouseHistoryItem(0, L"mouse row under pdf batch"));
    window->LayoutControls();
    window->ScrollSourceRailTo(0);
    int pdfJobY = max(1, window->m_metrics.railHeaderH) +
        max(1, window->m_metrics.batchTaskItemH) / 2;
    int collapsedPdfBatchH = window->GetSourceRailBatchSectionHeight();
    int oldPdfPageY = max(1, window->m_metrics.railHeaderH) +
        max(1, window->m_metrics.batchTaskItemH) +
        max(1, window->m_metrics.pdfPageItemH) / 2;
    auto firstVisiblePdfPage = std::find_if(pdfJob.pages.begin(), pdfJob.pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
    if (firstVisiblePdfPage == pdfJob.pages.end()) {
        return fail(L"Source Rail PDF disclosure fixture has no Page 2+ child.");
    }
    const int firstVisiblePdfPageIndex = firstVisiblePdfPage->pageIndex;
    const int visiblePdfPageCount = (int)std::count_if(
        pdfJob.pages.begin(), pdfJob.pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
    int hitPdfJobIndex = -1;
    int hitPdfPageIndex = 0;
    bool hitPdfJobRow = false;
    if (window->IsPdfJobExpanded(pdfJob) ||
        window->HitTestSourceRailBatchRow(oldPdfPageY, hitPdfJobIndex, hitPdfPageIndex, hitPdfJobRow)) {
        return fail(L"Source Rail PDF job should default to a collapsed page tree.");
    }
    SourceRailViewRow pdfRootRow;
    RECT pdfRootRc = {};
    if (!window->HitTestSourceRailViewRow(pdfJobY, pdfRootRow, &pdfRootRc) ||
        pdfRootRow.selection.kind != DashboardSourceRailRowKind::PdfJob ||
        !pdfRootRow.expandable) {
        return fail(L"Source Rail PDF thumbnail-disclosure fixture did not resolve the root row geometry.");
    }
    const RECT pdfThumbRc = window->GetSourceRailThumbnailRect(pdfRootRc);
    const RECT pdfDisclosureRc = window->GetSourceRailPdfDisclosureRect(pdfRootRc);
    SourceRailViewRow historyRootRow;
    RECT historyRootRc = {};
    const int historyRootY = max(1, window->m_metrics.sourceListItemH) +
        max(1, window->m_metrics.sourceListItemH) / 2;
    if (!window->HitTestSourceRailViewRow(historyRootY, historyRootRow, &historyRootRc) ||
        historyRootRow.selection.kind != DashboardSourceRailRowKind::History) {
        return fail(L"Source Rail thumbnail-alignment fixture did not resolve the History root.");
    }
    const RECT historyThumbRc = window->GetSourceRailThumbnailRect(historyRootRc);
    if (pdfThumbRc.left != historyThumbRc.left ||
        pdfThumbRc.right != historyThumbRc.right) {
        return fail(L"Source Rail PDF disclosure shifted the thumbnail baseline away from other roots.");
    }
    if (pdfDisclosureRc.left < pdfThumbRc.left ||
        pdfDisclosureRc.top < pdfThumbRc.top ||
        pdfDisclosureRc.right > pdfThumbRc.right ||
        pdfDisclosureRc.bottom > pdfThumbRc.bottom ||
        (pdfDisclosureRc.left + pdfDisclosureRc.right) <= (pdfThumbRc.left + pdfThumbRc.right) ||
        (pdfDisclosureRc.top + pdfDisclosureRc.bottom) <= (pdfThumbRc.top + pdfThumbRc.bottom)) {
        return fail(L"Source Rail PDF disclosure is not contained in the thumbnail bottom-right quadrant.");
    }
    const int pdfDisclosureX = (pdfDisclosureRc.left + pdfDisclosureRc.right) / 2;
    const int pdfDisclosureY = (pdfDisclosureRc.top + pdfDisclosureRc.bottom) / 2;
    const int pdfThumbnailToggleX = (pdfThumbRc.left + pdfThumbRc.right) / 2;
    const int pdfThumbnailToggleY = (pdfThumbRc.top + pdfThumbRc.bottom) / 2;
    const POINT pdfThumbnailTogglePoint = { pdfThumbnailToggleX, pdfThumbnailToggleY };
    if (PtInRect(&pdfDisclosureRc, pdfThumbnailTogglePoint)) {
        return fail(L"Source Rail PDF thumbnail double-click fixture overlaps the disclosure badge.");
    }
    const int pdfTextX = pdfThumbRc.right + max(1, window->m_metrics.sourceItemTextGap);
    SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, 0, MAKELPARAM(pdfTextX, pdfJobY));
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) >= 0 ||
        !DashboardHistorySelectedIndices(
            window->m_history.model.items,
            DashboardStateSelectedSourceKeys(window->m_dashboardState),
            DashboardStateSelectedHistoryIndex(window->m_dashboardState)).empty()) {
        return fail(L"Source Rail mouse hit-test did not prefer the PDF batch row over history rows.");
    }
    if (!window->m_gdiplusImage) {
        return fail(L"PDF Canvas-reuse fixture did not load its Page 1 image.");
    }
    window->m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
    window->m_dashboardState.canvasView.zoom = 2.25f;
    window->m_dashboardState.canvasView.panX = 23.0f;
    window->m_dashboardState.canvasView.panY = -13.0f;
    SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, 0, MAKELPARAM(pdfTextX, pdfJobY));
    if (window->m_dashboardState.canvasView.viewMode != ImageViewMode::Manual ||
        window->m_dashboardState.canvasView.zoom != 2.25f ||
        window->m_dashboardState.canvasView.panX != 23.0f ||
        window->m_dashboardState.canvasView.panY != -13.0f) {
        return fail(L"Repeated PDF root activation reloaded or refit the unchanged Canvas.");
    }
    SendMessageW(window->m_sourceList, WM_MOUSEMOVE, 0,
        MAKELPARAM(pdfDisclosureX, pdfDisclosureY));
    if (window->m_hoveredPdfDisclosureKey != pdfRootRow.selection.stableSourceKey) {
        return fail(L"Source Rail PDF thumbnail disclosure did not enter hover state.");
    }
    SendMessageW(window->m_sourceList, WM_MOUSELEAVE, 0, 0);
    if (window->m_trackingSourceRailMouse || !window->m_hoveredPdfDisclosureKey.empty()) {
        return fail(L"Source Rail PDF thumbnail disclosure hover state did not clear on mouse leave.");
    }
    SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, 0,
        MAKELPARAM(pdfDisclosureX, pdfDisclosureY));
    if (!window->IsPdfJobExpanded(pdfJob) ||
        window->GetSourceRailBatchSectionHeight() !=
            collapsedPdfBatchH + visiblePdfPageCount * max(1, window->m_metrics.pdfPageItemH) ||
        window->m_dashboardState.canvasView.viewMode != ImageViewMode::Manual ||
        window->m_dashboardState.canvasView.zoom != 2.25f ||
        window->m_dashboardState.canvasView.panX != 23.0f ||
        window->m_dashboardState.canvasView.panY != -13.0f) {
        return fail(L"Source Rail PDF thumbnail disclosure click did not expand the page tree.");
    }
    if (!window->HitTestSourceRailBatchRow(oldPdfPageY, hitPdfJobIndex, hitPdfPageIndex, hitPdfJobRow) ||
        hitPdfJobIndex != 0 ||
        hitPdfPageIndex != firstVisiblePdfPageIndex ||
        hitPdfJobRow) {
        return fail(L"Source Rail expanded PDF page row did not become hit-testable.");
    }
    SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, 0, MAKELPARAM(pdfTextX, oldPdfPageY));
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != firstVisiblePdfPageIndex ||
        window->m_dashboardState.canvasView.viewMode != ImageViewMode::Fit) {
        return fail(L"Source Rail expanded PDF page row did not activate page selection.");
    }
    SendMessageW(window->m_sourceList, WM_LBUTTONDBLCLK, 0,
        MAKELPARAM(pdfDisclosureX, pdfDisclosureY));
    if (!window->IsPdfJobExpanded(pdfJob)) {
        return fail(L"Source Rail PDF thumbnail disclosure double-click toggled the page tree twice.");
    }
    SendMessageW(window->m_sourceList, WM_LBUTTONDBLCLK, 0, MAKELPARAM(pdfTextX, pdfJobY));
    if (window->IsPdfJobExpanded(pdfJob) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
        window->GetSourceRailBatchSectionHeight() != collapsedPdfBatchH) {
        return fail(L"Source Rail PDF title/status double-click did not collapse and settle on the root.");
    }
    SendMessageW(window->m_sourceList, WM_KEYDOWN, VK_RIGHT, 0);
    if (!window->IsPdfJobExpanded(pdfJob)) {
        return fail(L"Source Rail keyboard Right did not re-expand the PDF before repeated root activation.");
    }
    window->m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
    window->m_dashboardState.canvasView.zoom = 1.875f;
    window->m_dashboardState.canvasView.panX = -19.0f;
    window->m_dashboardState.canvasView.panY = 29.0f;
    SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, 0, MAKELPARAM(pdfTextX, pdfJobY));
    SendMessageW(window->m_sourceList, WM_LBUTTONDBLCLK, 0, MAKELPARAM(pdfTextX, pdfJobY));
    if (window->IsPdfJobExpanded(pdfJob) ||
        window->m_dashboardState.canvasView.viewMode != ImageViewMode::Manual ||
        window->m_dashboardState.canvasView.zoom != 1.875f ||
        window->m_dashboardState.canvasView.panX != -19.0f ||
        window->m_dashboardState.canvasView.panY != 29.0f) {
        return fail(L"Source Rail PDF title/status double-click refreshed the unchanged root Canvas.");
    }
    SendMessageW(window->m_sourceList, WM_KEYDOWN, VK_RIGHT, 0);
    if (!window->IsPdfJobExpanded(pdfJob)) {
        return fail(L"Source Rail keyboard Right did not re-expand the PDF before thumbnail double-click.");
    }
    window->m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
    window->m_dashboardState.canvasView.zoom = 1.625f;
    window->m_dashboardState.canvasView.panX = 31.0f;
    window->m_dashboardState.canvasView.panY = -7.0f;
    SendMessageW(window->m_sourceList, WM_LBUTTONDOWN, 0,
        MAKELPARAM(pdfThumbnailToggleX, pdfThumbnailToggleY));
    if (!window->IsPdfJobExpanded(pdfJob) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
        window->m_dashboardState.canvasView.viewMode != ImageViewMode::Manual ||
        window->m_dashboardState.canvasView.zoom != 1.625f ||
        window->m_dashboardState.canvasView.panX != 31.0f ||
        window->m_dashboardState.canvasView.panY != -7.0f) {
        return fail(L"Source Rail PDF thumbnail first click changed disclosure state instead of selecting the root.");
    }
    SendMessageW(window->m_sourceList, WM_LBUTTONDBLCLK, 0,
        MAKELPARAM(pdfThumbnailToggleX, pdfThumbnailToggleY));
    if (window->IsPdfJobExpanded(pdfJob) ||
        window->GetSourceRailBatchSectionHeight() != collapsedPdfBatchH ||
        window->m_dashboardState.canvasView.viewMode != ImageViewMode::Manual ||
        window->m_dashboardState.canvasView.zoom != 1.625f ||
        window->m_dashboardState.canvasView.panX != 31.0f ||
        window->m_dashboardState.canvasView.panY != -7.0f) {
        return fail(L"Source Rail PDF thumbnail double-click refreshed the unchanged root Canvas.");
    }
    SendMessageW(window->m_sourceList, WM_KEYDOWN, VK_RIGHT, 0);
    if (!window->IsPdfJobExpanded(pdfJob)) {
        return fail(L"Source Rail keyboard Right did not expand the selected PDF job.");
    }
    SendMessageW(window->m_sourceList, WM_KEYDOWN, VK_RIGHT, 0);
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) || DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != firstVisiblePdfPageIndex) {
        return fail(L"Source Rail keyboard Right did not enter the first visible Page 2+ row.");
    }
    SendMessageW(window->m_sourceList, WM_KEYDOWN, VK_LEFT, 0);
    if (!DashboardStateHasPdfSelection(window->m_dashboardState) || DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0) {
        return fail(L"Source Rail keyboard Left did not return from PDF page to job row.");
    }
    SendMessageW(window->m_sourceList, WM_KEYDOWN, VK_LEFT, 0);
    if (window->IsPdfJobExpanded(pdfJob)) {
        return fail(L"Source Rail keyboard Left did not collapse the selected PDF job.");
    }
    window->SetPdfJobExpanded(pdfJob, true);
    std::vector<DashboardSourceRailSelectableRow> expandedSelectionRows =
        window->BuildSourceRailSelectableRows();
    auto pageSelectionIt = std::find_if(expandedSelectionRows.begin(), expandedSelectionRows.end(),
        [](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::PdfPage;
        });
    auto historySelectionIt = std::find_if(expandedSelectionRows.begin(), expandedSelectionRows.end(),
        [](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::History;
        });
    if (pageSelectionIt == expandedSelectionRows.end() || historySelectionIt == expandedSelectionRows.end()) {
        return fail(L"Source Rail collapse multi-selection fixture did not expose Page and History rows.");
    }
    window->SetSourceRailSelectionRows({ *pageSelectionIt, *historySelectionIt });
    window->SetPdfJobExpanded(pdfJob, false);
    std::vector<DashboardSourceRailSelectableRow> collapsedSelection =
        window->GetExplicitSelectedSourceRailRows();
    bool retainedPdfRoot = std::any_of(collapsedSelection.begin(), collapsedSelection.end(),
        [](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::PdfJob;
        });
    bool retainedHistoryRoot = std::any_of(collapsedSelection.begin(), collapsedSelection.end(),
        [](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::History;
        });
    bool retainedHiddenPage = std::any_of(collapsedSelection.begin(), collapsedSelection.end(),
        [](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::PdfPage;
        });
    if (!retainedPdfRoot || !retainedHistoryRoot || retainedHiddenPage || collapsedSelection.size() != 2) {
        return fail(L"Collapsing a PDF did not promote/deduplicate hidden Pages while retaining other selected roots.");
    }
    if (!clickHistoryRow(0, 0) ||
        DashboardStateHasPdfSelection(window->m_dashboardState) ||
        !selectedSourcesEqual(std::vector<int>{0}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 0) {
        return fail(L"Source Rail mouse hit-test did not select history rows below the PDF batch section.");
    }

    resetWindowState();
    DashboardBatchTaskItem reorderTaskA;
    reorderTaskA.job.sourceInstanceId = L"{55555555-5555-4555-8555-555555555555}";
    reorderTaskA.job.sourcePath = sourcePageImage;
    reorderTaskA.status = BatchOcrTaskStatus::Completed;
    DashboardBatchTaskItem reorderTaskB = reorderTaskA;
    reorderTaskB.job.sourceInstanceId = L"{66666666-6666-4666-8666-666666666666}";
    window->m_batch.batchTasks = { reorderTaskA, reorderTaskB };
    window->ActivateSourceRailImageTask(0);
    if (!window->m_gdiplusImage) {
        return fail(L"Image Source Canvas-reuse fixture did not load its source image.");
    }
    window->m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
    window->m_dashboardState.canvasView.zoom = 2.375f;
    window->m_dashboardState.canvasView.panX = -27.0f;
    window->m_dashboardState.canvasView.panY = 15.0f;
    window->ActivateSourceRailImageTask(0);
    if (window->m_dashboardState.canvasView.viewMode != ImageViewMode::Manual ||
        window->m_dashboardState.canvasView.zoom != 2.375f ||
        window->m_dashboardState.canvasView.panX != -27.0f ||
        window->m_dashboardState.canvasView.panY != 15.0f) {
        return fail(L"Repeated Image Source activation reloaded or refit the unchanged Canvas.");
    }
    window->ActivateSourceRailImageTask(1);
    if (window->m_dashboardState.canvasView.viewMode != ImageViewMode::Fit) {
        return fail(L"Different Image Sources sharing one file path incorrectly reused the active Canvas identity.");
    }
    std::vector<DashboardSourceRailSelectableRow> reorderRows =
        window->BuildSourceRailSelectableRows();
    auto reorderSelection = std::find_if(reorderRows.begin(), reorderRows.end(),
        [&](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::ImageTask && row.imageTaskIndex == 0;
        });
    if (reorderSelection == reorderRows.end()) {
        return fail(L"Stable image-task reorder fixture did not expose its Source row.");
    }
    window->SetSourceRailSelectionRows({ *reorderSelection });
    std::swap(window->m_batch.batchTasks[0], window->m_batch.batchTasks[1]);
    std::vector<DashboardSourceRailSelectableRow> reorderedSelection =
        window->GetSelectedBatchRows();
    if (reorderedSelection.size() != 1 || reorderedSelection.front().imageTaskIndex != 1 ||
        window->m_batch.batchTasks[(size_t)reorderedSelection.front().imageTaskIndex].job.sourceInstanceId !=
            reorderTaskA.job.sourceInstanceId) {
        return fail(L"Image Source selection followed a backing index instead of its stable key after reorder.");
    }

    resetWindowState();
    DashboardBatchTaskItem duplicateIdentityA;
    duplicateIdentityA.job.sourceInstanceId = L"{77777777-7777-4777-8777-777777777777}";
    duplicateIdentityA.job.sourcePath = sourcePageImage;
    duplicateIdentityA.job.outputRoot = JoinPathWide(runRoot, L"duplicate_identity_root_a");
    duplicateIdentityA.job.outputDir = JoinPathWide(duplicateIdentityA.job.outputRoot, L"a");
    duplicateIdentityA.job.manifestPath = JoinPathWide(duplicateIdentityA.job.outputDir, L"manifest.json");
    duplicateIdentityA.status = BatchOcrTaskStatus::Completed;
    DashboardBatchTaskItem duplicateIdentityB = duplicateIdentityA;
    duplicateIdentityB.job.outputRoot = JoinPathWide(runRoot, L"duplicate_identity_root_b");
    duplicateIdentityB.job.outputDir = JoinPathWide(duplicateIdentityB.job.outputRoot, L"b");
    duplicateIdentityB.job.manifestPath = JoinPathWide(duplicateIdentityB.job.outputDir, L"manifest.json");
    window->m_batch.batchTasks = { duplicateIdentityA, duplicateIdentityB };
    window->ApplyFilter(L"");
    window->ActivateSourceRailImageTask(1);
    const DashboardBatchTaskItem* selectedDuplicateIdentity = window->GetSelectedImageTask();
    if (selectedDuplicateIdentity != &window->m_batch.batchTasks[1] ||
        DashboardStateImageTaskSelectionStableKey(window->m_dashboardState).empty()) {
        return fail(L"Duplicate sourceInstanceId selection resolved to the first backing task.");
    }
    window->m_testAutoConfirmDelete = true;
    window->DeleteSelectedSources();
    window->m_testAutoConfirmDelete = false;
    if (window->m_batch.batchTasks.size() != 1 ||
        NormalizePathForCompare(window->m_batch.batchTasks.front().job.manifestPath) !=
            NormalizePathForCompare(duplicateIdentityA.job.manifestPath)) {
        return fail(L"Duplicate sourceInstanceId Remove affected more than the selected stable Source.");
    }

    resetWindowState();
    BatchOcrImageJob queuedDeleteJob;
    queuedDeleteJob.sourceInstanceId = L"{44444444-4444-4444-8444-444444444444}";
    queuedDeleteJob.sourcePath = sourcePageImage;
    queuedDeleteJob.baseName = L"queued_delete_contract";
    window->UpsertBatchTask(queuedDeleteJob, BatchOcrTaskStatus::Pending);
    DashboardQueuedOcr queuedDelete;
    queuedDelete.filePath = sourcePageImage;
    queuedDelete.hasImageTask = true;
    queuedDelete.imageTaskJob = queuedDeleteJob;
    window->m_batch.dropQueue.push_back(queuedDelete);
    window->ActivateSourceRailImageTask(0);
    window->m_testAutoConfirmDelete = true;
    window->DeleteSelectedSources();
    window->m_testAutoConfirmDelete = false;
    if (window->m_batch.batchTasks.size() != 1 || window->m_batch.dropQueue.size() != 1) {
        return fail(L"Remove deleted a Pending/queued image Source without an explicit cancel path.");
    }

    resetWindowState();
    BatchOcrImageJob crossSelectImageJob;
    crossSelectImageJob.index = 77;
    crossSelectImageJob.sourcePath = sourcePageImage;
    crossSelectImageJob.sourceImagePath = sourcePageImage;
    crossSelectImageJob.outputRoot = runRoot;
    crossSelectImageJob.outputDir = runRoot + L"\\cross_select_image";
    crossSelectImageJob.baseName = L"cross_select_image";
    crossSelectImageJob.manifestPath = crossSelectImageJob.outputDir + L"\\manifest.json";
    window->UpsertBatchTask(crossSelectImageJob, BatchOcrTaskStatus::Completed, 11, L"");
    window->AddHistoryItem(makeMouseHistoryItem(0, L"cross select history row"));
    window->LayoutControls();
    window->ScrollSourceRailTo(0);
    DashboardSourceRailSelectableRow crossImageRow;
    crossImageRow.kind = DashboardSourceRailRowKind::ImageTask;
    crossImageRow.imageTaskIndex = 0;
    window->ActivateSourceRailBatchRow(crossImageRow, false, false);
    window->ActivateSourceRailItem(0, true, false);
    if (window->GetSelectedBatchRows().size() != 1 ||
        !selectedSourcesEqual(std::vector<int>{0}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 0) {
        return fail(L"Source Rail Ctrl-selection did not preserve both image batch and history rows.");
    }
    window->ActivateSourceRailBatchRow(crossImageRow, true, false);
    if (!window->GetSelectedBatchRows().empty() ||
        !selectedSourcesEqual(std::vector<int>{0}) ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != 0) {
        return fail(L"Source Rail Ctrl-click did not remove a selected batch row from mixed selection.");
    }
    window->ActivateSourceRailBatchRow(crossImageRow, true, false);
    if (window->GetSelectedBatchRows().size() != 1 ||
        !selectedSourcesEqual(std::vector<int>{0})) {
        return fail(L"Source Rail Ctrl-click did not restore a batch row to mixed selection.");
    }
    window->ActivateSourceRailItem(0, true, false);
    if (window->GetSelectedBatchRows().size() != 1 ||
        !DashboardHistorySelectedIndices(
            window->m_history.model.items,
            DashboardStateSelectedSourceKeys(window->m_dashboardState),
            DashboardStateSelectedHistoryIndex(window->m_dashboardState)).empty() ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) != -1) {
        return fail(L"Source Rail Ctrl-click did not remove a selected history row from mixed selection.");
    }
    window->ActivateSourceRailItem(0, true, false);
    if (window->GetSelectedBatchRows().size() != 1 ||
        !selectedSourcesEqual(std::vector<int>{0})) {
        return fail(L"Source Rail Ctrl-click did not restore a history row to mixed selection.");
    }
    window->HandleSourceRailKey(L'A', true, false);
    if (window->GetSelectedBatchRows().size() != 1 ||
        !selectedSourcesEqual(std::vector<int>{0})) {
        return fail(L"Source Rail Ctrl+A did not select both batch and history rows.");
    }
    window->UpdatePreviewControls();
    if (window->m_copyBtn && IsWindowEnabled(window->m_copyBtn)) {
        return fail(L"Mixed Source selection exposed the single-result Copy action.");
    }
    window->m_testAutoConfirmDelete = true;
    window->DeleteSelectedSources();
    window->m_testAutoConfirmDelete = false;
    if (!window->m_batch.batchTasks.empty() ||
        !window->m_history.model.items.empty() ||
        !window->GetSelectedBatchRows().empty() ||
        !DashboardHistorySelectedIndices(
            window->m_history.model.items,
            DashboardStateSelectedSourceKeys(window->m_dashboardState),
            DashboardStateSelectedHistoryIndex(window->m_dashboardState)).empty()) {
        return fail(L"Source Rail mixed delete did not remove both image batch and history rows.");
    }

    resetWindowState();
    BatchOcrPdfJob pageOnlyDeleteJob = pdfJob;
    if (pageOnlyDeleteJob.pages.size() < 2) {
        BatchOcrPdfPageJob secondDeletePage = pageOnlyDeleteJob.pages.front();
        secondDeletePage.pageIndex = pageOnlyDeleteJob.pages.front().pageIndex + 1;
        pageOnlyDeleteJob.pages.push_back(secondDeletePage);
    }
    const size_t pageCountBeforeDelete = pageOnlyDeleteJob.pages.size();
    auto pageOnlyDeleteChild = std::find_if(
        pageOnlyDeleteJob.pages.begin(), pageOnlyDeleteJob.pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
    if (pageOnlyDeleteChild == pageOnlyDeleteJob.pages.end()) {
        return fail(L"PDF Page Remove fixture has no visible Page 2+ child.");
    }
    const int deletedPageIndex = pageOnlyDeleteChild->pageIndex;
    window->m_batch.activePdfJobs.push_back(pageOnlyDeleteJob);
    window->ActivateSourceRailPdfItem(0, deletedPageIndex, false);
    window->m_testAutoConfirmDelete = true;
    window->DeleteSelectedSources();
    window->m_testAutoConfirmDelete = false;
    if (window->m_batch.activePdfJobs.size() != 1 ||
        window->m_batch.activePdfJobs.front().pages.size() != pageCountBeforeDelete ||
        !DashboardFindPdfSelectionPage(window->m_batch.activePdfJobs.front(), deletedPageIndex) ||
        !DashboardStateHasPdfSelection(window->m_dashboardState) ||
        DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != deletedPageIndex ||
        !PathFileExistsW(pageOnlyDeleteJob.sourcePath.c_str()) ||
        GetFileAttributesW(pageOnlyDeleteJob.outputDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return fail(L"PDF Page-only Remove bypassed the root-only durability boundary.");
    }

    resetWindowState();
    window->m_batch.activePdfJobs.push_back(pageOnlyDeleteJob);
    window->SetPdfJobExpanded(pageOnlyDeleteJob, true);
    std::vector<DashboardSourceRailSelectableRow> pdfDeleteRows =
        window->BuildSourceRailSelectableRows();
    auto pdfRootDeleteRow = std::find_if(pdfDeleteRows.begin(), pdfDeleteRows.end(),
        [](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::PdfJob;
        });
    auto pdfPageDeleteRow = std::find_if(pdfDeleteRows.begin(), pdfDeleteRows.end(),
        [](const DashboardSourceRailSelectableRow& row) {
            return row.kind == DashboardSourceRailRowKind::PdfPage;
        });
    if (pdfRootDeleteRow == pdfDeleteRows.end() || pdfPageDeleteRow == pdfDeleteRows.end()) {
        return fail(L"Mixed PDF root/Page delete fixture did not expose both row types.");
    }
    window->SetSourceRailSelectionRows({ *pdfRootDeleteRow, *pdfPageDeleteRow });
    window->m_testAutoConfirmDelete = true;
    window->DeleteSelectedSources();
    window->m_testAutoConfirmDelete = false;
    if (!window->m_batch.activePdfJobs.empty() ||
        !PathFileExistsW(pageOnlyDeleteJob.sourcePath.c_str()) ||
        GetFileAttributesW(pageOnlyDeleteJob.outputDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return fail(L"Mixed PDF root/Page Remove did not deduplicate the owner or preserve source/output files.");
    }

    resetWindowState();
    std::wstring pauseRoot = runRoot + L"_pdf_pause";
    if (!BatchOcrWriter::EnsureDirectory(pauseRoot)) {
        return fail(L"Failed to create PDF pause contract output root.");
    }
    BatchOcrPdfJob pausePdfJob;
    if (!controller.CreatePdfJob(resolvedPdfPath, pauseRoot, pausePdfJob, setupError)) {
        return fail(setupError.empty()
            ? L"Failed to create PDF pause contract job."
            : setupError);
    }
    pausePdfJob.pageRange = L"1,2";
    if (!window->m_batchController.InitializePdfPages(pausePdfJob, {1, 2}, setupError)) {
        return fail(setupError.empty()
            ? L"Failed to initialize PDF pause contract pages."
            : setupError);
    }
    for (auto& page : pausePdfJob.pages) {
        page.sourceImagePath = sourcePageImage;
    }
    BatchOcrWriteResult pausePending = BatchOcrWriter::WritePdfPending(pausePdfJob);
    if (!pausePending.success) {
        return fail(pausePending.error.empty()
            ? L"Failed to write PDF pause pending manifest."
            : pausePending.error);
    }
    window->UpsertActivePdfJob(pausePdfJob);
    window->SetPdfJobPaused(pausePdfJob, true);
    if (!window->IsPdfJobPaused(pausePdfJob)) {
        return fail(L"PDF job-level pause state was not recorded.");
    }
    window->QueuePdfPageFile(sourcePageImage, pausePdfJob, pausePdfJob.pages[0]);
    window->QueuePdfPageFile(sourcePageImage, pausePdfJob, pausePdfJob.pages[1]);
    if (DashboardStateIsOcrBusy(window->m_dashboardState) || window->m_batch.dropQueue.size() != 2) {
        return fail(L"Paused PDF job should leave queued pages undispatched.");
    }

    window->SetPdfPagePaused(pausePdfJob, 2, true);
    if (!window->IsPdfPagePaused(pausePdfJob, 2)) {
        return fail(L"PDF page-level pause state was not recorded.");
    }
    window->SetPdfJobPaused(pausePdfJob, false);
    bool firstPausePageFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        BatchOcrPdfJob* active = window->FindActivePdfJob(pausePdfJob);
        if (!active || active->pages.size() < 2) return false;
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.size() == 1 &&
            active->pages[0].status == BatchOcrTaskStatus::Completed &&
            active->pages[1].status == BatchOcrTaskStatus::Pending;
    });
    if (!firstPausePageFinished) {
        return fail(L"PDF page-level pause did not hold the paused page after dispatching other pages.");
    }
    window->SetPdfPagePaused(pausePdfJob, 2, false);
    bool resumedPausePageFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        BatchOcrPdfJob* active = window->FindActivePdfJob(pausePdfJob);
        if (!active || active->pages.size() < 2) return false;
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            active->pages[0].status == BatchOcrTaskStatus::Completed &&
            active->pages[1].status == BatchOcrTaskStatus::Completed;
    });
    if (!resumedPausePageFinished) {
        return fail(L"PDF page-level resume did not dispatch the held page.");
    }

    resetWindowState();
    std::wstring pendingResumeRoot = runRoot + L"_pdf_pending_resume";
    if (!BatchOcrWriter::EnsureDirectory(pendingResumeRoot)) {
        return fail(L"Failed to create PDF pending resume contract output root.");
    }
    BatchOcrPdfJob pendingResumePdfJob;
    if (!controller.CreatePdfJob(resolvedPdfPath, pendingResumeRoot, pendingResumePdfJob, setupError)) {
        return fail(setupError.empty()
            ? L"Failed to create PDF pending resume contract job."
            : setupError);
    }
    pendingResumePdfJob.pageRange = L"1,2";
    if (!window->m_batchController.InitializePdfPages(pendingResumePdfJob, {1, 2}, setupError)) {
        return fail(setupError.empty()
            ? L"Failed to initialize PDF pending resume contract pages."
            : setupError);
    }
    if (!BatchOcrWriter::EnsureDirectory(pendingResumePdfJob.pageImagesDir)) {
        return fail(L"Failed to prepare PDF pending resume page image directory.");
    }
    for (auto& page : pendingResumePdfJob.pages) {
        wchar_t pageImageName[32] = {};
        swprintf_s(pageImageName, L"page_%04d.png", page.pageIndex);
        std::wstring pageImagePath = JoinPathWide(pendingResumePdfJob.pageImagesDir, pageImageName);
        if (!CopyFileW(sourcePageImage.c_str(), pageImagePath.c_str(), FALSE)) {
            return fail(L"Failed to copy PDF pending resume page image fixture.");
        }
        page.sourceImagePath = pageImagePath;
    }
    BatchOcrWriteResult pendingResumeWrite = BatchOcrWriter::WritePdfPending(pendingResumePdfJob);
    if (!pendingResumeWrite.success) {
        return fail(pendingResumeWrite.error.empty()
            ? L"Failed to write PDF pending resume manifest."
            : pendingResumeWrite.error);
    }

    window->SetPdfPagePaused(pendingResumePdfJob, 2, true);
    if (!window->LoadBatchOutputSnapshot(pendingResumeRoot, false, false)) {
        return fail(L"Dashboard PDF pending resume snapshot did not load.");
    }
    if (!DashboardStateIsBatchPaused(window->m_dashboardState) ||
        DashboardStateIsOcrBusy(window->m_dashboardState) ||
        window->m_batch.dropQueue.size() != 2 ||
        !window->m_batch.failedPdfPages.empty()) {
        return fail(L"Dashboard PDF pending resume did not rebuild a paused queue without misclassifying pending pages as failed.");
    }
    BatchOcrPdfJob* pendingActive = window->FindActivePdfJob(pendingResumePdfJob);
    if (!pendingActive ||
        pendingActive->pages.size() != 2 ||
        pendingActive->pages[0].status != BatchOcrTaskStatus::Pending ||
        pendingActive->pages[1].status != BatchOcrTaskStatus::Pending) {
        return fail(L"Dashboard PDF pending resume did not restore pending page state.");
    }

    window->ToggleBatchPause();
    bool firstPendingResumeFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        BatchOcrPdfJob* active = window->FindActivePdfJob(pendingResumePdfJob);
        if (!active || active->pages.size() < 2) return false;
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.size() == 1 &&
            active->pages[0].status == BatchOcrTaskStatus::Completed &&
            active->pages[1].status == BatchOcrTaskStatus::Pending;
    });
    if (!firstPendingResumeFinished) {
        return fail(L"Dashboard PDF pending resume did not continue the unpaused pending page.");
    }

    window->SetPdfPagePaused(pendingResumePdfJob, 2, false);
    bool secondPendingResumeFinished = DashboardWindowTestPumpUntil(hwnd, 20000, [&]() {
        BatchOcrPdfJob* active = window->FindActivePdfJob(pendingResumePdfJob);
        if (!active || active->pages.size() < 2) return false;
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            active->pages[0].status == BatchOcrTaskStatus::Completed &&
            active->pages[1].status == BatchOcrTaskStatus::Completed;
    });
    if (!secondPendingResumeFinished) {
        return fail(L"Dashboard PDF pending resume did not dispatch the paused page after Resume.");
    }

    resetWindowState();
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        pendingResumeRoot,
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        { pendingResumeRoot, pauseRoot });
    window->SaveBatchSessionState();
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        L"",
        DashboardStateRecentBatchOutputRoots(window->m_dashboardState));
    DashboardStateApplyBatchOutputRoots(
        window->m_dashboardState,
        DashboardStatePreferredBatchOutputRoot(window->m_dashboardState),
        DashboardStateLastBatchOutputRoot(window->m_dashboardState),
        {});
    window->LoadBatchSessionState();
    window->AutoResumeLastBatchOutputRoot();
    auto countLoadedRoot = [&](const std::wstring& root) {
        int count = 0;
        std::wstring normalizedRoot = NormalizePathForCompare(root);
        for (const auto& job : window->m_batch.activePdfJobs) {
            if (NormalizePathForCompare(job.outputRoot) == normalizedRoot) {
                count++;
            }
        }
        return count;
    };
    if (countLoadedRoot(pendingResumeRoot) != 1 ||
        countLoadedRoot(pauseRoot) != 1 ||
        window->m_batch.activePdfJobs.size() < 2) {
        return fail(L"Dashboard auto resume did not restore multiple recent batch output roots into the task list.");
    }
    window->m_dashboardState.expandedPdfJobKeys.clear();
    window->ApplyFilter(L"");
    int unfilteredTaskRowCount = (int)window->BuildSourceRailTaskRows().size();
    if (unfilteredTaskRowCount < 2) {
        return fail(L"Source Rail task search contract needs at least two restored task rows.");
    }
    window->ApplyFilter(L"pdf_pending_resume");
    std::vector<SourceRailTaskRow> filteredRootRows = window->BuildSourceRailTaskRows();
    int filteredRootJobCount = 0;
    bool filteredRootMatchesExpected = false;
    for (const auto& row : filteredRootRows) {
        if (row.kind != SourceRailTaskRowKind::PdfJob) continue;
        filteredRootJobCount++;
        if (row.pdfJobIndex >= 0 && row.pdfJobIndex < (int)window->m_batch.activePdfJobs.size() &&
            NormalizePathForCompare(window->m_batch.activePdfJobs[(size_t)row.pdfJobIndex].outputRoot) ==
                NormalizePathForCompare(pendingResumeRoot)) {
            filteredRootMatchesExpected = true;
        }
    }
    if (filteredRootJobCount != 1 ||
        !filteredRootMatchesExpected) {
        return fail(L"Source Rail search did not filter recent-root PDF jobs down to the matching task.");
    }

    window->ScrollSourceRailTo(0);
    window->ApplyFilter(L"page_0002");
    std::vector<SourceRailTaskRow> filteredPageRows = window->BuildSourceRailTaskRows();
    int firstPageRow = -1;
    for (int i = 0; i < (int)filteredPageRows.size(); ++i) {
        if (filteredPageRows[(size_t)i].kind == SourceRailTaskRowKind::PdfPage &&
            filteredPageRows[(size_t)i].pageIndex == 2) {
            firstPageRow = i;
            break;
        }
    }
    if (firstPageRow < 0) {
        return fail(L"Source Rail search did not expose matching PDF page rows.");
    }
    int pageHitY = max(1, window->m_metrics.railHeaderH);
    for (int i = 0; i < firstPageRow; ++i) {
        pageHitY += filteredPageRows[(size_t)i].kind == SourceRailTaskRowKind::PdfPage
            ? max(1, window->m_metrics.pdfPageItemH)
            : max(1, window->m_metrics.batchTaskItemH);
    }
    pageHitY += max(1, window->m_metrics.pdfPageItemH) / 2;
    int filteredHitJobIndex = -1;
    int filteredHitPageIndex = 0;
    bool filteredHitJobRow = false;
    if (!window->HitTestSourceRailBatchRow(
            pageHitY,
            filteredHitJobIndex,
            filteredHitPageIndex,
            filteredHitJobRow) ||
        filteredHitJobRow ||
        filteredHitPageIndex != 2) {
        return fail(L"Source Rail search-filtered PDF page row was not hit-testable.");
    }

    DashboardBatchTaskItem imageSearchTask;
    imageSearchTask.job.baseName = L"manual image task";
    imageSearchTask.job.sourcePath = sourcePageImage;
    imageSearchTask.job.outputRoot = runRoot + L"_manual_image_search_root";
    imageSearchTask.status = BatchOcrTaskStatus::Failed;
    imageSearchTask.error = L"needle-image-error";
    window->m_batch.batchTasks.push_back(imageSearchTask);
    window->ApplyFilter(L"needle-image-error");
    std::vector<SourceRailTaskRow> filteredImageRows = window->BuildSourceRailTaskRows();
    if (filteredImageRows.size() != 1 ||
        filteredImageRows.front().kind != SourceRailTaskRowKind::ImageTask ||
        window->GetSourceRailBatchSectionHeight() <= 0) {
        return fail(L"Source Rail search did not filter image batch task rows.");
    }
    int imageHitY = max(1, window->m_metrics.railHeaderH) +
        max(1, window->m_metrics.batchTaskItemH) / 2;
    if (window->HitTestSourceRailBatchRow(
            imageHitY,
            filteredHitJobIndex,
            filteredHitPageIndex,
            filteredHitJobRow)) {
        return fail(L"Source Rail image task rows should stay visible but not be treated as PDF hit-test rows.");
    }
    window->ApplyFilter(L"");

    resetWindowState();
    SetWindowPos(hwnd, nullptr, 0, 0, 1200, 800,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->LayoutControls();
    SetWindowPos(hwnd, HWND_TOPMOST, 32, 32, 1200, 800,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    window->LayoutControls();

    auto settleDashboardPixels = [&]() {
        RedrawWindow(hwnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        if (window->m_splitterTracker && IsWindowVisible(window->m_splitterTracker)) {
            RedrawWindow(window->m_splitterTracker, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
        DWORD settleStart = GetTickCount();
        DashboardWindowTestPumpUntil(hwnd, 120, [&]() {
            return GetTickCount() - settleStart >= 40;
        });
    };

    auto captureHasTrackerStripe = [&](int splitterLeftX, bool& captureOk) -> bool {
        int captureW = 0;
        int captureH = 0;
        std::vector<COLORREF> capturePixels;
        captureOk = DashboardWindowTestCaptureClientPixels(hwnd, captureW, captureH, capturePixels);
        if (!captureOk) return false;
        int mainY = window->m_metrics.margin +
            max(window->m_metrics.commandBarH, window->m_metrics.buttonH) +
            window->m_metrics.spacing;
        return DashboardWindowTestHasLongTrackerStripe(
            capturePixels,
            captureW,
            captureH,
            splitterLeftX,
            max(2, window->m_metrics.splitterW),
            mainY,
            captureH);
    };

    auto captureTrackerWindowHasStripe = [&](bool& captureOk) -> bool {
        int captureW = 0;
        int captureH = 0;
        std::vector<COLORREF> capturePixels;
        captureOk = window->m_splitterTracker &&
            DashboardWindowTestCaptureClientPixels(window->m_splitterTracker, captureW, captureH, capturePixels);
        if (!captureOk) return false;
        return DashboardWindowTestHasLongTrackerStripe(
            capturePixels,
            captureW,
            captureH,
            0,
            captureW,
            0,
            captureH);
    };

    // Pure geometry contract: persisted intent and responsive visibility stay
    // separate, and hidden panes/splitters consume no width.
    {
        DashboardLayoutMetrics metrics;
        DashboardLayoutState state;
        DashboardResponsiveState responsive;
        auto wide = ResolveDashboardLayout({1400, 800}, 40, metrics, state, responsive);
        if (!wide.sourceVisible || !wide.resultVisible ||
            wide.sourceSplitterRc.right <= wide.sourceSplitterRc.left ||
            wide.resultSplitterRc.right <= wide.resultSplitterRc.left) {
            return fail(L"Dashboard layout solver did not resolve a three-pane wide layout.");
        }
        state.sourceVisible = false;
        auto sourceHidden = ResolveDashboardLayout({1400, 800}, 40, metrics, state, responsive);
        if (sourceHidden.sourceVisible ||
            sourceHidden.sourceRc.right != sourceHidden.sourceRc.left ||
            sourceHidden.sourceSplitterRc.right != sourceHidden.sourceSplitterRc.left ||
            sourceHidden.canvasRc.left != metrics.margin) {
            return fail(L"Dashboard layout solver did not make hidden Source geometry zero-width.");
        }
        state.sourceVisible = true;
        responsive = {};
        auto narrow = ResolveDashboardLayout({760, 700}, 40, metrics, state, responsive);
        if (!narrow.sourceVisible || narrow.resultVisible || !narrow.resultAutoHidden ||
            !state.sourceVisible || !state.resultVisible ||
            narrow.sourceRc.right - narrow.sourceRc.left != state.sourceWidth) {
            return fail(L"Dashboard responsive layout changed intent or hid the wrong default pane.");
        }
        responsive.preferredPane = DashboardSidePane::Result;
        auto preferred = ResolveDashboardLayout({760, 700}, 40, metrics, state, responsive);
        if (preferred.sourceVisible || !preferred.resultVisible ||
            !preferred.sourceAutoHidden || !state.sourceVisible || !state.resultVisible ||
            preferred.resultRc.right - preferred.resultRc.left != state.resultWidth) {
            return fail(L"Dashboard preferred-pane switch did not reveal Result without changing intent.");
        }

        state.sourceVisible = false;
        responsive = {};
        responsive.resultAutoHidden = true;
        auto heldBySlack = ResolveDashboardLayout({587, 700}, 40, metrics, state, responsive);
        if (heldBySlack.resultVisible || !heldBySlack.resultAutoHidden) {
            return fail(L"Dashboard restore hysteresis did not hold an auto-hidden Result pane.");
        }
        responsive.resultAutoHidden = false;
        auto explicitlyRequested = ResolveDashboardLayout({587, 700}, 40, metrics, state, responsive);
        if (!explicitlyRequested.resultVisible || explicitlyRequested.resultAutoHidden ||
            !state.resultVisible) {
            return fail(L"Dashboard explicit Result request remained blocked by restore hysteresis.");
        }

        for (int clientW = 500; clientW <= 1800; clientW += 13) {
            for (int intentMask = 0; intentMask < 4; intentMask++) {
                for (int autoMask = 0; autoMask < 4; autoMask++) {
                    for (int preferredPane = 0; preferredPane < 2; preferredPane++) {
                        DashboardLayoutState matrixState;
                        matrixState.sourceWidth = 410;
                        matrixState.resultWidth = 570;
                        matrixState.sourceVisible = (intentMask & 1) != 0;
                        matrixState.resultVisible = (intentMask & 2) != 0;
                        DashboardResponsiveState matrixResponsive;
                        matrixResponsive.sourceAutoHidden = (autoMask & 1) != 0;
                        matrixResponsive.resultAutoHidden = (autoMask & 2) != 0;
                        matrixResponsive.preferredPane = preferredPane == 0
                            ? DashboardSidePane::Source : DashboardSidePane::Result;
                        auto matrix = ResolveDashboardLayout(
                            {clientW, 700}, 40, metrics, matrixState, matrixResponsive);
                        int canvasW = matrix.canvasRc.right - matrix.canvasRc.left;
                        bool invalid =
                            (!matrixState.sourceVisible && matrix.sourceVisible) ||
                            (!matrixState.resultVisible && matrix.resultVisible) ||
                            matrix.sourceAutoHidden != (matrixState.sourceVisible && !matrix.sourceVisible) ||
                            matrix.resultAutoHidden != (matrixState.resultVisible && !matrix.resultVisible) ||
                            canvasW < metrics.canvasMinW ||
                            matrix.canvasRc.left < metrics.margin ||
                            matrix.canvasRc.right > clientW - metrics.margin;
                        if (matrix.sourceVisible) {
                            invalid = invalid ||
                                matrix.sourceRc.right - matrix.sourceRc.left < metrics.sourceMinW ||
                                matrix.sourceRc.left != metrics.margin ||
                                matrix.sourceRc.right > matrix.sourceSplitterRc.left ||
                                matrix.sourceSplitterRc.right > matrix.canvasRc.left;
                        } else {
                            invalid = invalid ||
                                matrix.sourceRc.right != matrix.sourceRc.left ||
                                matrix.sourceSplitterRc.right != matrix.sourceSplitterRc.left;
                        }
                        if (matrix.resultVisible) {
                            invalid = invalid ||
                                matrix.resultRc.right - matrix.resultRc.left < metrics.resultMinW ||
                                matrix.canvasRc.right > matrix.resultSplitterRc.left ||
                                matrix.resultSplitterRc.right > matrix.resultRc.left ||
                                matrix.resultRc.right != clientW - metrics.margin;
                        } else {
                            invalid = invalid ||
                                matrix.resultRc.right != matrix.resultRc.left ||
                                matrix.resultSplitterRc.right != matrix.resultSplitterRc.left;
                        }
                        if (invalid) {
                            return fail(L"Dashboard layout solver matrix produced invalid or overlapping geometry.");
                        }
                    }
                }
            }
        }
    }

    int beforeCancelDragSourceWidth = window->m_layout.sourceWidth;
    int cancelDragY = window->m_metrics.margin +
        max(window->m_metrics.commandBarH, window->m_metrics.buttonH) +
        window->m_metrics.spacing +
        max(6, window->m_metrics.splitterW);
    int cancelDragStartX = DashboardStateSourceSplitterX(window->m_dashboardState) + window->m_metrics.splitterW / 2;
    int cancelDragTargetX = cancelDragStartX + max(36, window->Scale(72));
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cancelDragStartX, cancelDragY));
    if (!DashboardStateIsSplitterPressPending(window->m_dashboardState) || DashboardStateIsDraggingSplitter(window->m_dashboardState) ||
        DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind != 1 ||
        (window->m_splitterTracker && IsWindowVisible(window->m_splitterTracker))) {
        return fail(L"Source Rail splitter press did not remain pending before drag slop.");
    }
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(cancelDragTargetX, cancelDragY));
    if (!DashboardStateIsDraggingSplitter(window->m_dashboardState) || DashboardStateIsSplitterPressPending(window->m_dashboardState)) {
        return fail(L"Source Rail splitter cancel test did not cross drag slop.");
    }
    SendMessageW(hwnd, WM_CAPTURECHANGED, 0, 0);
    ReleaseCapture();
    if (DashboardStateIsDraggingSplitter(window->m_dashboardState) ||
        DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind != 0 ||
        (window->m_splitterTracker && IsWindowVisible(window->m_splitterTracker)) ||
        window->m_layout.sourceWidth != beforeCancelDragSourceWidth) {
        return fail(L"Source Rail splitter canceled drag did not settle without committing width or leaving the tracker visible.");
    }

    int beforeDragSourceWidth = window->m_layout.sourceWidth;
    int splitterDragY = window->m_metrics.margin +
        max(window->m_metrics.commandBarH, window->m_metrics.buttonH) +
        window->m_metrics.spacing +
        max(6, window->m_metrics.splitterW);
    int splitterDragStartX = DashboardStateSourceSplitterX(window->m_dashboardState) + window->m_metrics.splitterW / 2;
    int firstPreviewTargetX = splitterDragStartX + max(32, window->Scale(64));
    int splitterDragTargetX = splitterDragStartX + max(96, window->Scale(160));
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(splitterDragStartX, splitterDragY));
    if (!DashboardStateIsSplitterPressPending(window->m_dashboardState) || DashboardStateIsDraggingSplitter(window->m_dashboardState) || DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind != 1) {
        return fail(L"Source Rail splitter drag did not enter pending press mode.");
    }
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(firstPreviewTargetX, splitterDragY));
    if (!DashboardStateIsDraggingSplitter(window->m_dashboardState) || DashboardStateIsSplitterPressPending(window->m_dashboardState)) {
        return fail(L"Source Rail splitter drag did not cross drag slop.");
    }
    int firstPreviewSplitterX = DashboardStateSplitterDragPreviewX(window->m_dashboardState);
    settleDashboardPixels();
    bool captureOk = false;
    if (!captureTrackerWindowHasStripe(captureOk)) {
        return fail(captureOk
            ? L"Source Rail splitter pixel contract did not see the live tracker at the first preview position."
            : L"Source Rail splitter pixel contract could not capture Dashboard client pixels.");
    }
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(splitterDragTargetX, splitterDragY));
    int secondPreviewSplitterX = DashboardStateSplitterDragPreviewX(window->m_dashboardState);
    settleDashboardPixels();
    if (captureHasTrackerStripe(firstPreviewSplitterX, captureOk)) {
        return fail(L"Source Rail splitter left a tracker-colored stripe at the previous preview position while dragging.");
    }
    if (!captureOk) {
        return fail(L"Source Rail splitter pixel contract could not capture Dashboard client pixels after tracker move.");
    }
    if (!captureTrackerWindowHasStripe(captureOk)) {
        return fail(captureOk
            ? L"Source Rail splitter pixel contract did not see the live tracker at the current preview position."
            : L"Source Rail splitter pixel contract could not capture Dashboard client pixels at the current preview position.");
    }
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(splitterDragTargetX, splitterDragY));
    settleDashboardPixels();
    if (DashboardStateIsDraggingSplitter(window->m_dashboardState) ||
        DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind != 0 ||
        (window->m_splitterTracker && IsWindowVisible(window->m_splitterTracker)) ||
        window->m_layout.sourceWidth <= beforeDragSourceWidth ||
        DashboardStateSourceSplitterX(window->m_dashboardState) <= splitterDragStartX) {
        return fail(
            L"Source Rail splitter drag commit did not resize and settle cleanly. dragging=" +
            std::to_wstring(DashboardStateIsDraggingSplitter(window->m_dashboardState) ? 1 : 0) +
            L" kind=" + std::to_wstring(DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind) +
            L" trackerVisible=" + std::to_wstring((window->m_splitterTracker && IsWindowVisible(window->m_splitterTracker)) ? 1 : 0) +
            L" sourceBefore=" + std::to_wstring(beforeDragSourceWidth) +
            L" sourceAfter=" + std::to_wstring(window->m_layout.sourceWidth) +
            L" startX=" + std::to_wstring(splitterDragStartX) +
            L" splitterX=" + std::to_wstring(DashboardStateSourceSplitterX(window->m_dashboardState)) +
            L" previewX=" + std::to_wstring(DashboardStateSplitterDragPreviewX(window->m_dashboardState)));
    }
    if (captureHasTrackerStripe(firstPreviewSplitterX, captureOk) ||
        captureHasTrackerStripe(secondPreviewSplitterX, captureOk)) {
        return fail(L"Source Rail splitter left a tracker-colored stripe after drag commit.");
    }
    if (!captureOk) {
        return fail(L"Source Rail splitter pixel contract could not capture Dashboard client pixels after drag commit.");
    }

    int beforeResizeCancelSourceWidth = window->m_layout.sourceWidth;
    int resizeCancelStartX = DashboardStateSourceSplitterX(window->m_dashboardState) + window->m_metrics.splitterW / 2;
    int resizeCancelTargetX = resizeCancelStartX + max(24, window->Scale(48));
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(resizeCancelStartX, splitterDragY));
    if (!DashboardStateIsSplitterPressPending(window->m_dashboardState) || DashboardStateIsDraggingSplitter(window->m_dashboardState) || DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind != 1) {
        return fail(L"Source Rail splitter resize-cancel test did not enter pending press mode.");
    }
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(resizeCancelTargetX, splitterDragY));
    if (!DashboardStateIsDraggingSplitter(window->m_dashboardState) || DashboardStateIsSplitterPressPending(window->m_dashboardState)) {
        return fail(L"Source Rail splitter resize-cancel test did not cross drag slop.");
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 1320, 840,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (DashboardStateIsDraggingSplitter(window->m_dashboardState)) {
        SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, 0);
    }
    if (DashboardStateIsDraggingSplitter(window->m_dashboardState) ||
        DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind != 0 ||
        (window->m_splitterTracker && IsWindowVisible(window->m_splitterTracker)) ||
        window->m_layout.sourceWidth != beforeResizeCancelSourceWidth) {
        return fail(L"Source Rail splitter drag did not cancel cleanly when the window resized mid-drag.");
    }

    auto validateCommandBarGeometry = [&](const std::wstring& label) -> bool {
        if (!window->m_sourcePanelToggleBtn || !window->m_resultPanelToggleBtn ||
            !IsWindowVisible(window->m_sourcePanelToggleBtn) ||
            !IsWindowVisible(window->m_resultPanelToggleBtn) ||
            !window->m_importBtn || !IsWindowVisible(window->m_importBtn)) {
            return fail(label + L": persistent panel toggles or Import button became hidden.");
        }
        RECT resultToggleRc = {};
        GetWindowRect(window->m_resultPanelToggleBtn, &resultToggleRc);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&resultToggleRc), 2);
        const HWND leftCommands[] = {
            window->m_sourcePanelToggleBtn,
            window->m_importBtn,
            window->m_outputFolderBtn,
            window->m_dashboardOcrCombo,
            window->m_copyBtn,
            window->m_clearBtn,
            window->m_retryFailedBtn,
            window->m_pauseBatchBtn,
            window->m_openOutputBtn,
            window->m_statusText,
        };
        for (HWND command : leftCommands) {
            if (!command || !IsWindowVisible(command)) continue;
            RECT commandRc = {};
            GetWindowRect(command, &commandRc);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&commandRc), 2);
            if (commandRc.right > resultToggleRc.left - window->m_metrics.spacing) {
                wchar_t commandText[64] = {};
                GetWindowTextW(command, commandText, ARRAYSIZE(commandText));
                return fail(label + L": visible left command overlaps the Result mode group: " +
                    commandText + L" right=" + std::to_wstring(commandRc.right) +
                    L" resultLeft=" + std::to_wstring(resultToggleRc.left));
            }
        }
        RECT nextRecordRc = {};
        RECT languageRc = {};
        RECT minimizeRc = {};
        RECT maximizeRc = {};
        RECT closeRc = {};
        const bool nextRecordVisible =
            window->m_nextRecordBtn && IsWindowVisible(window->m_nextRecordBtn);
        if (nextRecordVisible) {
            GetWindowRect(window->m_nextRecordBtn, &nextRecordRc);
        }
        if (window->m_langToggleBtn && IsWindowVisible(window->m_langToggleBtn)) {
            GetWindowRect(window->m_langToggleBtn, &languageRc);
        }
        if (window->m_minimizeBtn && IsWindowVisible(window->m_minimizeBtn)) {
            GetWindowRect(window->m_minimizeBtn, &minimizeRc);
        }
        if (window->m_maximizeBtn && IsWindowVisible(window->m_maximizeBtn)) {
            GetWindowRect(window->m_maximizeBtn, &maximizeRc);
        }
        GetWindowRect(window->m_closeBtn, &closeRc);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&nextRecordRc), 2);
        if (languageRc.right > languageRc.left) {
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&languageRc), 2);
        }
        if (minimizeRc.right > minimizeRc.left) {
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&minimizeRc), 2);
        }
        if (maximizeRc.right > maximizeRc.left) {
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&maximizeRc), 2);
        }
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&closeRc), 2);
        if ((nextRecordVisible && nextRecordRc.right > (languageRc.right > languageRc.left
                ? languageRc.left
                : (minimizeRc.right > minimizeRc.left ? minimizeRc.left : maximizeRc.left)) - window->m_metrics.spacing) ||
            (languageRc.right > languageRc.left &&
                languageRc.right > (minimizeRc.right > minimizeRc.left
                    ? minimizeRc.left
                    : (maximizeRc.right > maximizeRc.left ? maximizeRc.left : closeRc.left)) - window->m_metrics.spacing) ||
            (minimizeRc.right > minimizeRc.left &&
                minimizeRc.right > (maximizeRc.right > maximizeRc.left ? maximizeRc.left : closeRc.left) - window->m_metrics.spacing) ||
            (maximizeRc.right > maximizeRc.left &&
                maximizeRc.right > closeRc.left - window->m_metrics.spacing)) {
            return fail(label + L": Result navigation and window control buttons overlap.");
        }
        return true;
    };

    auto validateWorkbenchGeometry = [&](const std::wstring& label) -> bool {
        RECT clientRc = {};
        GetClientRect(hwnd, &clientRc);
        if (clientRc.right <= 0 || clientRc.bottom <= 0) {
            return fail(label + L": Dashboard client rect is empty after resize.");
        }
        bool splitterInvalid =
            (window->m_resolvedLayout.sourceVisible && DashboardStateSourceSplitterX(window->m_dashboardState) <= 0) ||
            (window->m_resolvedLayout.resultVisible &&
                (DashboardStateResultSplitterX(window->m_dashboardState) <= 0 || DashboardStateResultSplitterX(window->m_dashboardState) >= clientRc.right)) ||
            (window->m_resolvedLayout.sourceVisible && window->m_resolvedLayout.resultVisible &&
                DashboardStateResultSplitterX(window->m_dashboardState) <= DashboardStateSourceSplitterX(window->m_dashboardState));
        if (splitterInvalid) {
            return fail(label + L": Dashboard splitter positions are not ordered inside the client rect: source=" +
                std::to_wstring(DashboardStateSourceSplitterX(window->m_dashboardState)) +
                L", result=" + std::to_wstring(DashboardStateResultSplitterX(window->m_dashboardState)) +
                L", client=" + std::to_wstring(clientRc.right) + L"x" +
                std::to_wstring(clientRc.bottom) +
                L", sourceVisible=" + std::to_wstring(window->m_resolvedLayout.sourceVisible ? 1 : 0) +
                L", resultVisible=" + std::to_wstring(window->m_resolvedLayout.resultVisible ? 1 : 0) + L".");
        }
        if (!window->m_sourceList || !IsWindow(window->m_sourceList)) {
            return fail(label + L": Source Rail window is missing after resize.");
        }
        LONG_PTR sourceStyle = GetWindowLongPtrW(window->m_sourceList, GWL_STYLE);
        bool sourceWindowVisible = (sourceStyle & WS_VISIBLE) != 0;
        if (sourceWindowVisible != window->m_resolvedLayout.sourceVisible) {
            return fail(label + L": Source Rail HWND visibility disagrees with responsive layout state.");
        }
        if (!window->m_resolvedLayout.sourceVisible) return validateCommandBarGeometry(label);
        RECT sourceListRc = {};
        GetWindowRect(window->m_sourceList, &sourceListRc);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&sourceListRc), 2);
        int sourceListW = sourceListRc.right - sourceListRc.left;
        int sourceListH = sourceListRc.bottom - sourceListRc.top;
        if (sourceListW <= 0 ||
            sourceListH <= 0 ||
            sourceListRc.left < 0 ||
            sourceListRc.right > clientRc.right ||
            sourceListRc.bottom > clientRc.bottom) {
            return fail(label + L": Source Rail client bounds escaped the Dashboard after resize.");
        }
        HDC railDc = GetDC(window->m_sourceList);
        RECT sourceClientForBuffer = {};
        GetClientRect(window->m_sourceList, &sourceClientForBuffer);
        int sourceBufferW = max(1, sourceClientForBuffer.right - sourceClientForBuffer.left);
        int sourceBufferH = max(1, sourceClientForBuffer.bottom - sourceClientForBuffer.top);
        bool backbufferOk = window->EnsureSourceRailBackbuffer(
            railDc,
            sourceBufferW,
            sourceBufferH);
        if (railDc) ReleaseDC(window->m_sourceList, railDc);
        if (!backbufferOk ||
            window->m_sourceRailBufferW != sourceBufferW ||
            window->m_sourceRailBufferH != sourceBufferH) {
            return fail(label + L": Source Rail backbuffer did not match the resized bounds.");
        }
        if (DashboardStateIsDraggingSplitter(window->m_dashboardState) ||
            DashboardStateIsDraggingSplitter(window->m_dashboardState)Kind != 0 ||
            (window->m_splitterTracker && IsWindowVisible(window->m_splitterTracker))) {
            return fail(label + L": Splitter tracker state leaked after resize.");
        }
        return validateCommandBarGeometry(label);
    };

    const SIZE rapidResizeSizes[] = {
        { 1280, 760 },
        { 1500, 900 },
        { 1200, 780 },
        { 1680, 1040 },
        { 1320, 840 },
    };
    for (const SIZE& rapidSize : rapidResizeSizes) {
        SetWindowPos(hwnd, nullptr, 0, 0, rapidSize.cx, rapidSize.cy,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        window->LayoutControls();
        window->UpdateSourceRailScrollInfo();
        if (!validateWorkbenchGeometry(
                L"Source Rail rapid resize " +
                std::to_wstring(rapidSize.cx) +
                L"x" +
                std::to_wstring(rapidSize.cy))) {
            return false;
        }
    }

    SetWindowPos(hwnd, nullptr, 0, 0, window->m_metrics.minTrackW, window->m_metrics.minTrackH,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->LayoutControls();
    if (!validateCommandBarGeometry(L"Dashboard minimum-width command bar")) return false;
    SetWindowPos(hwnd, nullptr, 0, 0, 1320, 840,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->LayoutControls();

    UINT dpiBeforeRuntimeChange = window->m_dpi;
    UINT dpiDuringRuntimeChange = dpiBeforeRuntimeChange == 96 ? 144 : 96;
    int sourceWidthBeforeDpiChange = window->m_layout.sourceWidth;
    int resultWidthBeforeDpiChange = window->m_layout.resultWidth;
    RECT dpiSuggestedRc = {};
    GetWindowRect(hwnd, &dpiSuggestedRc);
    SendMessageW(hwnd, WM_DPICHANGED,
        MAKEWPARAM(dpiDuringRuntimeChange, dpiDuringRuntimeChange),
        reinterpret_cast<LPARAM>(&dpiSuggestedRc));
    int expectedSourceWidthAtChangedDpi = max(1,
        MulDiv(sourceWidthBeforeDpiChange, dpiDuringRuntimeChange, dpiBeforeRuntimeChange));
    int expectedResultWidthAtChangedDpi = max(1,
        MulDiv(resultWidthBeforeDpiChange, dpiDuringRuntimeChange, dpiBeforeRuntimeChange));
    if (window->m_dpi != dpiDuringRuntimeChange ||
        window->m_layout.sourceWidth != expectedSourceWidthAtChangedDpi ||
        window->m_layout.resultWidth != expectedResultWidthAtChangedDpi) {
        return fail(L"Dashboard runtime DPI change did not scale persisted pane widths with the metrics.");
    }
    GetWindowRect(hwnd, &dpiSuggestedRc);
    SendMessageW(hwnd, WM_DPICHANGED,
        MAKEWPARAM(dpiBeforeRuntimeChange, dpiBeforeRuntimeChange),
        reinterpret_cast<LPARAM>(&dpiSuggestedRc));
    const UINT sourceRailDpiMatrix[] = { 96, 120, 144, 168, 192 };
    for (UINT matrixDpi : sourceRailDpiMatrix) {
        UINT previousDpi = window->m_dpi;
        int previousSourceWidth = window->m_layout.sourceWidth;
        int previousResultWidth = window->m_layout.resultWidth;
        GetWindowRect(hwnd, &dpiSuggestedRc);
        SendMessageW(hwnd, WM_DPICHANGED,
            MAKEWPARAM(matrixDpi, matrixDpi),
            reinterpret_cast<LPARAM>(&dpiSuggestedRc));
        if (window->m_dpi != matrixDpi ||
            window->m_layout.sourceWidth != max(1, MulDiv(previousSourceWidth, matrixDpi, previousDpi)) ||
            window->m_layout.resultWidth != max(1, MulDiv(previousResultWidth, matrixDpi, previousDpi)) ||
            window->m_metrics.batchTaskItemH != max(1, MulDiv(84, matrixDpi, kDashboardDesignDpi)) ||
            window->m_metrics.sourceListItemH != window->m_metrics.batchTaskItemH ||
            window->m_metrics.pdfPageItemH != max(1, MulDiv(40, matrixDpi, kDashboardDesignDpi))) {
            return fail(L"Source Rail 100%/125%/150%/175%/200% DPI matrix did not preserve scaled root/Page geometry.");
        }
        const RECT dpiRootRc = {
            0,
            0,
            window->m_metrics.sourceMinW,
            window->m_metrics.sourceListItemH
        };
        const RECT dpiThumbRc = window->GetSourceRailThumbnailRect(dpiRootRc);
        const RECT dpiDisclosureRc = window->GetSourceRailPdfDisclosureRect(dpiRootRc);
        const int expectedDisclosureSize = min(
            max(18, window->Scale(24)),
            max(1, min(
                dpiThumbRc.right - dpiThumbRc.left,
                dpiThumbRc.bottom - dpiThumbRc.top) - max(2, window->Scale(3)) * 2));
        if (dpiDisclosureRc.right - dpiDisclosureRc.left != expectedDisclosureSize ||
            dpiDisclosureRc.bottom - dpiDisclosureRc.top != expectedDisclosureSize ||
            dpiDisclosureRc.left < dpiThumbRc.left ||
            dpiDisclosureRc.top < dpiThumbRc.top ||
            dpiDisclosureRc.right > dpiThumbRc.right ||
            dpiDisclosureRc.bottom > dpiThumbRc.bottom) {
            return fail(L"Source Rail PDF thumbnail disclosure did not remain inside the cover across the DPI matrix.");
        }
        if (!window->m_sourceTitleFontMetrics.IsUsable() ||
            !window->m_sourceMetaFontMetrics.IsUsable() ||
            !window->m_uiFontMetrics.IsUsable() ||
            !window->m_editFontMetrics.IsUsable()) {
            return fail(L"Dashboard font-metrics cache was not rebuilt for a DPI transition.");
        }

        const int rootH = window->m_metrics.sourceListItemH;
        const int titleH = window->m_metrics.sourceTitleLineH;
        const int metaH = window->m_metrics.sourceMetaLineH;
        const int titleGap = window->m_metrics.sourceTitleToMetaGap;
        const int metaGap = window->m_metrics.sourceMetaLineGap;
        const int textBlockH = titleH + titleGap + metaH + metaGap + metaH;
        const int textTop = max(window->m_metrics.sourceItemPadY, (rootH - textBlockH) / 2);
        const auto measuredLineFitsRoot = [rootH](int nominalTop, int nominalH, int measuredH) {
            const int drawH = max(nominalH, measuredH);
            const int drawTop = (nominalTop + nominalTop + nominalH - drawH) / 2;
            return drawTop >= 0 && drawTop + drawH <= rootH;
        };
        const int statusTop = textTop + titleH + titleGap;
        const int metaTop = statusTop + metaH + metaGap;
        if (!measuredLineFitsRoot(textTop, titleH, window->m_sourceTitleFontMetrics.height) ||
            !measuredLineFitsRoot(statusTop, metaH, window->m_sourceMetaFontMetrics.height) ||
            !measuredLineFitsRoot(metaTop, metaH, window->m_sourceMetaFontMetrics.height)) {
            return fail(L"Source Rail measured text cells do not fit inside an 84-design-pixel root card at the active DPI.");
        }
        window->LayoutControls();
        SendMessageW(window->m_sourceList, WM_PAINT, 0, 0);
    }
    if (window->m_dpi != dpiBeforeRuntimeChange) {
        UINT previousDpi = window->m_dpi;
        int previousSourceWidth = window->m_layout.sourceWidth;
        int previousResultWidth = window->m_layout.resultWidth;
        GetWindowRect(hwnd, &dpiSuggestedRc);
        SendMessageW(hwnd, WM_DPICHANGED,
            MAKEWPARAM(dpiBeforeRuntimeChange, dpiBeforeRuntimeChange),
            reinterpret_cast<LPARAM>(&dpiSuggestedRc));
        if (window->m_layout.sourceWidth !=
                max(1, MulDiv(previousSourceWidth, dpiBeforeRuntimeChange, previousDpi)) ||
            window->m_layout.resultWidth !=
                max(1, MulDiv(previousResultWidth, dpiBeforeRuntimeChange, previousDpi))) {
            return fail(L"Source Rail DPI matrix did not restore the original pane scale.");
        }
    }
    window->m_responsiveLayout = {};
    SetWindowPos(hwnd, nullptr, 0, 0, 1320, 840,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->LayoutControls();

    // Zero-width explicit hide and stable toolbar recovery contract.
    int savedSourceWidthForPaneContract = window->m_layout.sourceWidth;
    int savedResultWidthForPaneContract = window->m_layout.resultWidth;
    window->m_layout.sourceVisible = false;
    window->LayoutControls();
    if (window->m_resolvedLayout.sourceVisible || DashboardStateSourceSplitterX(window->m_dashboardState) != 0 ||
        window->m_resolvedLayout.sourceRc.right != window->m_resolvedLayout.sourceRc.left ||
        window->m_resolvedLayout.sourceSplitterRc.right != window->m_resolvedLayout.sourceSplitterRc.left ||
        IsWindowVisible(window->m_searchEdit) || IsWindowVisible(window->m_sourceList) ||
        window->m_resolvedLayout.canvasRc.left != window->m_metrics.margin) {
        return fail(L"Explicit Source hide retained pane geometry, splitter hit area, or child visibility.");
    }
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_DASH_SOURCE_PANEL_TOGGLE, BN_CLICKED),
        reinterpret_cast<LPARAM>(window->m_sourcePanelToggleBtn));
    if (!window->m_layout.sourceVisible || !window->m_resolvedLayout.sourceVisible ||
        window->m_layout.sourceWidth != savedSourceWidthForPaneContract) {
        return fail(L"Source panel toggle did not restore the persisted expanded width.");
    }

    int sourceDoubleClickX = DashboardStateSourceSplitterX(window->m_dashboardState) + window->m_metrics.splitterW / 2;
    bool sourceIntentBeforeDoubleClick = window->m_layout.sourceVisible;
    window->m_layout.sourceWidth = window->m_metrics.sourceMinW;
    window->LayoutControls();
    sourceDoubleClickX = DashboardStateSourceSplitterX(window->m_dashboardState) + window->m_metrics.splitterW / 2;
    SendMessageW(hwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(sourceDoubleClickX, splitterDragY));
    if (window->m_layout.sourceVisible != sourceIntentBeforeDoubleClick ||
        window->m_layout.sourceWidth < window->m_metrics.sourceW ||
        DashboardStateIsDraggingSplitter(window->m_dashboardState) || DashboardStateIsSplitterPressPending(window->m_dashboardState)) {
        return fail(L"Source splitter double-click changed visibility or failed to restore the design width.");
    }

    int minDragStartX = DashboardStateSourceSplitterX(window->m_dashboardState) + window->m_metrics.splitterW / 2;
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(minDragStartX, splitterDragY));
    SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON,
        MAKELPARAM(window->m_metrics.margin, splitterDragY));
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
        MAKELPARAM(window->m_metrics.margin, splitterDragY));
    if (!window->m_layout.sourceVisible || window->m_layout.sourceWidth != window->m_metrics.sourceMinW) {
        return fail(L"Dragging Source splitter to its minimum hid the pane instead of clamping its width.");
    }

    window->m_layout.sourceVisible = false;
    window->m_layout.resultVisible = false;
    window->LayoutControls();
    RECT bothHiddenClient = {};
    GetClientRect(hwnd, &bothHiddenClient);
    if (DashboardStateSourceSplitterX(window->m_dashboardState) != 0 || DashboardStateResultSplitterX(window->m_dashboardState) != 0 ||
        window->m_resolvedLayout.canvasRc.left != window->m_metrics.margin ||
        window->m_resolvedLayout.canvasRc.right != bothHiddenClient.right - window->m_metrics.margin) {
        return fail(L"Hiding both side panes did not expand Canvas across the full main workspace.");
    }
    window->m_layout.sourceVisible = true;
    window->m_layout.resultVisible = true;
    window->m_layout.sourceWidth = savedSourceWidthForPaneContract;
    window->m_layout.resultWidth = savedResultWidthForPaneContract;
    window->m_responsiveLayout = {};
    window->LayoutControls();

    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(hwnd, SW_HIDE);

    resetWindowState();
    std::wstring cacheDir = GetOcrImageDir();
    if (!BatchOcrWriter::EnsureDirectory(cacheDir)) {
        return fail(L"Failed to prepare OCR image cache directory for Source Rail delete contract.");
    }
    wchar_t deleteSuffix[128] = {};
    swprintf_s(deleteSuffix, L"%lu_%llu",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()));
    std::wstring uniqueCacheImage = cacheDir + L"dashboard_delete_unique_" + deleteSuffix + L".png";
    std::wstring sharedCacheImage = cacheDir + L"dashboard_delete_shared_" + deleteSuffix + L".png";
    std::wstring externalImage = runRoot + L"\\dashboard_delete_external_" + deleteSuffix + L".png";
    if (!PathFileExistsW(sourcePageImage.c_str()) ||
        !CopyFileW(sourcePageImage.c_str(), uniqueCacheImage.c_str(), FALSE) ||
        !CopyFileW(sourcePageImage.c_str(), sharedCacheImage.c_str(), FALSE) ||
        !CopyFileW(sourcePageImage.c_str(), externalImage.c_str(), FALSE)) {
        return fail(L"Failed to prepare Source Rail delete contract image files.");
    }

    OcrDashboardHistoryItem deleteUnique;
    deleteUnique.timestamp = L"2026-07-03 01:00:00";
    deleteUnique.imagePath = uniqueCacheImage;
    deleteUnique.text = L"delete unique cache";
    OcrDashboardHistoryItem deleteExternal;
    deleteExternal.timestamp = L"2026-07-03 01:01:00";
    deleteExternal.imagePath = externalImage;
    deleteExternal.text = L"delete external original";
    OcrDashboardHistoryItem deleteSharedA;
    deleteSharedA.timestamp = L"2026-07-03 01:02:00";
    deleteSharedA.imagePath = sharedCacheImage;
    deleteSharedA.text = L"delete shared cache A";
    OcrDashboardHistoryItem deleteSharedB;
    deleteSharedB.timestamp = L"2026-07-03 01:03:00";
    deleteSharedB.imagePath = sharedCacheImage;
    deleteSharedB.text = L"keep shared cache B";
    window->AddHistoryItem(deleteUnique);
    window->AddHistoryItem(deleteExternal);
    window->AddHistoryItem(deleteSharedA);
    window->AddHistoryItem(deleteSharedB);
    window->SetSourceSelectionIndices({0, 1, 2});
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    window->m_testAutoConfirmDelete = true;
#endif
    if (!window->HandleSourceRailKey(VK_DELETE, false, false)) {
        return fail(L"Source Rail Delete key was not handled.");
    }
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    window->m_testAutoConfirmDelete = false;
#endif
    if (window->m_history.model.items.size() != 1 ||
        window->m_history.model.items.front().text != L"keep shared cache B") {
        return fail(L"Source Rail Delete key did not remove the selected history rows.");
    }
    if (PathFileExistsW(uniqueCacheImage.c_str())) {
        return fail(L"Source Rail Delete key did not remove the unreferenced OCR cache image.");
    }
    if (!PathFileExistsW(externalImage.c_str())) {
        return fail(L"Source Rail Delete key removed an external source image.");
    }
    if (!PathFileExistsW(sharedCacheImage.c_str())) {
        return fail(L"Source Rail Delete key removed a cache image still referenced by remaining history.");
    }

    {
        OcrDashboardWindow* cancelWindow = new OcrDashboardWindow();
        s_instance = cancelWindow;
        if (!cancelWindow->Create(nullptr)) {
            s_instance = window;
            delete cancelWindow;
            return fail(L"Failed to create Dashboard window for deferred close contract.");
        }
        HWND cancelHwnd = cancelWindow->m_hwnd;
        ShowWindow(cancelHwnd, SW_HIDE);

        auto failCancelWindow = [&](const std::wstring& message) {
            if (IsWindow(cancelHwnd)) {
                DestroyWindow(cancelHwnd);
            }
            s_instance = window;
            return fail(message);
        };

        std::wstring cancelRoot = runRoot + L"_cancel_close";
        if (!BatchOcrWriter::EnsureDirectory(cancelRoot)) {
            return failCancelWindow(L"Failed to create deferred close contract output root.");
        }
        BatchOcrPdfJob cancelPdfJob;
        if (!controller.CreatePdfJob(resolvedPdfPath, cancelRoot, cancelPdfJob, setupError)) {
            return failCancelWindow(setupError.empty()
                ? L"Failed to create deferred close PDF job."
                : setupError);
        }
        cancelPdfJob.pageRange = L"1";
        cancelPdfJob.pdfRenderDpi = 144;

        uint64_t renderGeneration = DashboardStateOcrGeneration(cancelWindow->m_dashboardState);
        DashboardStateApplyBatchOutputRoots(
            cancelWindow->m_dashboardState,
            DashboardStatePreferredBatchOutputRoot(cancelWindow->m_dashboardState),
            cancelRoot,
            DashboardStateRecentBatchOutputRoots(cancelWindow->m_dashboardState));
        cancelWindow->m_batch.activePdfJobs.push_back(cancelPdfJob);
        DashboardStateSyncBatchProgress(cancelWindow->m_dashboardState, DashboardStateIsCancelBatchRequested(cancelWindow->m_dashboardState), 1, 0, 1);
        cancelWindow->UpdateCloseCancelButtonText();

        SendMessageW(
            cancelHwnd,
            WM_COMMAND,
            MAKEWPARAM(ID_DASH_CLOSE, BN_CLICKED),
            reinterpret_cast<LPARAM>(cancelWindow->m_closeBtn));
        if (!IsWindow(cancelHwnd)) {
            s_instance = window;
            return fail(L"Deferred close destroyed the Dashboard before PDF render callback settled.");
        }
        if (!cancelWindow->m_closeAfterCancel ||
            !DashboardStateIsCancelBatchRequested(cancelWindow->m_dashboardState) ||
            DashboardStatePdfRenderInFlight(cancelWindow->m_dashboardState) != 1) {
            return failCancelWindow(L"Deferred close did not enter cancel-wait state for an in-flight PDF render.");
        }
        std::wstring cancelManifest;
        if (!DashboardWindowTestReadUtf8File(cancelPdfJob.manifestPath, cancelManifest) ||
            !DashboardWindowTestContains(cancelManifest, L"\"status\": \"canceled\"")) {
            return failCancelWindow(L"Deferred close did not write a canceled PDF manifest while render was in-flight.");
        }

        auto* lateRender = new DashboardPdfRenderResult();
        lateRender->generation = renderGeneration;
        lateRender->pdfJob = cancelPdfJob;
        lateRender->render.success = false;
        lateRender->render.error = L"deferred close render completion";
        cancelWindow->HandlePdfRenderComplete(lateRender);

        if (!DashboardWindowTestPumpUntilDestroyed(cancelHwnd, 3000)) {
            return failCancelWindow(L"Deferred close did not destroy the Dashboard after PDF render callback settled.");
        }
        s_instance = window;

        std::wstring finalCancelManifest;
        if (!DashboardWindowTestReadUtf8File(cancelPdfJob.manifestPath, finalCancelManifest) ||
            !DashboardWindowTestContains(finalCancelManifest, L"\"status\": \"canceled\"")) {
            return fail(L"Deferred close did not preserve the canceled PDF manifest after closing.");
        }
    }

    {
        OcrDashboardWindow* queuedCloseWindow = new OcrDashboardWindow();
        s_instance = queuedCloseWindow;
        if (!queuedCloseWindow->Create(nullptr)) {
            s_instance = window;
            delete queuedCloseWindow;
            return fail(L"Failed to create Dashboard window for queued cover shutdown contract.");
        }
        HWND queuedCloseHwnd = queuedCloseWindow->m_hwnd;
        ShowWindow(queuedCloseHwnd, SW_HIDE);
        std::wstring queuedCandidate = JoinPathWide(
            runRoot,
            L"thumbnail.g" + std::to_wstring(DashboardStateOcrGeneration(queuedCloseWindow->m_dashboardState)) +
                L".queued-close.candidate.png");
        if (!CopyFileW(sourcePageImage.c_str(), queuedCandidate.c_str(), FALSE)) {
            DestroyWindow(queuedCloseHwnd);
            s_instance = window;
            return fail(L"Failed to prepare queued cover shutdown candidate.");
        }
        auto* queuedCover = new DashboardPdfCoverResult();
        queuedCover->generation = DashboardStateOcrGeneration(queuedCloseWindow->m_dashboardState);
        queuedCover->outputDir = runRoot;
        queuedCover->candidatePath = queuedCandidate;
        queuedCover->render.success = true;
        queuedCover->render.candidatePath = queuedCandidate;
        if (!PostDashboardAsyncMessage(
                queuedCloseWindow->m_asyncDispatchState,
                WM_DASHBOARD_PDF_COVER_COMPLETE,
                0,
                reinterpret_cast<LPARAM>(queuedCover))) {
            delete queuedCover;
            DeleteFileW(queuedCandidate.c_str());
            DestroyWindow(queuedCloseHwnd);
            s_instance = window;
            return fail(L"Failed to enqueue PDF cover completion before Dashboard shutdown.");
        }
        DestroyWindow(queuedCloseHwnd);
        s_instance = window;
        if (PathFileExistsW(queuedCandidate.c_str())) {
            return fail(L"Dashboard shutdown did not drain queued cover payload/candidate ownership.");
        }
    }

    // Warm the one-time Win32/OLE registrations, then repeatedly construct and
    // destroy complete Dashboard windows.  Resource counts are sampled only
    // after warmup so a process-global lazy cache cannot hide a per-window
    // GDI/USER handle or worker-thread leak.
    auto createAndDestroyResourceProbe = [&]() {
        OcrDashboardWindow* probe = new OcrDashboardWindow();
        s_instance = probe;
        if (!probe->Create(nullptr)) {
            s_instance = nullptr;
            delete probe;
            s_instance = window;
            return false;
        }
        HWND probeHwnd = probe->m_hwnd;
        ShowWindow(probeHwnd, SW_HIDE);
        DestroyWindow(probeHwnd);
        s_instance = window;

        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return !IsWindow(probeHwnd);
    };
    if (!createAndDestroyResourceProbe()) {
        return fail(L"Dashboard resource warmup window did not close cleanly.");
    }

    HANDLE currentProcess = GetCurrentProcess();
    const DWORD gdiBefore = GetGuiResources(currentProcess, GR_GDIOBJECTS);
    const DWORD userBefore = GetGuiResources(currentProcess, GR_USEROBJECTS);
    const DWORD threadsBefore = DashboardWindowTestCurrentProcessThreadCount();
    for (int iteration = 0; iteration < 8; ++iteration) {
        if (!createAndDestroyResourceProbe()) {
            return fail(L"Dashboard repeated resource-probe window did not close cleanly.");
        }
    }
    const DWORD gdiAfter = GetGuiResources(currentProcess, GR_GDIOBJECTS);
    const DWORD userAfter = GetGuiResources(currentProcess, GR_USEROBJECTS);
    const DWORD threadsAfter = DashboardWindowTestCurrentProcessThreadCount();
    wprintf(
        L"Dashboard 8x open/close resources: GDI %lu->%lu USER %lu->%lu threads %lu->%lu\n",
        static_cast<unsigned long>(gdiBefore),
        static_cast<unsigned long>(gdiAfter),
        static_cast<unsigned long>(userBefore),
        static_cast<unsigned long>(userAfter),
        static_cast<unsigned long>(threadsBefore),
        static_cast<unsigned long>(threadsAfter));
    if (!gdiBefore || !userBefore || !threadsBefore ||
        gdiAfter > gdiBefore + 2 ||
        userAfter > userBefore + 2 ||
        threadsAfter > threadsBefore) {
        return fail(
            L"Dashboard repeated open/close leaked process resources: GDI " +
            std::to_wstring(gdiBefore) + L"->" + std::to_wstring(gdiAfter) +
            L", USER " + std::to_wstring(userBefore) + L"->" + std::to_wstring(userAfter) +
            L", threads " + std::to_wstring(threadsBefore) + L"->" + std::to_wstring(threadsAfter) + L".");
    }

    resetWindowState();
    std::wstring positionPath = testPositionPath;
    UINT currentDpi = window->m_dpi > 0 ? window->m_dpi : kDashboardDesignDpi;
    UINT savedDpi = currentDpi == 96 ? kDashboardDesignDpi : 96;
    int savedSourceWidth = 240;
    int savedResultWidth = 360;
    WritePrivateProfileStringW(L"Window", L"Position", L"80,80,900,640,1", positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"Dpi", std::to_wstring(savedDpi).c_str(), positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"SourceWidth", std::to_wstring(savedSourceWidth).c_str(), positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ResultWidth", std::to_wstring(savedResultWidth).c_str(), positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"SourceVisible", nullptr, positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ResultVisible", nullptr, positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"SourceCollapsed", L"1", positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ResultCollapsed", L"0", positionPath.c_str());

    window->m_layout.sourceWidth = 1;
    window->m_layout.resultWidth = 1;
    window->m_layout.sourceVisible = true;
    window->m_layout.resultVisible = false;
    bool restoredMaximized = window->RestoreWindowPosition();
    int expectedSourceWidth = max(window->m_metrics.sourceMinW,
        MulDiv(savedSourceWidth, (int)currentDpi, (int)savedDpi));
    int expectedResultWidth = max(window->m_metrics.resultMinW,
        MulDiv(savedResultWidth, (int)currentDpi, (int)savedDpi));
    if (!restoredMaximized ||
        window->m_layout.sourceWidth != expectedSourceWidth ||
        window->m_layout.resultWidth != expectedResultWidth ||
        window->m_layout.sourceVisible ||
        !window->m_layout.resultVisible) {
        return fail(L"Dashboard window restore did not migrate DPI-scaled layout and legacy collapsed state.");
    }
    WritePrivateProfileStringW(L"Window", L"SourceVisible", L"1", positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ResultVisible", L"0", positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"SourceCollapsed", L"1", positionPath.c_str());
    WritePrivateProfileStringW(L"Window", L"ResultCollapsed", L"0", positionPath.c_str());
    window->RestoreWindowPosition();
    if (!window->m_layout.sourceVisible || window->m_layout.resultVisible) {
        return fail(L"Dashboard window restore did not prefer new visible keys over legacy collapsed keys.");
    }
    window->LayoutControls();
    if (!window->m_closeBtn || !IsWindow(window->m_closeBtn)) {
        return fail(L"Dashboard frameless Close button is missing.");
    }

    HWND closeTestHwnd = hwnd;
    HWND closeButton = window->m_closeBtn;
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_DASH_CLOSE, BN_CLICKED), reinterpret_cast<LPARAM>(closeButton));
    DashboardWindowTestClearOverrides();
    DeleteFileW(testPositionPath.c_str());
    DeleteFileW(testDismissedPath.c_str());
    if (IsWindow(closeTestHwnd)) {
        DashboardWindowTestSetError(error, L"Dashboard Close button command did not close the window.");
        DestroyWindow(closeTestHwnd);
        return false;
    }
    return true;
}

bool OcrDashboardWindow::RunRuntimeContractForTests(
    const std::wstring& outputRoot,
    std::wstring& error)
{
    error.clear();

    // Dashboard persists the user-facing "paddle_local" mode and lets the
    // enableDocParsing snapshot select the document engine.  Probe the same
    // route that the queued task will actually use; Create("paddle_local_doc")
    // is not a Dashboard mode and historically fell through to Windows OCR.
    auto docEngine = OcrEngineFactory::Create(L"paddle_local");
    if (!docEngine || !docEngine->IsAvailable()) {
        error = L"SKIP: PaddleOCR-VL 1.6 Local document parsing engine is unavailable.";
        return true;
    }

    wchar_t fullOutputRoot[MAX_PATH] = {};
    DWORD fullLen = GetFullPathNameW(outputRoot.c_str(), MAX_PATH, fullOutputRoot, nullptr);
    std::wstring runRoot = (fullLen > 0 && fullLen < MAX_PATH) ? std::wstring(fullOutputRoot) : outputRoot;
    if (!runRoot.empty() && (runRoot.back() == L'\\' || runRoot.back() == L'/')) {
        runRoot.pop_back();
    }
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        error = L"Failed to create Dashboard runtime contract base output root.";
        return false;
    }
    wchar_t suffix[128] = {};
    swprintf_s(suffix, L"\\dashboard_runtime_contract_%lu_%llu",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()));
    runRoot += suffix;
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        error = L"Failed to create Dashboard runtime contract output root.";
        return false;
    }

    std::wstring sourceImagePath = runRoot + L"\\local_ocr_source.png";
    HBITMAP textBitmap = DashboardWindowTestCreateTextBitmap();
    if (!textBitmap) {
        error = L"Failed to create Dashboard runtime OCR bitmap.";
        return false;
    }
    bool saved = DashboardWindowTestSaveBitmapAsPng(textBitmap, sourceImagePath);
    DeleteObject(textBitmap);
    if (!saved || !PathFileExistsW(sourceImagePath.c_str())) {
        error = L"Failed to save Dashboard runtime OCR image.";
        return false;
    }

    std::wstring testPositionPath = GetWindowPositionFilePath();
    std::wstring testDismissedPath = DashboardWindowTestDismissedFilePath();
    DeleteFileW(testPositionPath.c_str());
    DeleteFileW(testDismissedPath.c_str());

    OcrDashboardWindow* window = new OcrDashboardWindow();
    s_instance = window;
    if (!window->Create(nullptr)) {
        s_instance = nullptr;
        delete window;
        DeleteFileW(testPositionPath.c_str());
        DeleteFileW(testDismissedPath.c_str());
        error = L"Failed to create Dashboard runtime window.";
        return false;
    }
    HWND hwnd = window->m_hwnd;
    ShowWindow(hwnd, SW_HIDE);
    window->SetDashboardOcrMode(L"paddle_local", false);

    auto fail = [&](const std::wstring& message) {
        DashboardWindowTestSetError(error, message);
        DeleteFileW(testPositionPath.c_str());
        DeleteFileW(testDismissedPath.c_str());
        if (IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
        return false;
    };

    size_t historyCountBeforeOcr = window->m_history.model.items.size();
    size_t taskCountBeforeOcr = window->m_batch.batchTasks.size();
    int dropDoneBeforeOcr = DashboardStateDropDone(window->m_dashboardState);
    window->QueueDroppedFile(sourceImagePath, nullptr);
    const size_t runtimeTaskIndex = window->m_batch.batchTasks.size() > taskCountBeforeOcr
        ? window->m_batch.batchTasks.size() - 1
        : static_cast<size_t>(-1);
    auto runtimeTaskIsTerminal = [&]() {
        if (runtimeTaskIndex >= window->m_batch.batchTasks.size()) return false;
        const BatchOcrTaskStatus status = window->m_batch.batchTasks[runtimeTaskIndex].status;
        return status == BatchOcrTaskStatus::Completed ||
            status == BatchOcrTaskStatus::Failed ||
            status == BatchOcrTaskStatus::Canceled;
    };
    bool ocrFinished = DashboardWindowTestPumpUntil(hwnd, 300000, [&]() {
        return !DashboardStateIsOcrBusy(window->m_dashboardState) &&
            window->m_batch.dropQueue.empty() &&
            (window->m_history.model.items.size() > historyCountBeforeOcr ||
             DashboardStateDropDone(window->m_dashboardState) > dropDoneBeforeOcr ||
             runtimeTaskIsTerminal());
    });
    if (!ocrFinished) {
        return fail(
            L"Dashboard runtime PaddleOCR-VL 1.6 Local document parsing did not complete. busy=" +
            std::to_wstring(DashboardStateIsOcrBusy(window->m_dashboardState) ? 1 : 0) +
            L" queue=" + std::to_wstring(window->m_batch.dropQueue.size()) +
            L" done=" + std::to_wstring(DashboardStateDropDone(window->m_dashboardState)) +
            L" history=" + std::to_wstring(window->m_history.model.items.size()) +
            L" mode=" + window->GetDashboardOcrMode());
    }
    if (runtimeTaskIndex < window->m_batch.batchTasks.size()) {
        const DashboardBatchTaskItem& runtimeTask = window->m_batch.batchTasks[runtimeTaskIndex];
        if (runtimeTask.status == BatchOcrTaskStatus::Failed ||
            runtimeTask.status == BatchOcrTaskStatus::Canceled) {
            return fail(
                L"Dashboard runtime PaddleOCR-VL 1.6 Local document parsing task failed: " +
                (runtimeTask.error.empty() ? L"<no task error>" : runtimeTask.error));
        }
    }
    if (window->m_history.model.items.size() <= historyCountBeforeOcr ||
        window->m_history.model.items.back().text.empty() ||
        !DashboardWindowTestLooksLikeExpectedOcr(window->m_history.model.items.back().text)) {
        std::wstring actual = window->m_history.model.items.size() <= historyCountBeforeOcr
            ? L"<no new history>"
            : window->m_history.model.items.back().text;
        return fail(L"Dashboard runtime PaddleOCR-VL 1.6 Local document parsing returned unexpected or empty history text: " + actual);
    }
    if (DashboardStateSelectedHistoryIndex(window->m_dashboardState) < 0 ||
        DashboardStateSelectedHistoryIndex(window->m_dashboardState) >= (int)window->m_history.model.items.size()) {
        return fail(L"Dashboard runtime OCR did not select the recognized history item.");
    }
    if (!window->m_gdiplusImage) {
        return fail(L"Dashboard runtime OCR did not load the recognized image into Canvas.");
    }

    window->SetTextMode(DashboardTextMode::Source);
    if (window->GetCurrentResultText().empty()) {
        return fail(L"Dashboard runtime Source mode lost the OCR result.");
    }
    window->SetTextMode(DashboardTextMode::Text);
    if (window->GetCurrentResultText().empty()) {
        return fail(L"Dashboard runtime Text mode lost the OCR result.");
    }
    window->SetTextMode(DashboardTextMode::Json);
    if (window->GetCurrentResultText().find(L"elapsedMs") == std::wstring::npos) {
        return fail(L"Dashboard runtime JSON mode did not expose OCR metadata.");
    }

    auto samePathNoCase = [](const std::wstring& left, const std::wstring& right) {
        return !left.empty() && !right.empty() &&
            _wcsicmp(left.c_str(), right.c_str()) == 0;
    };
    auto findActivePdfJobIndex = [&](const BatchOcrPdfJob& expected) {
        for (size_t i = 0; i < window->m_batch.activePdfJobs.size(); i++) {
            const BatchOcrPdfJob& active = window->m_batch.activePdfJobs[i];
            if (samePathNoCase(active.outputDir, expected.outputDir) ||
                samePathNoCase(active.manifestPath, expected.manifestPath) ||
                (samePathNoCase(active.sourcePath, expected.sourcePath) &&
                    samePathNoCase(active.outputRoot, expected.outputRoot))) {
                return (int)i;
            }
        }
        return -1;
    };
    auto pdfPagesTerminal = [](const BatchOcrPdfJob& job) {
        if (job.pages.empty()) return false;
        return std::all_of(job.pages.begin(), job.pages.end(),
            [](const BatchOcrPdfPageJob& page) {
                return page.status == BatchOcrTaskStatus::Completed ||
                    page.status == BatchOcrTaskStatus::Failed ||
                    page.status == BatchOcrTaskStatus::Canceled;
            });
    };
    auto waitForRuntimePdfJob = [&](
        const BatchOcrPdfJob& expected,
        int expectedPageCount,
        DWORD timeoutMs,
        int& jobIndex,
        BatchOcrPdfJob& finishedJob)
    {
        jobIndex = -1;
        finishedJob = BatchOcrPdfJob{};
        bool pdfFinished = DashboardWindowTestPumpUntil(hwnd, timeoutMs, [&]() {
            if (DashboardStatePdfRenderInFlight(window->m_dashboardState) != 0 ||
                DashboardStateIsOcrBusy(window->m_dashboardState) ||
                !window->m_batch.dropQueue.empty()) {
                return false;
            }
            int index = findActivePdfJobIndex(expected);
            if (index < 0) return false;
            const BatchOcrPdfJob& active = window->m_batch.activePdfJobs[(size_t)index];
            if (expectedPageCount > 0 && (int)active.pages.size() != expectedPageCount) return false;
            return pdfPagesTerminal(active);
        });
        if (!pdfFinished) return false;

        jobIndex = findActivePdfJobIndex(expected);
        if (jobIndex < 0) return false;
        finishedJob = window->m_batch.activePdfJobs[(size_t)jobIndex];
        return true;
    };
    auto verifyRuntimePdfJob = [&](
        int pdfJobIndex,
        int expectedPageCount,
        size_t expectedHistoryCount,
        const std::wstring& label)
    {
        if (window->m_history.model.items.size() != expectedHistoryCount) {
            return fail(label + L": PDF OCR should not append pages to image history.");
        }
        if (pdfJobIndex < 0 || pdfJobIndex >= (int)window->m_batch.activePdfJobs.size()) {
            return fail(label + L": active PDF job was not found.");
        }

        const BatchOcrPdfJob& active = window->m_batch.activePdfJobs[(size_t)pdfJobIndex];
        if ((int)active.pages.size() != expectedPageCount) {
            return fail(label + L": active PDF job has the wrong page count.");
        }
        if (active.markdownPath.empty() ||
            active.textPath.empty() ||
            active.contentJsonPath.empty() ||
            !PathFileExistsW(active.markdownPath.c_str()) ||
            !PathFileExistsW(active.textPath.c_str()) ||
            !PathFileExistsW(active.contentJsonPath.c_str())) {
            return fail(label + L": PDF document outputs were not written.");
        }

        std::wstring documentMarkdown;
        if (!DashboardWindowTestReadUtf8File(active.markdownPath, documentMarkdown) ||
            !DashboardWindowTestLooksLikeExpectedOcr(documentMarkdown)) {
            return fail(label + L": PDF document markdown did not contain expected OCR text.");
        }

        window->ActivateSourceRailPdfItem(pdfJobIndex, 0, true);
        if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
            DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != 0 ||
            !window->m_gdiplusImage) {
            return fail(label + L": PDF document row did not load Canvas.");
        }
        window->SetTextMode(DashboardTextMode::Source);
        if (!DashboardWindowTestLooksLikeExpectedOcr(window->GetCurrentResultText())) {
            return fail(label + L": PDF document Source mode lost OCR text.");
        }
        window->SetTextMode(DashboardTextMode::Text);
        if (!DashboardWindowTestLooksLikeExpectedOcr(window->GetCurrentResultText())) {
            return fail(label + L": PDF document Text mode lost OCR text.");
        }
        window->SetTextMode(DashboardTextMode::Json);
        if (window->GetCurrentResultText().find(L"pages") == std::wstring::npos) {
            return fail(label + L": PDF document JSON mode did not expose page data.");
        }
        int oldResultWidth = window->m_layout.resultWidth;
        window->m_layout.resultWidth = max(window->m_metrics.resultMinW, oldResultWidth + window->Scale(24));
        window->LayoutControls();
        window->SetTextMode(DashboardTextMode::Source);
        if (!DashboardWindowTestLooksLikeExpectedOcr(window->GetCurrentResultText())) {
            return fail(label + L": Result Inspector resize lost the selected PDF result.");
        }
        window->m_layout.resultWidth = oldResultWidth;
        window->LayoutControls();

        for (const auto& page : active.pages) {
            if (page.status != BatchOcrTaskStatus::Completed ||
                page.sourceImagePath.empty() ||
                page.markdownPath.empty() ||
                page.textPath.empty() ||
                page.contentJsonPath.empty() ||
                !PathFileExistsW(page.sourceImagePath.c_str()) ||
                !PathFileExistsW(page.markdownPath.c_str()) ||
                !PathFileExistsW(page.textPath.c_str()) ||
                !PathFileExistsW(page.contentJsonPath.c_str())) {
                return fail(label + L": PDF page outputs were not completed.");
            }

            std::wstring pageMarkdown;
            if (!DashboardWindowTestReadUtf8File(page.markdownPath, pageMarkdown) ||
                !DashboardWindowTestLooksLikeExpectedOcr(pageMarkdown)) {
                return fail(label + L": PDF page markdown did not contain expected OCR text.");
            }

            window->ActivateSourceRailPdfItem(pdfJobIndex, page.pageIndex, false);
            const bool flattenedFirstPage = page.pageIndex == 1;
            const int expectedSelectedPage = flattenedFirstPage ? 0 : page.pageIndex;
            if (!DashboardStateHasPdfSelection(window->m_dashboardState) ||
                DashboardStatePdfSelectionPageIndex(window->m_dashboardState) != expectedSelectedPage ||
                !window->m_gdiplusImage) {
                return fail(label + L": PDF page selection did not load Canvas.");
            }
            if (flattenedFirstPage) {
                std::vector<SourceRailTaskRow> runtimeRows = window->BuildSourceRailTaskRows();
                int matchingRoots = 0;
                int matchingPages = 0;
                for (const auto& row : runtimeRows) {
                    if (row.pdfJobIndex != pdfJobIndex) continue;
                    if (row.kind == SourceRailTaskRowKind::PdfJob) matchingRoots++;
                    if (row.kind == SourceRailTaskRowKind::PdfPage) matchingPages++;
                }
                int expectedVisiblePages = (int)std::count_if(
                    active.pages.begin(), active.pages.end(),
                    [](const BatchOcrPdfPageJob& candidate) { return candidate.pageIndex > 1; });
                if (matchingRoots != 1 || matchingPages != expectedVisiblePages) {
                    return fail(label + L": runtime PDF did not hide Page 1 while retaining later Page rows.");
                }
            }
            window->SetTextMode(DashboardTextMode::Source);
            if (!DashboardWindowTestLooksLikeExpectedOcr(window->GetCurrentResultText())) {
                return fail(label + L": PDF page Source mode lost OCR text.");
            }
            window->SetTextMode(DashboardTextMode::Text);
            if (!DashboardWindowTestLooksLikeExpectedOcr(window->GetCurrentResultText())) {
                return fail(label + L": PDF page Text mode lost OCR text.");
            }
            window->SetTextMode(DashboardTextMode::Json);
            const std::wstring runtimeJson = window->GetCurrentResultText();
            const std::wstring expectedJsonField = flattenedFirstPage ? L"pages" : L"elapsedMs";
            if (runtimeJson.find(expectedJsonField) == std::wstring::npos) {
                return fail(label + L": PDF page JSON mode did not expose metadata.");
            }
        }

        return true;
    };
    auto createAndRunRuntimePdf = [&](
        const std::wstring& fileName,
        const std::vector<std::wstring>& pageTexts,
        const std::wstring& pageRange,
        int expectedPageCount,
        const std::wstring& label,
        int& jobIndex,
        BatchOcrPdfJob& finishedJob)
    {
        std::wstring pdfPath = runRoot + L"\\" + fileName;
        if (!DashboardWindowTestWriteSimplePdf(pdfPath, pageTexts) ||
            !PathFileExistsW(pdfPath.c_str())) {
            return fail(label + L": failed to create PDF fixture.");
        }

        std::wstring pdfOutputRoot = runRoot + L"\\pdf_runtime_output";
        if (!BatchOcrWriter::EnsureDirectory(pdfOutputRoot)) {
            return fail(label + L": failed to create PDF output root.");
        }

        BatchOcrController controller;
        BatchOcrPdfJob pdfJob;
        std::wstring setupError;
        if (!controller.CreatePdfJob(pdfPath, pdfOutputRoot, pdfJob, setupError)) {
            return fail(setupError.empty()
                ? (label + L": failed to create PDF job.")
                : setupError);
        }
        pdfJob.pageRange = pageRange;
        pdfJob.pdfRenderDpi = 200;

        window->StartPdfRenderJob(pdfJob);
        if (!waitForRuntimePdfJob(pdfJob, expectedPageCount, 90000, jobIndex, finishedJob)) {
            return fail(label + L": PDF render/OCR did not complete.");
        }
        return true;
    };

    size_t historyCountBeforePdf = window->m_history.model.items.size();
    int singlePdfJobIndex = -1;
    BatchOcrPdfJob singlePdfJob;
    if (!createAndRunRuntimePdf(
            L"local_ocr_source.pdf",
            { L"ZEN CROP 123" },
            L"1",
            1,
            L"Dashboard runtime single-page PDF",
            singlePdfJobIndex,
            singlePdfJob) ||
        !verifyRuntimePdfJob(singlePdfJobIndex, 1, historyCountBeforePdf, L"Dashboard runtime single-page PDF")) {
        return false;
    }

    int multiPdfJobIndex = -1;
    BatchOcrPdfJob multiPdfJob;
    if (!createAndRunRuntimePdf(
            L"local_ocr_multi_page.pdf",
            { L"ZEN CROP 123", L"ZEN CROP 456" },
            L"1,2",
            2,
            L"Dashboard runtime multi-page PDF",
            multiPdfJobIndex,
            multiPdfJob) ||
        !verifyRuntimePdfJob(multiPdfJobIndex, 2, historyCountBeforePdf, L"Dashboard runtime multi-page PDF")) {
        return false;
    }

    int secondPdfJobIndex = -1;
    BatchOcrPdfJob secondPdfJob;
    if (!createAndRunRuntimePdf(
            L"local_ocr_second.pdf",
            { L"ZEN CROP 789" },
            L"1",
            1,
            L"Dashboard runtime second PDF",
            secondPdfJobIndex,
            secondPdfJob) ||
        !verifyRuntimePdfJob(secondPdfJobIndex, 1, historyCountBeforePdf, L"Dashboard runtime second PDF")) {
        return false;
    }

    if (findActivePdfJobIndex(singlePdfJob) < 0 ||
        findActivePdfJobIndex(multiPdfJob) < 0 ||
        findActivePdfJobIndex(secondPdfJob) < 0) {
        return fail(L"Dashboard runtime did not keep all PDF jobs visible in Source Rail state.");
    }

    int retryPdfJobIndex = findActivePdfJobIndex(multiPdfJob);
    if (retryPdfJobIndex < 0) {
        return fail(L"Dashboard runtime retry scenario could not find the multi-page PDF job.");
    }
    BatchOcrPdfJob retryPdfJobBeforeFailure = window->m_batch.activePdfJobs[(size_t)retryPdfJobIndex];
    if (retryPdfJobBeforeFailure.pages.size() < 2 ||
        retryPdfJobBeforeFailure.pages[1].sourceImagePath.empty() ||
        !PathFileExistsW(retryPdfJobBeforeFailure.pages[1].sourceImagePath.c_str())) {
        return fail(L"Dashboard runtime retry scenario does not have a reusable PDF page image.");
    }
    BatchOcrWriteResult failedPageWrite = window->RecordPdfPageFailure(
        retryPdfJobBeforeFailure,
        retryPdfJobBeforeFailure.pages[1].pageIndex,
        L"paddle_local",
        L"runtime retry contract injected failure",
        0);
    if (!failedPageWrite.success || window->m_batch.failedPdfPages.size() != 1) {
        return fail(L"Dashboard runtime failed PDF page was not exposed as retryable.");
    }
    retryPdfJobIndex = findActivePdfJobIndex(multiPdfJob);
    if (retryPdfJobIndex < 0 ||
        window->m_batch.activePdfJobs[(size_t)retryPdfJobIndex].pages[1].status != BatchOcrTaskStatus::Failed) {
        return fail(L"Dashboard runtime failed PDF page did not update active Source Rail state.");
    }

    window->RetryFailedBatchJobs();
    bool retryFinished = DashboardWindowTestPumpUntil(hwnd, 300000, [&]() {
        if (DashboardStatePdfRenderInFlight(window->m_dashboardState) != 0 ||
            DashboardStateIsOcrBusy(window->m_dashboardState) ||
            !window->m_batch.dropQueue.empty() ||
            !window->m_batch.failedPdfPages.empty()) {
            return false;
        }
        int index = findActivePdfJobIndex(multiPdfJob);
        if (index < 0) return false;
        const BatchOcrPdfJob& active = window->m_batch.activePdfJobs[(size_t)index];
        return active.pages.size() >= 2 &&
            active.pages[1].status == BatchOcrTaskStatus::Completed &&
            DashboardWindowTestLooksLikeExpectedOcr(active.pages[1].markdown);
    });
    if (!retryFinished) {
        return fail(L"Dashboard runtime PDF failed page retry did not complete.");
    }
    retryPdfJobIndex = findActivePdfJobIndex(multiPdfJob);
    if (retryPdfJobIndex < 0 ||
        !verifyRuntimePdfJob(retryPdfJobIndex, 2, historyCountBeforePdf, L"Dashboard runtime PDF retry")) {
        return false;
    }

    // ALT/main hotkey OCR already owns a durable screenshot before inference
    // begins. It must immediately project that image as one Recognizing Source,
    // then merge the final History payload back into the same Source identity.
    window->UpdateSourceRailHeader();
    const size_t externalTaskCountBefore = window->m_batch.batchTasks.size();
    const size_t externalHistoryCountBefore = window->m_history.model.items.size();
    const int headerRootsBefore = window->m_cachedVisibleRootCount;
    const uint64_t externalProgressId = ShowExternalOcrProgress(
        L"paddle_local_doc",
        sourceImagePath,
        true);
    auto externalPending = window->m_batch.externalOcrJobs.find(externalProgressId);
    if (externalProgressId == 0 ||
        externalPending == window->m_batch.externalOcrJobs.end() ||
        window->m_batch.batchTasks.size() != externalTaskCountBefore + 1 ||
        !DashboardStateHasImageTaskSelection(window->m_dashboardState) ||
        !window->m_gdiplusImage ||
        externalPending->second.status != BatchOcrTaskStatus::Recognizing ||
        DashboardStateImageTaskSelectionSourceInstanceId(window->m_dashboardState) != externalPending->second.sourceInstanceId ||
        window->BuildActiveWorkText().find(L"paddle_local_doc") == std::wstring::npos) {
        return fail(L"Hotkey OCR did not immediately create/select a Recognizing image Source.");
    }
    // Removing paint-time UpdateSourceRailHeader exposed a gap: new image roots
    // must refresh header count immediately, not wait for history/filter/layout.
    if (headerRootsBefore >= 0 &&
        window->m_cachedVisibleRootCount != headerRootsBefore + 1) {
        return fail(L"New hotkey/import Source did not refresh Sources header root count.");
    }
    if (window->m_sourceHeaderText) {
        const int headerLen = GetWindowTextLengthW(window->m_sourceHeaderText);
        std::wstring headerText(static_cast<size_t>(headerLen) + 1, L'\0');
        if (headerLen > 0) {
            GetWindowTextW(window->m_sourceHeaderText, headerText.data(), headerLen + 1);
        }
        headerText.resize(static_cast<size_t>(headerLen));
        const std::wstring expectedCount = std::to_wstring(window->m_cachedVisibleRootCount);
        if (headerText.find(expectedCount) == std::wstring::npos) {
            return fail(L"Sources header window text does not include the updated root count.");
        }
    }
    const std::wstring externalSourceId = externalPending->second.sourceInstanceId;
    BatchOcrImageJob externalJobSnapshot = externalPending->second;
    // Status-only updates must not drop the cached root count.
    const int headerRootsAfterInsert = window->m_cachedVisibleRootCount;
    window->UpdateBatchTaskStatus(
        externalJobSnapshot,
        BatchOcrTaskStatus::Recognizing,
        0,
        L"");
    if (window->m_cachedVisibleRootCount != headerRootsAfterInsert) {
        return fail(L"Status-only batch task update corrupted Sources header root count cache.");
    }

    OcrDashboardHistoryItem externalResult;
    externalResult.timestamp = L"2026-07-16 10:53:43";
    externalResult.imagePath = sourceImagePath;
    externalResult.text = L"# Hotkey OCR\n\nImmediate source merge";
    externalResult.elapsedMs = 11000;
    OcrLayoutBlock externalBlock;
    externalBlock.id = L"page_1:layout_1";
    externalBlock.order = 1;
    externalBlock.label = L"text";
    externalBlock.content = L"Immediate source merge";
    externalBlock.bbox = RECT{10, 10, 220, 80};
    externalResult.blocks = {externalBlock};
    CompleteExternalOcr(externalProgressId, externalResult);

    int externalTaskIndex = -1;
    for (int index = 0; index < static_cast<int>(window->m_batch.batchTasks.size()); ++index) {
        if (DashboardProjectionTextEquals(
                window->m_batch.batchTasks[static_cast<size_t>(index)].job.sourceInstanceId,
                externalSourceId)) {
            externalTaskIndex = index;
            break;
        }
    }
    const OcrDashboardHistoryItem* externalHistory = nullptr;
    for (const auto& item : window->m_history.model.items) {
        if (DashboardProjectionTextEquals(item.sourceInstanceId, externalSourceId)) {
            externalHistory = &item;
            break;
        }
    }
    auto externalProjection = BuildDashboardSourceProjection(
        window->m_batch.batchTasks,
        window->m_batch.activePdfJobs,
        window->m_history.model.items);
    int externalProjectionCount = 0;
    for (const auto& entry : externalProjection) {
        if (entry.refs.imageTaskIndex == externalTaskIndex ||
            (externalHistory && entry.refs.historyIndex >= 0 &&
             DashboardProjectionTextEquals(
                 window->m_history.model.items[static_cast<size_t>(entry.refs.historyIndex)].sourceInstanceId,
                 externalSourceId))) {
            externalProjectionCount++;
        }
    }
    if (window->m_batch.externalOcrJobs.find(externalProgressId) != window->m_batch.externalOcrJobs.end() ||
        window->m_batch.externalOcrBusy ||
        window->m_history.model.items.size() != externalHistoryCountBefore + 1 ||
        externalTaskIndex < 0 ||
        window->m_batch.batchTasks[static_cast<size_t>(externalTaskIndex)].status != BatchOcrTaskStatus::Completed ||
        window->m_batch.batchTasks[static_cast<size_t>(externalTaskIndex)].job.blocks.size() != 1 ||
        !externalHistory ||
        externalHistory->blocks.size() != 1 ||
        externalProjectionCount != 1) {
        return fail(L"Hotkey OCR completion did not merge the result into exactly one completed Source.");
    }

    // Phase 1a contracts (runtime): continuation must inherit pause/counters;
    // pending-only render stays active; external completion order is per-id.
    auto clearRuntimeActivityState = [&]() {
        window->m_batch.dropQueue.clear();
        DashboardStateSyncBatchProgress(window->m_dashboardState, false, 0, 0, 0);
        DashboardStateSetBatchPaused(window->m_dashboardState, false);
        DashboardStateSetActiveWorkHadFailure(window->m_dashboardState, false);
        window->m_batch.externalOcrJobs.clear();
        window->m_batch.externalOcrRuntimes.clear();
        window->RefreshExternalOcrPresentation();
        window->m_hasCachedActivityProjection = false;
        window->m_cachedSourceOverlays.clear();
        window->m_cachedSourceHeaderActivity.clear();
        KillTimer(window->m_hwnd, TIMER_ACTIVE_WORK);
        window->m_activeWorkTimerRunning = false;
    };

    clearRuntimeActivityState();
    DashboardStateSetBatchPaused(window->m_dashboardState, true);
    DashboardStateSyncBatchProgress(window->m_dashboardState, DashboardStateIsCancelBatchRequested(window->m_dashboardState), 3, 3, 0);
    if (window->HasActiveBatchWork()) {
        return fail(L"Idle paused session with counters must not report active work without pending/queue/render.");
    }
    BatchOcrPdfJob continuationPdf = multiPdfJob;
    if (continuationPdf.pages.empty()) {
        return fail(L"Missing PDF fixture for continuation pause contract.");
    }
    for (auto& page : continuationPdf.pages) {
        if (page.sourceImagePath.empty()) page.sourceImagePath = sourceImagePath;
    }
    window->UpsertActivePdfJob(continuationPdf);
    const int doneBeforeContinuation = DashboardStateDropDone(window->m_dashboardState);
    const int totalBeforeContinuation = DashboardStateDropTotal(window->m_dashboardState);
    window->QueuePdfPageFile(
        continuationPdf.pages.front().sourceImagePath,
        continuationPdf,
        continuationPdf.pages.front(),
        true,
        /*preserveBatchPause=*/true);
    if (!DashboardStateIsBatchPaused(window->m_dashboardState) ||
        DashboardStateDropDone(window->m_dashboardState) != doneBeforeContinuation ||
        DashboardStateDropTotal(window->m_dashboardState) != totalBeforeContinuation + 1 ||
        !DashboardStateActiveWorkHadFailure(window->m_dashboardState) ||
        DashboardStateIsOcrBusy(window->m_dashboardState) ||
        window->m_batch.dropQueue.size() != 1) {
        return fail(L"PDF page continuation cleared pause/counters or dispatched while paused.");
    }

    clearRuntimeActivityState();
    {
        decltype(window->m_batch.pdfRenderPending)::value_type pendingOnly;
        pendingOnly.job = continuationPdf;
        pendingOnly.cloudNative = false;
        window->m_batch.pdfRenderPending.push_back(pendingOnly);
    }
    if (!window->HasActiveBatchWork()) {
        return fail(L"pdfRenderPending-only state must count as active batch work.");
    }

    clearRuntimeActivityState();
    const uint64_t externalA = ShowExternalOcrProgress(L"engine-a", L"", true);
    const uint64_t externalB = ShowExternalOcrProgress(L"engine-b", L"", true);
    if (externalA == 0 || externalB == 0 || externalA == externalB ||
        window->m_batch.externalOcrRuntimes.size() != 2 ||
        !window->m_batch.externalOcrBusy) {
        return fail(L"Concurrent source-less external OCR did not register two runtime records.");
    }
    HideExternalOcrProgress(externalB);
    if (window->m_batch.externalOcrRuntimes.size() != 1 ||
        window->m_batch.externalOcrRuntimes.find(externalA) == window->m_batch.externalOcrRuntimes.end() ||
        !window->m_batch.externalOcrBusy) {
        return fail(L"Completing external B first incorrectly cleared external A progress.");
    }
    HideExternalOcrProgress(externalA);
    if (!window->m_batch.externalOcrRuntimes.empty() || window->m_batch.externalOcrBusy) {
        return fail(L"Hiding final external OCR did not clear runtime map.");
    }

    if (DashboardSourceRailStatusText(
            BatchOcrTaskStatus::Recognizing, true, false, false, false) != L"Pausing") {
        return fail(L"SourceRailStatusText still prefers Paused over active Recognizing.");
    }

    DeleteFileW(testPositionPath.c_str());
    DeleteFileW(testDismissedPath.c_str());
    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    return true;
}
#endif
