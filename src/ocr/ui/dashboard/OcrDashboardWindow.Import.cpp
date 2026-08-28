#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardFileTypes.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardDialogLayout.h"
#include "dashboard/DashboardOleDropTarget.h"
#include "dashboard/DashboardFolderImportOptionsDialog.h"
#include "dashboard/DashboardOutputArtifactOptionsDialog.h"
#include "dashboard/DashboardPdfOptionsDialog.h"
#include "BatchOcrWriter.h"
#include "Settings.h"
#include "Strings.h"
#include "OcrUtils.h"
#include "core/WideStringUtils.h"

#include <commdlg.h>
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <memory>
#include <new>
#include <vector>
#include <windows.h>

// D-I-3: real TU (was Import.inl).

bool OcrDashboardWindow::IsSupportedImageFile(const std::wstring& filePath) const {
    return DashboardIsSupportedImageFile(filePath);
}

bool OcrDashboardWindow::IsSupportedPdfFile(const std::wstring& filePath) const {
    return DashboardIsSupportedPdfFile(filePath);
}

void OcrDashboardWindow::ImportImageFiles() {
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    if (!g_dashboardWindowTestImportFiles.empty()) {
        QueueImageFiles(g_dashboardWindowTestImportFiles);
        return;
    }
#endif

    std::vector<std::wstring> files;

    bool uninitializeCom = false;
    if (DashboardEnsureComForDashboard(uninitializeCom)) {
        IFileOpenDialog* dialog = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(hr) && dialog) {
            DWORD options = 0;
            if (SUCCEEDED(dialog->GetOptions(&options))) {
                dialog->SetOptions(options |
                    FOS_FORCEFILESYSTEM |
                    FOS_FILEMUSTEXIST |
                    FOS_PATHMUSTEXIST |
                    FOS_ALLOWMULTISELECT);
            }
            COMDLG_FILTERSPEC filters[] = {
        { L"Image/PDF files", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif;*.pdf" },
                { L"PDF files", L"*.pdf" },
        { L"Image files", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif" },
                { L"All files", L"*.*" },
            };
            dialog->SetFileTypes((UINT)(sizeof(filters) / sizeof(filters[0])), filters);
            dialog->SetFileTypeIndex(1);
            dialog->SetTitle(S::IsChinese() ? L"导入 OCR 图片或 PDF" : L"Import OCR images or PDFs");

            hr = dialog->Show(m_hwnd);
            if (SUCCEEDED(hr)) {
                IShellItemArray* results = nullptr;
                if (SUCCEEDED(dialog->GetResults(&results)) && results) {
                    DWORD count = 0;
                    if (SUCCEEDED(results->GetCount(&count))) {
                        files.reserve(count);
                        for (DWORD i = 0; i < count; ++i) {
                            IShellItem* item = nullptr;
                            if (SUCCEEDED(results->GetItemAt(i, &item)) && item) {
                                PWSTR path = nullptr;
                                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                                    files.emplace_back(path);
                                    CoTaskMemFree(path);
                                }
                                item->Release();
                            }
                        }
                    }
                    results->Release();
                }
            }
            dialog->Release();
            if (uninitializeCom) CoUninitialize();
            if (!files.empty()) {
                QueueImageFiles(files);
            }
            return;
        }
        if (dialog) dialog->Release();
        if (uninitializeCom) CoUninitialize();
    }

    std::vector<wchar_t> buffer(65536, L'\0');
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter =
            L"Image/PDF files (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif;*.pdf)\0"
            L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif;*.pdf\0"
        L"PDF files (*.pdf)\0*.pdf\0"
            L"Image files (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif)\0"
            L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif\0"
        L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = (DWORD)buffer.size();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring first = buffer.data();
    wchar_t* cursor = buffer.data() + first.size() + 1;
    if (*cursor == L'\0') {
        files.push_back(first);
    } else {
        std::wstring dir = first;
        while (*cursor != L'\0') {
            std::wstring name = cursor;
            std::wstring path = dir;
            if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L"\\";
            path += name;
            files.push_back(path);
            cursor += name.size() + 1;
        }
    }

    QueueImageFiles(files);
}

