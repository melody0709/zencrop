#include "OcrModelDownloadDialog.h"

#include "core/Settings.h"
#include "core/WideStringUtils.h"
#include "ocr/model_download/OcrModelDownloadCatalog.h"
#include "ocr/model_download/OcrModelDownloadService.h"
#include "ocr/model_download/OcrModelInstaller.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwchar>
#include <string>

namespace {

constexpr UINT_PTR kRefreshTimer = 1;

struct ModelDialogArgs {
    OcrModelBundleId initialBundle = OcrModelBundleId::PpOcrV6Small;
    std::wstring initialRoot;
    OcrModelInstallResult* output = nullptr;
};

struct ModelDialogState {
    OcrModelDownloadService service;
    OcrModelInstallResult accumulatedResult;
    OcrModelInstallResult* output = nullptr;
    bool hasResult = false;
    bool completionCaptured = false;
    bool closeWhenStopped = false;
};

ModelDialogState* State(HWND dialog)
{
    return reinterpret_cast<ModelDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
}

void CompleteDialog(HWND dialog, ModelDialogState* state)
{
    if (state && state->hasResult && state->output) {
        *state->output = state->accumulatedResult;
    }
    EndDialog(dialog, state && state->hasResult ? IDOK : IDCANCEL);
}

std::wstring ControlText(HWND dialog, int id)
{
    const int length = GetWindowTextLengthW(GetDlgItem(dialog, id));
    std::wstring value(static_cast<size_t>((std::max)(length, 0)) + 1, L'\0');
    GetDlgItemTextW(dialog, id, value.data(), static_cast<int>(value.size()));
    value.resize(std::wcslen(value.c_str()));
    return value;
}

std::wstring FormatBytes(std::uint64_t bytes)
{
    wchar_t buffer[64] = {};
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        swprintf_s(buffer, L"%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        swprintf_s(buffer, L"%.1f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        swprintf_s(buffer, L"%.1f KB", bytes / 1024.0);
    } else {
        swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

OcrModelBundleId SelectedBundle(HWND dialog)
{
    const LRESULT selected = SendDlgItemMessageW(
        dialog, IDC_MODEL_DOWNLOAD_BUNDLE, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return OcrModelBundleId::PpOcrV6Small;
    const LRESULT data = SendDlgItemMessageW(
        dialog, IDC_MODEL_DOWNLOAD_BUNDLE, CB_GETITEMDATA,
        static_cast<WPARAM>(selected), 0);
    return data == CB_ERR
        ? OcrModelBundleId::PpOcrV6Small
        : static_cast<OcrModelBundleId>(data);
}

OcrModelMirrorPreference SelectedMirror(HWND dialog)
{
    const LRESULT selected = SendDlgItemMessageW(
        dialog, IDC_MODEL_DOWNLOAD_MIRROR, CB_GETCURSEL, 0, 0);
    return selected == 1
        ? OcrModelMirrorPreference::ModelScopeFirst
        : OcrModelMirrorPreference::Auto;
}

void MergeInstallResult(
    OcrModelInstallResult& target,
    const OcrModelInstallResult& source)
{
    target.modelRoot = source.modelRoot;
    target.bundle = source.bundle;
    if (!source.paddleLocalModelDir.empty()) {
        target.paddleLocalModelDir = source.paddleLocalModelDir;
    }
    if (!source.ppocrv6ModelDir.empty()) {
        target.ppocrv6ModelDir = source.ppocrv6ModelDir;
    }
    if (!source.ppocrv6Variant.empty()) {
        target.ppocrv6Variant = source.ppocrv6Variant;
    }
    if (!source.docLayoutModelPath.empty()) {
        target.docLayoutModelPath = source.docLayoutModelPath;
    }
}

bool BrowseForFolder(HWND dialog, const std::wstring& initial, std::wstring& picked)
{
    picked.clear();
    IFileOpenDialog* picker = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&picker));
    if (FAILED(hr)) return false;
    DWORD options = 0;
    picker->GetOptions(&options);
    picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    picker->SetTitle(L"Select Model Download Folder");
    if (!initial.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                initial.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            picker->SetDefaultFolder(folder);
            folder->Release();
        }
    }
    hr = picker->Show(dialog);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(picker->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                picked = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    picker->Release();
    return !picked.empty();
}

void SetControlsForActive(HWND dialog, bool active)
{
    EnableWindow(GetDlgItem(dialog, IDC_MODEL_DOWNLOAD_BUNDLE), !active);
    EnableWindow(GetDlgItem(dialog, IDC_MODEL_DOWNLOAD_LOCATION), !active);
    EnableWindow(GetDlgItem(dialog, IDC_MODEL_DOWNLOAD_BROWSE), !active);
    EnableWindow(GetDlgItem(dialog, IDC_MODEL_DOWNLOAD_START), !active);
    EnableWindow(GetDlgItem(dialog, IDC_MODEL_DOWNLOAD_CANCEL), active);
}

void UpdateSelectionStatus(HWND dialog)
{
    ModelDialogState* state = State(dialog);
    if (!state) return;
    const OcrModelBundleSpec* bundle = OcrModelDownloadFindBundle(SelectedBundle(dialog));
    if (!bundle) return;
    SetDlgItemTextW(
        dialog, IDC_MODEL_DOWNLOAD_SIZE,
        (L"Download: " + FormatBytes(OcrModelDownloadBundleBytes(*bundle)) +
         L"  |  License: " + bundle->upstreamLicense).c_str());
    const std::wstring root = ControlText(dialog, IDC_MODEL_DOWNLOAD_LOCATION);
    const bool installed = !root.empty() && state->service.BundleInstalled(bundle->id, root);
    SetDlgItemTextW(
        dialog, IDC_MODEL_DOWNLOAD_INSTALLED,
        installed ? L"Status: Installed" : L"Status: Not installed or repair required");
    SetDlgItemTextW(
        dialog, IDC_MODEL_DOWNLOAD_START,
        installed ? L"Verify / Repair" : L"Download");
}

std::wstring ErrorDetails(const OcrModelDownloadError& error)
{
    std::wstring details = error.technicalDetail;
    if (error.httpStatus != 0) {
        if (!details.empty()) details += L"\r\n";
        details += L"HTTP status: " + std::to_wstring(error.httpStatus);
    }
    if (error.win32Error != 0) {
        if (!details.empty()) details += L"\r\n";
        details += L"Win32 error: " + std::to_wstring(error.win32Error);
    }
    if (!error.artifactId.empty()) {
        if (!details.empty()) details += L"\r\n";
        details += L"Artifact: " + error.artifactId;
    }
    return details;
}

void RefreshProgress(HWND dialog)
{
    ModelDialogState* state = State(dialog);
    if (!state) return;
    const OcrModelDownloadSnapshot snapshot = state->service.Snapshot();
    const bool active = OcrModelDownloadStateIsActive(snapshot.state);
    SetControlsForActive(dialog, active);

    int progress = 0;
    if (snapshot.totalBytes > 0) {
        progress = static_cast<int>((std::min<std::uint64_t>)(
            10000ULL, snapshot.completedBytes * 10000ULL / snapshot.totalBytes));
    }
    SendDlgItemMessageW(dialog, IDC_MODEL_DOWNLOAD_PROGRESS, PBM_SETPOS, progress, 0);

    std::wstring status = snapshot.statusText;
    if (active && snapshot.totalBytes > 0) {
        status += L"  " + FormatBytes(snapshot.completedBytes) + L" / " +
            FormatBytes(snapshot.totalBytes);
        if (snapshot.bytesPerSecond > 0) {
            status += L"  (" + FormatBytes(snapshot.bytesPerSecond) + L"/s)";
        }
    }
    SetDlgItemTextW(dialog, IDC_MODEL_DOWNLOAD_STATUS, status.c_str());

    if (snapshot.state == OcrModelDownloadState::Failed) {
        SetDlgItemTextW(
            dialog, IDC_MODEL_DOWNLOAD_DETAILS,
            ErrorDetails(snapshot.error).c_str());
        SetDlgItemTextW(dialog, IDC_MODEL_DOWNLOAD_START, L"Retry");
    } else if (snapshot.state == OcrModelDownloadState::Completed) {
        if (!state->completionCaptured) {
            MergeInstallResult(state->accumulatedResult, snapshot.installResult);
            state->hasResult = true;
            state->completionCaptured = true;
        }
        SetDlgItemTextW(
            dialog, IDC_MODEL_DOWNLOAD_DETAILS,
            L"The bundle is ready. You can install another bundle or close this dialog.");
        UpdateSelectionStatus(dialog);
    } else if (snapshot.state == OcrModelDownloadState::Cancelled) {
        SetDlgItemTextW(
            dialog, IDC_MODEL_DOWNLOAD_DETAILS,
            L"Partial data is preserved and will be resumed next time.");
        SetDlgItemTextW(dialog, IDC_MODEL_DOWNLOAD_START, L"Resume");
    }

    if (!active && state->closeWhenStopped) {
        CompleteDialog(dialog, state);
        return;
    }
}

void RequestClose(HWND dialog)
{
    ModelDialogState* state = State(dialog);
    if (!state) {
        EndDialog(dialog, IDCANCEL);
        return;
    }
    const OcrModelDownloadSnapshot snapshot = state->service.Snapshot();
    if (OcrModelDownloadStateIsActive(snapshot.state)) {
        if (MessageBoxW(
                dialog,
                L"Pause the current download and close? Partial data will be kept for resume.",
                L"Manage OCR Models",
                MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        state->closeWhenStopped = true;
        state->service.Cancel();
        return;
    }
    CompleteDialog(dialog, state);
}

INT_PTR CALLBACK ModelDownloadDialogProc(
    HWND dialog,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG: {
        auto* args = reinterpret_cast<ModelDialogArgs*>(lParam);
        auto* state = new ModelDialogState();
        state->output = args ? args->output : nullptr;
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        SendDlgItemMessageW(dialog, IDC_MODEL_DOWNLOAD_PROGRESS, PBM_SETRANGE32, 0, 10000);

        int initialIndex = 0;
        int index = 0;
        for (const auto& bundle : OcrModelDownloadCatalog()) {
            const LRESULT item = SendDlgItemMessageW(
                dialog, IDC_MODEL_DOWNLOAD_BUNDLE, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(bundle.displayName.c_str()));
            SendDlgItemMessageW(
                dialog, IDC_MODEL_DOWNLOAD_BUNDLE, CB_SETITEMDATA,
                static_cast<WPARAM>(item), static_cast<LPARAM>(bundle.id));
            if (args && bundle.id == args->initialBundle) initialIndex = index;
            ++index;
        }
        SendDlgItemMessageW(
            dialog, IDC_MODEL_DOWNLOAD_BUNDLE, CB_SETCURSEL, initialIndex, 0);

        SendDlgItemMessageW(
            dialog, IDC_MODEL_DOWNLOAD_MIRROR, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"HuggingFace (ModelScope fallback)"));
        SendDlgItemMessageW(
            dialog, IDC_MODEL_DOWNLOAD_MIRROR, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"ModelScope (HuggingFace fallback)"));
        SendDlgItemMessageW(
            dialog, IDC_MODEL_DOWNLOAD_MIRROR, CB_SETCURSEL, 0, 0);

        const std::wstring root = args && !args->initialRoot.empty()
            ? args->initialRoot
            : OcrModelDefaultDownloadRoot();
        SetDlgItemTextW(dialog, IDC_MODEL_DOWNLOAD_LOCATION, root.c_str());
        EnableWindow(GetDlgItem(dialog, IDC_MODEL_DOWNLOAD_CANCEL), FALSE);
        UpdateSelectionStatus(dialog);
        SetTimer(dialog, kRefreshTimer, 100, nullptr);
        return TRUE;
    }

    case WM_TIMER:
        if (wParam == kRefreshTimer) RefreshProgress(dialog);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MODEL_DOWNLOAD_BUNDLE:
            if (HIWORD(wParam) == CBN_SELCHANGE) UpdateSelectionStatus(dialog);
            return TRUE;
        case IDC_MODEL_DOWNLOAD_LOCATION:
            if (HIWORD(wParam) == EN_KILLFOCUS) UpdateSelectionStatus(dialog);
            return TRUE;
        case IDC_MODEL_DOWNLOAD_BROWSE: {
            std::wstring picked;
            if (BrowseForFolder(
                    dialog,
                    ControlText(dialog, IDC_MODEL_DOWNLOAD_LOCATION),
                    picked)) {
                SetDlgItemTextW(dialog, IDC_MODEL_DOWNLOAD_LOCATION, picked.c_str());
                UpdateSelectionStatus(dialog);
            }
            return TRUE;
        }
        case IDC_MODEL_DOWNLOAD_START: {
            ModelDialogState* state = State(dialog);
            const std::wstring root = ControlText(dialog, IDC_MODEL_DOWNLOAD_LOCATION);
            if (!state || root.empty()) {
                MessageBoxW(
                    dialog, L"Select a model download folder first.",
                    L"Manage OCR Models", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            SetDlgItemTextW(dialog, IDC_MODEL_DOWNLOAD_DETAILS, L"");
            if (!state->service.Start(
                    SelectedBundle(dialog), root, SelectedMirror(dialog))) {
                MessageBoxW(
                    dialog, L"Another model download is already active.",
                    L"Manage OCR Models", MB_OK | MB_ICONWARNING);
            } else {
                state->completionCaptured = false;
            }
            RefreshProgress(dialog);
            return TRUE;
        }
        case IDC_MODEL_DOWNLOAD_CANCEL:
            if (ModelDialogState* state = State(dialog)) state->service.Cancel();
            return TRUE;
        case IDC_MODEL_DOWNLOAD_OPEN: {
            const std::wstring root = ControlText(dialog, IDC_MODEL_DOWNLOAD_LOCATION);
            if (!root.empty()) {
                ShellExecuteW(dialog, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return TRUE;
        }
        case IDOK:
        case IDCANCEL:
            RequestClose(dialog);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        RequestClose(dialog);
        return TRUE;

    case WM_NCDESTROY: {
        KillTimer(dialog, kRefreshTimer);
        ModelDialogState* state = State(dialog);
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        delete state;
        return TRUE;
    }
    }
    return FALSE;
}

} // namespace

bool ShowOcrModelDownloadDialog(
    HWND owner,
    OcrModelBundleId initialBundle,
    const std::wstring& initialRoot,
    OcrModelInstallResult& installResult)
{
    installResult = {};
    ModelDialogArgs args;
    args.initialBundle = initialBundle;
    args.initialRoot = initialRoot.empty() ? OcrModelDefaultDownloadRoot() : initialRoot;
    args.output = &installResult;
    const INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_OCR_MODEL_DOWNLOAD),
        owner,
        ModelDownloadDialogProc,
        reinterpret_cast<LPARAM>(&args));
    return result == IDOK;
}