bool OcrDashboardWindow::RegisterOleDropTargets() {
    if (m_oleDropTarget) return true;

    HRESULT oleHr = OleInitialize(nullptr);
    if (FAILED(oleHr)) {
        return false;
    }
    m_oleInitializedForDrop = true;

    m_oleDropTarget = new (std::nothrow) DashboardOleDropTarget(this);
    if (!m_oleDropTarget) {
        RevokeOleDropTargets();
        return false;
    }

    HWND targets[] = {
        m_hwnd, m_imageArea, m_edit, m_searchEdit, m_sourceList,
        m_splitterHitTargets[0], m_splitterHitTargets[1], m_splitterHitTargets[2]
    };
    for (HWND target : targets) {
        if (!target) continue;
        HRESULT hr = RegisterDragDrop(target, m_oleDropTarget);
        if (SUCCEEDED(hr)) {
            m_oleDropTargetWindows.push_back(target);
        }
    }

    if (m_oleDropTargetWindows.empty()) {
        RevokeOleDropTargets();
        return false;
    }
    return true;
}

void OcrDashboardWindow::RevokeOleDropTargets() {
    for (HWND target : m_oleDropTargetWindows) {
        if (target && IsWindow(target)) {
            RevokeDragDrop(target);
        }
    }
    m_oleDropTargetWindows.clear();

    if (m_oleDropTarget) {
        m_oleDropTarget->Release();
        m_oleDropTarget = nullptr;
    }

    if (m_oleInitializedForDrop) {
        OleUninitialize();
        m_oleInitializedForDrop = false;
    }
}

bool OcrDashboardWindow::CanAcceptOleDropDataObject(IDataObject* dataObject) const {
    if (!dataObject) return false;

    FORMATETC hdropFormat = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (SUCCEEDED(dataObject->QueryGetData(&hdropFormat))) return true;

    CLIPFORMAT fileDescriptorFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
    if (fileDescriptorFormat == 0) return false;

    FORMATETC descriptorFormat = { fileDescriptorFormat, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (FAILED(dataObject->QueryGetData(&descriptorFormat))) return false;

    STGMEDIUM descriptorMedium = {};
    if (FAILED(dataObject->GetData(&descriptorFormat, &descriptorMedium))) return false;

    bool supported = false;
    auto* group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(descriptorMedium.hGlobal));
    if (group) {
        UINT count = (std::min)(group->cItems, static_cast<UINT>(128));
        for (UINT i = 0; i < count; ++i) {
            std::wstring displayName = group->fgd[i].cFileName;
            // OWN-96: pure file-name extract (WideStringUtils).
            std::wstring leaf = WideFileNameFromPath(displayName);
            std::wstring fileName = !leaf.empty() ? leaf : displayName;
            if (DashboardIsSupportedImageFile(fileName) || DashboardIsSupportedPdfFile(fileName)) {
                supported = true;
                break;
            }
        }
        GlobalUnlock(descriptorMedium.hGlobal);
    }
    ReleaseStgMedium(&descriptorMedium);
    return supported;
}

bool OcrDashboardWindow::HandleOleDropDataObject(IDataObject* dataObject) {
    std::vector<std::wstring> files;
    if (!ExtractOleDropFiles(dataObject, files) || files.empty()) {
        UpdateStatus(S::IsChinese()
            ? L"拖放内容中没有可识别的图片或 PDF"
            : L"Dropped data did not contain supported image or PDF files");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return false;
    }

    QueueImageFiles(files);
    return true;
}

bool OcrDashboardWindow::ExtractOleDropFiles(IDataObject* dataObject, std::vector<std::wstring>& files) const {
    files.clear();
    if (!dataObject) return false;

    std::vector<std::wstring> fileSystemFiles;
    if (ExtractFileSystemDropFiles(dataObject, fileSystemFiles)) {
        bool hasImportableFileSystemPath = std::any_of(
            fileSystemFiles.begin(),
            fileSystemFiles.end(),
            [this](const std::wstring& path) {
                DWORD attrs = GetFileAttributesW(path.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
                return DashboardIsSupportedImageFile(path) || DashboardIsSupportedPdfFile(path);
            });
        if (hasImportableFileSystemPath) {
            files = std::move(fileSystemFiles);
            return true;
        }
    }

    if (!ExtractVirtualDropFiles(dataObject, files)) {
        files = std::move(fileSystemFiles);
    }
    return !files.empty();
}

bool OcrDashboardWindow::ExtractFileSystemDropFiles(IDataObject* dataObject, std::vector<std::wstring>& files) const {
    FORMATETC format = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = {};
    if (FAILED(dataObject->GetData(&format, &medium))) return false;

    bool added = false;
    HDROP drop = static_cast<HDROP>(medium.hGlobal);
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    files.reserve(files.size() + count);
    for (UINT i = 0; i < count; ++i) {
        UINT len = DragQueryFileW(drop, i, nullptr, 0);
        if (len == 0) continue;
        std::wstring path(len + 1, L'\0');
        if (DragQueryFileW(drop, i, path.data(), len + 1)) {
            path.resize(len);
            files.push_back(std::move(path));
            added = true;
        }
    }
    ReleaseStgMedium(&medium);
    return added;
}

bool OcrDashboardWindow::ExtractVirtualDropFiles(IDataObject* dataObject, std::vector<std::wstring>& files) const {
    CLIPFORMAT descriptorClip = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
    CLIPFORMAT contentsClip = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILECONTENTS));
    if (descriptorClip == 0 || contentsClip == 0) return false;

    FORMATETC descriptorFormat = { descriptorClip, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM descriptorMedium = {};
    if (FAILED(dataObject->GetData(&descriptorFormat, &descriptorMedium))) return false;

    bool added = false;
    auto* group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(descriptorMedium.hGlobal));
    if (group) {
        UINT count = (std::min)(group->cItems, static_cast<UINT>(128));
        for (UINT i = 0; i < count; ++i) {
            const FILEDESCRIPTORW& descriptor = group->fgd[i];
            std::wstring displayName = descriptor.cFileName;
            // OWN-96: pure file-name extract (WideStringUtils).
            std::wstring leaf = WideFileNameFromPath(displayName);
            std::wstring fileName = !leaf.empty() ? leaf : displayName;
            if (fileName.empty()) continue;
            if (!DashboardIsSupportedImageFile(fileName) && !DashboardIsSupportedPdfFile(fileName)) continue;

            FORMATETC contentFormat = {
                contentsClip,
                nullptr,
                DVASPECT_CONTENT,
                static_cast<LONG>(i),
                static_cast<DWORD>(TYMED_HGLOBAL | TYMED_ISTREAM)
            };
            STGMEDIUM contentMedium = {};
            if (FAILED(dataObject->GetData(&contentFormat, &contentMedium))) {
                continue;
            }

            std::wstring cachedPath = DashboardMakeOcrImportCacheFilePath(fileName);
            bool wrote = DashboardWriteStorageMediumToFile(contentMedium, cachedPath);
            ReleaseStgMedium(&contentMedium);
            if (wrote && PathFileExistsW(cachedPath.c_str())) {
                files.push_back(cachedPath);
                added = true;
            }
        }
        GlobalUnlock(descriptorMedium.hGlobal);
    }

    ReleaseStgMedium(&descriptorMedium);
    return added;
}

bool OcrDashboardWindow::ResolvePreferredBatchOutputRoot(std::wstring& outputRoot) {
    outputRoot.clear();

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    if (!g_dashboardWindowTestBatchOutputRoot.empty()) {
        outputRoot = g_dashboardWindowTestBatchOutputRoot;
        return BatchOcrWriter::EnsureDirectory(outputRoot);
    }
#endif

    // Pure dual-write is read authority for preferred output root.
    std::wstring preferred = DashboardTrimWide(
        DashboardStatePreferredBatchOutputRoot(m_dashboardState));
    if (preferred.empty()) return false;
    if (!BatchOcrWriter::EnsureDirectory(preferred)) return false;

    outputRoot = preferred;
    return true;
}

bool OcrDashboardWindow::PromptForBatchOutputRoot(std::wstring& outputRoot) {
    outputRoot.clear();

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    if (!g_dashboardWindowTestBatchOutputRoot.empty()) {
        outputRoot = g_dashboardWindowTestBatchOutputRoot;
        return BatchOcrWriter::EnsureDirectory(outputRoot);
    }
#endif

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(S::IsChinese() ? L"选择 OCR 批量输出目录" : L"Choose OCR batch output folder");

    auto existingRoot = [](const std::wstring& candidate) -> std::wstring {
        std::wstring trimmed = DashboardTrimWide(candidate);
        return !trimmed.empty() && DashboardDirectoryExistsWide(trimmed) ? trimmed : L"";
    };
    // Pure dual-write is read authority for preferred/last/recent roots.
    std::wstring initialRoot = existingRoot(
        DashboardStatePreferredBatchOutputRoot(m_dashboardState));
    if (initialRoot.empty()) {
        initialRoot = existingRoot(DashboardStateLastBatchOutputRoot(m_dashboardState));
    }
    for (const auto& root : DashboardStateRecentBatchOutputRoots(m_dashboardState)) {
        if (!initialRoot.empty()) break;
        initialRoot = existingRoot(root);
    }
    if (!initialRoot.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialRoot.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    bool selected = false;
    if (SUCCEEDED(dialog->Show(m_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                outputRoot = path;
                CoTaskMemFree(path);
                selected = !outputRoot.empty();
            }
            item->Release();
        }
    }

    dialog->Release();
    return selected;
}

bool OcrDashboardWindow::PromptForOutputArtifactOptions(
    OcrOutputArtifactOptions& options,
    std::wstring* outputRoot)
{
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    // Existing window-contract callers inject only a folder choice. Preserve
    // that deterministic seam while retaining the current default artifact
    // policy; interactive builds always open the unified settings dialog.
    if (outputRoot && !g_dashboardWindowTestBatchOutputRoot.empty()) {
        *outputRoot = g_dashboardWindowTestBatchOutputRoot;
        options = NormalizeOcrOutputArtifactOptions(options);
        return true;
    }
#endif
    OcrOutputArtifactOptions draft = NormalizeOcrOutputArtifactOptions(options);
    std::wstring rootDraft = outputRoot ? *outputRoot : L"";
    if (!DashboardShowOutputArtifactOptionsDialog(
            m_hwnd,
            draft,
            outputRoot ? &rootDraft : nullptr,
            m_dpi,
            m_hUiFont)) {
        return false;
    }
    if (outputRoot) *outputRoot = DashboardTrimWide(rootDraft);
    options = NormalizeOcrOutputArtifactOptions(draft);
    return true;
}

void OcrDashboardWindow::ChooseBatchOutputRoot() {
    // Pure dual-write is read authority for preferred output root.
    std::wstring outputRoot = DashboardStatePreferredBatchOutputRoot(m_dashboardState);
    if (outputRoot.empty()) {
        ResolveDefaultBatchOutputRoot(outputRoot);
    }
    OcrOutputArtifactOptions artifacts = DashboardStateOcrOutputArtifactOptions(m_dashboardState);
    if (!PromptForOutputArtifactOptions(artifacts, &outputRoot)) return;

    outputRoot = DashboardTrimWide(outputRoot);
    if (outputRoot.empty()) return;
    if (!BatchOcrWriter::EnsureDirectory(outputRoot)) {
        MessageBoxW(
            m_hwnd,
            L"Failed to create the selected output folder.",
            L"ZenCrop",
            MB_OK | MB_ICONERROR);
        return;
    }

    DashboardStateApplyBatchOutputRoots(
        m_dashboardState,
        outputRoot,
        DashboardStateLastBatchOutputRoot(m_dashboardState),
        DashboardStateRecentBatchOutputRoots(m_dashboardState));
    // D-B-4: sole write authority is DashboardState output artifact defaults.
    {
        const OcrOutputArtifactOptions normalized = NormalizeOcrOutputArtifactOptions(artifacts);
        DashboardOutputArtifactDefaults defaults;
        defaults.writeLayoutPreview = normalized.writeLayoutPreview;
        defaults.layoutPreviewFormat = static_cast<int>(normalized.layoutPreviewFormat);
        defaults.layoutPreviewQuality = normalized.layoutPreviewQuality;
        defaults.pdfThumbnailPolicy = static_cast<int>(normalized.pdfThumbnailPolicy);
        defaults.pdfThumbnailFormat = static_cast<int>(normalized.pdfThumbnailFormat);
        defaults.pdfThumbnailQuality = normalized.pdfThumbnailQuality;
        defaults.pdfThumbnailMaxPixelEdge = normalized.pdfThumbnailMaxPixelEdge;
        defaults.embeddedAssetFormat = static_cast<int>(normalized.embeddedAssetFormat);
        defaults.embeddedAssetQuality = normalized.embeddedAssetQuality;
        DashboardStateApplyOutputArtifactDefaults(m_dashboardState, defaults);
    }
    SaveBatchSessionState();
    RefreshAllTexts();
    if (m_openOutputBtn) EnableWindow(m_openOutputBtn, TRUE);
    UpdatePreviewControls();
    UpdateStatus(std::wstring(L"Default output folder: ") + outputRoot);
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
}

bool OcrDashboardWindow::ResolveDefaultBatchOutputRoot(std::wstring& outputRoot) {
    outputRoot.clear();

    if (ResolvePreferredBatchOutputRoot(outputRoot)) return true;

    auto useExistingRoot = [&](const std::wstring& candidate) {
        std::wstring trimmed = DashboardTrimWide(candidate);
        if (trimmed.empty() || !DashboardDirectoryExistsWide(trimmed)) return false;
        outputRoot = trimmed;
        return true;
    };

    // Pure dual-write is read authority for last/recent output roots.
    if (useExistingRoot(DashboardStateLastBatchOutputRoot(m_dashboardState))) return true;
    for (const auto& root : DashboardStateRecentBatchOutputRoots(m_dashboardState)) {
        if (useExistingRoot(root)) return true;
    }

    std::wstring cacheDir = GetOcrImageDir();
    std::wstring defaultRoot = DashboardJoinPathWide(cacheDir, L"batch_output");
    if (BatchOcrWriter::EnsureDirectory(defaultRoot)) {
        outputRoot = defaultRoot;
        return true;
    }

    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPath) > 0) {
        std::wstring tempRoot = DashboardJoinPathWide(tempPath, L"ZenCropOcrBatchOutput");
        if (BatchOcrWriter::EnsureDirectory(tempRoot)) {
            outputRoot = tempRoot;
            return true;
        }
    }

    return false;
}

bool OcrDashboardWindow::PromptForFolderImportOptions(size_t directoryCount, std::wstring& outputRoot) {
    if (directoryCount == 0) return true;

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    if (g_dashboardWindowTestUseFolderImportOptions) {
        if (outputRoot.empty()) {
            ResolveDefaultBatchOutputRoot(outputRoot);
        }
        // D-B-2: sole write authority is DashboardState folder import prefs.
        {
            const bool recursive = g_dashboardWindowTestFolderImportRecursive;
            const int maxDepth = DashboardNormalizeFolderImportDepth(
                recursive ? g_dashboardWindowTestFolderImportMaxDepth : 0);
            DashboardStateApplyFolderImportPrefs(
                m_dashboardState,
                recursive,
                maxDepth,
                DashboardTrimWide(g_dashboardWindowTestFolderImportExcludePatterns));
        }
        SaveBatchSessionState();
        return true;
    }
#endif

    // D-B-9: folder options dialog lives in DashboardFolderImportOptionsDialog.cpp.
    {
        UINT dpi = m_hwnd ? GetDpiForWindow(m_hwnd) : (m_dpi > 0 ? m_dpi : kDashboardHostDesignDpi);
        if (dpi == 0) dpi = kDashboardHostDesignDpi;
        HFONT font = DashboardCreateHostFont(20, dpi);
        bool ownsFont = font != nullptr;
        if (!font) {
            font = m_hUiFont;
            ownsFont = false;
        }
        auto run = DashboardRunFolderImportOptionsDialog(
            m_hwnd,
            dpi,
            font,
            directoryCount,
            DashboardStateIsFolderImportRecursive(m_dashboardState),
            DashboardNormalizeFolderImportDepth(DashboardStateFolderImportMaxDepth(m_dashboardState)),
            DashboardStateFolderImportExcludePatterns(m_dashboardState),
            outputRoot);
        if (ownsFont && font) DeleteObject(font);

        if (run.dialogFailedOpen) {
            MessageBoxW(
                m_hwnd,
                S::IsChinese()
                    ? L"无法打开文件夹导入选项，将使用上次保存的设置。"
                    : L"Folder import options could not be opened; the last saved settings will be used.",
                L"ZenCrop",
                MB_OK | MB_ICONWARNING);
            return true;
        }
        if (!run.result.accepted) return false;

        DashboardStateApplyFolderImportPrefs(
            m_dashboardState,
            run.result.recursive,
            DashboardNormalizeFolderImportDepth(run.result.maxDepth),
            DashboardTrimWide(run.result.excludePatterns));
        outputRoot = DashboardTrimWide(run.result.outputRoot);
        SaveBatchSessionState();
        return true;
    }
}

bool OcrDashboardWindow::PromptForPdfImportOptions(
    const std::vector<std::wstring>& pdfs,
    std::wstring& outputRoot,
    DashboardPdfImportOptions& options)
{
    if (pdfs.empty()) return true;

    // Seed from DashboardState sole session prefs (typed result domain owns dialog).
    const std::wstring& lastPageRange = DashboardStateLastPdfPageRange(m_dashboardState);
    const int lastRenderDpi = DashboardStateLastPdfRenderDpi(m_dashboardState);
    const unsigned int lastMaxPixelEdge = DashboardStateLastPdfMaxPixelEdge(m_dashboardState);
    const unsigned int lastMaxMegapixels = DashboardStateLastPdfMaxMegapixels(m_dashboardState);
    const auto lastImageFormat = static_cast<PdfRenderImageFormat>(
        DashboardStateLastPdfImageFormat(m_dashboardState));
    const int lastImageQuality = DashboardStateLastPdfImageQuality(m_dashboardState);
    if (options.pageRange.empty()) {
        options.pageRange = lastPageRange.empty() ? L"all" : lastPageRange;
    }
    if (options.pdfRenderDpi <= 0) {
        options.pdfRenderDpi = lastRenderDpi > 0 ? lastRenderDpi : kDefaultPdfRenderDpi;
    }
    options.pdfMaxPixelEdge = options.pdfMaxPixelEdge > 0 ? options.pdfMaxPixelEdge : lastMaxPixelEdge;
    options.pdfMaxMegapixels = options.pdfMaxMegapixels > 0 ? options.pdfMaxMegapixels : lastMaxMegapixels;
    options.pdfImageFormat = lastImageFormat;
    options.pdfImageQuality = options.pdfImageQuality > 0 ? options.pdfImageQuality : lastImageQuality;
    OcrOutputArtifactOptions defaultConstructedArtifacts;
    options.outputArtifacts = NormalizeOcrOutputArtifactOptions(
        DashboardOutputArtifactOptionsEqual(
            options.outputArtifacts, defaultConstructedArtifacts)
            ? DashboardStateOcrOutputArtifactOptions(m_dashboardState)
            : options.outputArtifacts);
    options.ocrMode = options.ocrMode.empty()
        ? GetDashboardOcrMode()
        : DashboardNormalizeOcrMode(options.ocrMode);

    std::vector<PdfImportPreflightInfo> preflight;
    int totalPageCount = 0;
    if (!DashboardCollectPdfImportPreflight(m_hwnd, pdfs, m_dpi, m_hUiFont, preflight, totalPageCount)) {
        return false;
    }
    options.pdfPasswords.clear();
    options.pdfPageCounts.clear();
    options.pdfRequiresPasswords.clear();
    options.pdfPasswords.reserve(preflight.size());
    options.pdfPageCounts.reserve(preflight.size());
    options.pdfRequiresPasswords.reserve(preflight.size());
    for (const auto& info : preflight) {
        options.pdfPasswords.push_back(info.password);
        options.pdfPageCounts.push_back(info.pageCount);
        options.pdfRequiresPasswords.push_back(info.requiresPassword);
    }
    options.cloudFullPdfConsentGranted = false;
    options.rememberCloudFullPdfConsent =
        DashboardStateIsPdfCloudRememberFullPdfConsent(m_dashboardState);

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    if (g_dashboardWindowTestUsePdfImportOptions) {
        options.pageRange = g_dashboardWindowTestPdfImportOptions.pageRange.empty()
            ? L"all"
            : g_dashboardWindowTestPdfImportOptions.pageRange;
        options.pdfRenderDpi = g_dashboardWindowTestPdfImportOptions.pdfRenderDpi > 0
            ? g_dashboardWindowTestPdfImportOptions.pdfRenderDpi
            : options.pdfRenderDpi;
        options.pdfMaxPixelEdge = g_dashboardWindowTestPdfImportOptions.pdfMaxPixelEdge;
        options.pdfMaxMegapixels = g_dashboardWindowTestPdfImportOptions.pdfMaxMegapixels;
        options.pdfImageFormat = g_dashboardWindowTestPdfImportOptions.pdfImageFormat;
        options.pdfImageQuality = g_dashboardWindowTestPdfImportOptions.pdfImageQuality;
        if (!g_dashboardWindowTestPdfImportOptions.ocrMode.empty()) {
            options.ocrMode = DashboardNormalizeOcrMode(g_dashboardWindowTestPdfImportOptions.ocrMode);
        }
        if (!g_dashboardWindowTestPdfImportOptions.pdfPasswords.empty()) {
            options.pdfPasswords = g_dashboardWindowTestPdfImportOptions.pdfPasswords;
        }

        int selectedPageCount = 0;
        if (!DashboardValidatePdfOptions(m_hwnd, options.pageRange, options.pdfRenderDpi, &preflight, &selectedPageCount)) {
            return false;
        }

        {
            DashboardPdfImportSessionPrefs prefs;
            prefs.pageRange = options.pageRange.empty() ? L"all" : options.pageRange;
            prefs.renderDpi = options.pdfRenderDpi > 0 ? options.pdfRenderDpi : kDefaultPdfRenderDpi;
            prefs.maxPixelEdge = options.pdfMaxPixelEdge;
            prefs.maxMegapixels = options.pdfMaxMegapixels;
            prefs.imageFormat = static_cast<int>(options.pdfImageFormat);
            prefs.imageQuality = options.pdfImageQuality;
            prefs.rememberCloudFullPdfConsent =
                DashboardStateIsPdfCloudRememberFullPdfConsent(m_dashboardState);
            DashboardStateApplyPdfImportSessionPrefs(m_dashboardState, std::move(prefs));
        }
        SaveBatchSessionState();
        return true;
    }
#endif

    const auto savePdfImportSettings = [this](const DashboardPdfImportOptions& savedOptions) {
        DashboardPdfImportSessionPrefs prefs;
        prefs.pageRange = savedOptions.pageRange.empty() ? L"all" : savedOptions.pageRange;
        prefs.renderDpi = savedOptions.pdfRenderDpi > 0
            ? savedOptions.pdfRenderDpi
            : kDefaultPdfRenderDpi;
        prefs.maxPixelEdge = savedOptions.pdfMaxPixelEdge;
        prefs.maxMegapixels = savedOptions.pdfMaxMegapixels;
        prefs.imageFormat = static_cast<int>(savedOptions.pdfImageFormat);
        prefs.imageQuality = savedOptions.pdfImageQuality;
        prefs.rememberCloudFullPdfConsent = savedOptions.rememberCloudFullPdfConsent;
        DashboardStateApplyPdfImportSessionPrefs(m_dashboardState, std::move(prefs));
        {
            const OcrOutputArtifactOptions normalized =
                NormalizeOcrOutputArtifactOptions(savedOptions.outputArtifacts);
            DashboardOutputArtifactDefaults defaults;
            defaults.writeLayoutPreview = normalized.writeLayoutPreview;
            defaults.layoutPreviewFormat = static_cast<int>(normalized.layoutPreviewFormat);
            defaults.layoutPreviewQuality = normalized.layoutPreviewQuality;
            defaults.pdfThumbnailPolicy = static_cast<int>(normalized.pdfThumbnailPolicy);
            defaults.pdfThumbnailFormat = static_cast<int>(normalized.pdfThumbnailFormat);
            defaults.pdfThumbnailQuality = normalized.pdfThumbnailQuality;
            defaults.pdfThumbnailMaxPixelEdge = normalized.pdfThumbnailMaxPixelEdge;
            defaults.embeddedAssetFormat = static_cast<int>(normalized.embeddedAssetFormat);
            defaults.embeddedAssetQuality = normalized.embeddedAssetQuality;
            DashboardStateApplyOutputArtifactDefaults(m_dashboardState, defaults);
        }
        SetDashboardOcrMode(savedOptions.ocrMode, false);
        OcrSettings ocrSettings = LoadOcrSettings();
        ocrSettings.localRasterMaxPixelEdge = savedOptions.pdfMaxPixelEdge;
        ocrSettings.localRasterMaxMegapixels = savedOptions.pdfMaxMegapixels;
        SaveOcrSettings(ocrSettings);
        SaveBatchSessionState();
        RefreshAllTexts();
    };

    DashboardPdfOptionsDialogRunInput runInput;
    runInput.owner = m_hwnd;
    runInput.dpi = m_hwnd ? GetDpiForWindow(m_hwnd) : (m_dpi > 0 ? m_dpi : kDashboardHostDesignDpi);
    if (runInput.dpi == 0) runInput.dpi = kDashboardHostDesignDpi;
    runInput.fallbackFont = m_hUiFont;
    runInput.pdfs = &pdfs;
    runInput.preflight = &preflight;
    runInput.totalPageCount = totalPageCount;
    runInput.options = &options;
    runInput.outputRoot = &outputRoot;
    runInput.editOutputArtifacts = [this](OcrOutputArtifactOptions& artifacts) {
        return PromptForOutputArtifactOptions(artifacts, nullptr);
    };
    runInput.saveSettings = savePdfImportSettings;

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    DashboardPdfOptionsDialogTestDrive testDrive;
    if (g_dashboardWindowTestDrivePdfOptionsDialog) {
        testDrive.enabled = true;
        testDrive.cancel = g_dashboardWindowTestCancelPdfOptionsDialog;
        testDrive.saveThenCancel = g_dashboardWindowTestSavePdfOptionsDialog;
        testDrive.drivenOptions = g_dashboardWindowTestDrivenPdfImportOptions;
        testDrive.saveStayedOpenOut = &g_dashboardWindowTestSavePdfOptionsDialogStayedOpen;
        runInput.testDrive = &testDrive;
    }
#endif

    DashboardPdfOptionsDialogRunResult run = DashboardRunPdfImportOptionsDialog(runInput);

    // Class registration / CreateWindow failed: keep legacy non-modal fallback.
    if (run.dialogFailedOpen) {
        int selectedPageCount = run.selectedPageCount;
        if (selectedPageCount <= 0) {
            if (!DashboardValidatePdfOptions(
                    m_hwnd, options.pageRange, options.pdfRenderDpi, &preflight, &selectedPageCount)) {
                return false;
            }
        }
        {
            DashboardPdfImportSessionPrefs prefs;
            prefs.pageRange = options.pageRange.empty() ? L"all" : options.pageRange;
            prefs.renderDpi = options.pdfRenderDpi > 0 ? options.pdfRenderDpi : kDefaultPdfRenderDpi;
            prefs.maxPixelEdge = options.pdfMaxPixelEdge;
            prefs.maxMegapixels = options.pdfMaxMegapixels;
            prefs.imageFormat = static_cast<int>(options.pdfImageFormat);
            prefs.imageQuality = options.pdfImageQuality;
            prefs.rememberCloudFullPdfConsent = options.rememberCloudFullPdfConsent;
            DashboardStateApplyPdfImportSessionPrefs(m_dashboardState, std::move(prefs));
        }
        SaveBatchSessionState();

        if (DashboardIsCloudOcrMode(options.ocrMode)) {
            if (DashboardStateIsPdfCloudRememberFullPdfConsent(m_dashboardState)) {
                options.cloudFullPdfConsentGranted = true;
                return true;
            }
            std::wstring prompt = DashboardFormatPdfCloudConfirmPrompt(
                selectedPageCount,
                totalPageCount,
                (int)pdfs.size(),
                DashboardStatePdfCloudRiskPolicy(m_dashboardState));
            options.cloudFullPdfConsentGranted =
                MessageBoxW(m_hwnd, prompt.c_str(), L"ZenCrop", MB_YESNO | MB_ICONQUESTION) == IDYES;
            return options.cloudFullPdfConsentGranted;
        }
        return true;
    }

    if (!run.accepted) return false;

    savePdfImportSettings(options);

    options.ocrMode = DashboardNormalizeOcrMode(options.ocrMode);
    if (DashboardIsCloudOcrMode(options.ocrMode)) {
        if (DashboardStateIsPdfCloudRememberFullPdfConsent(m_dashboardState)) {
            options.cloudFullPdfConsentGranted = true;
            return true;
        }
        std::wstring prompt = DashboardFormatPdfCloudConfirmPrompt(
            run.selectedPageCount,
            totalPageCount,
            (int)pdfs.size(),
            DashboardStatePdfCloudRiskPolicy(m_dashboardState));
        if (MessageBoxW(m_hwnd, prompt.c_str(), L"ZenCrop", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return false;
        }
        options.cloudFullPdfConsentGranted = true;
    }

    return true;
}
