#include "ocr/ui/dashboard/DashboardFolderImportOptionsDialog.h"
#include "ocr/ui/dashboard/DashboardDialogLayout.h"
#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include "core/WideStringUtils.h"
#include "BatchOcrWriter.h"
#include "Strings.h"

#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <string>

namespace {

constexpr UINT kFolderOptionsRecursiveId = 1221;
constexpr UINT kFolderOptionsDepthId = 1222;
constexpr UINT kFolderOptionsExcludeId = 1223;
constexpr UINT kFolderOptionsOkId = 1224;
constexpr UINT kFolderOptionsCancelId = 1225;
constexpr UINT kFolderOptionsBrowseOutputId = 1226;
constexpr const wchar_t* kFolderImportOptionsDialogClass =
    L"ZenCrop.OcrDashboard.FolderImportOptions";
constexpr int kFolderImportMaxDepthLimit = 64;

struct FolderImportOptionsDialogState {
    HWND hwnd = nullptr;
    HWND titleText = nullptr;
    HWND countText = nullptr;
    HWND introText = nullptr;
    HWND recursiveCheck = nullptr;
    HWND depthLabel = nullptr;
    HWND depthEdit = nullptr;
    HWND excludeLabel = nullptr;
    HWND excludeEdit = nullptr;
    HWND hintText = nullptr;
    HWND outputLabel = nullptr;
    HWND outputText = nullptr;
    HWND outputBrowseBtn = nullptr;
    HWND okBtn = nullptr;
    HWND cancelBtn = nullptr;
    HFONT font = nullptr;
    bool ownsFont = false;
    UINT dpi = kDashboardDialogDesignDpi;
    bool recursive = true;
    int maxDepth = 16;
    std::wstring excludePatterns;
    std::wstring outputRoot;
    size_t directoryCount = 0;
    bool accepted = false;
    bool done = false;
};

bool DirectoryExistsWideLocal(const std::wstring& path)
{
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

SIZE GetFolderImportOptionsDialogClientSize(UINT dpi)
{
    SIZE size = {};
    size.cx = DashboardScaleDialogValue(760, dpi);
    size.cy = DashboardScaleDialogValue(500, dpi);
    return size;
}

SIZE GetFolderImportOptionsDialogWindowSize(UINT dpi)
{
    SIZE client = GetFolderImportOptionsDialogClientSize(dpi);
    RECT rc = { 0, 0, client.cx, client.cy };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    AdjustWindowRectExForDpi(
        &rc, style, FALSE, exStyle, dpi > 0 ? dpi : kDashboardDialogDesignDpi);
    SIZE size = {};
    size.cx = rc.right - rc.left;
    size.cy = rc.bottom - rc.top;
    return size;
}

void ApplyFolderImportOptionsDialogFont(FolderImportOptionsDialogState* state)
{
    if (!state) return;
    HWND controls[] = {
        state->titleText,
        state->countText,
        state->introText,
        state->recursiveCheck,
        state->depthLabel,
        state->depthEdit,
        state->excludeLabel,
        state->excludeEdit,
        state->hintText,
        state->outputLabel,
        state->outputText,
        state->outputBrowseBtn,
        state->okBtn,
        state->cancelBtn
    };
    for (HWND control : controls) {
        DashboardSetControlFont(control, state->font);
    }
}

int GetFolderImportOptionsDialogLineHeight(FolderImportOptionsDialogState* state)
{
    int fallback = DashboardScaleDialogValue(24, state ? state->dpi : kDashboardDialogDesignDpi);
    if (!state || !state->hwnd || !state->font) return fallback;

    HDC hdc = GetDC(state->hwnd);
    if (!hdc) return fallback;

    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, state->font));
    TEXTMETRICW tm = {};
    int lineH = fallback;
    if (GetTextMetricsW(hdc, &tm)) {
        lineH = (std::max)(lineH, (int)(tm.tmHeight + tm.tmExternalLeading));
    }
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(state->hwnd, hdc);
    return lineH;
}

std::wstring FormatFolderImportOutputText(const std::wstring& outputRoot)
{
    std::wstring trimmed = DashboardTrimWide(outputRoot);
    return trimmed.empty() ? L"Output: not selected" : L"Output: " + trimmed;
}

void UpdateFolderImportOutputSummary(FolderImportOptionsDialogState* state)
{
    if (!state || !state->outputText) return;
    SetWindowTextW(state->outputText, FormatFolderImportOutputText(state->outputRoot).c_str());
}

void LayoutFolderImportOptionsDialog(FolderImportOptionsDialogState* state)
{
    if (!state || !state->hwnd) return;

    RECT client = {};
    GetClientRect(state->hwnd, &client);
    int clientW = (std::max)(1, (int)(client.right - client.left));
    int clientH = (std::max)(1, (int)(client.bottom - client.top));

    int lineH = GetFolderImportOptionsDialogLineHeight(state);
    int margin = DashboardScaleDialogValue(22, state->dpi);
    int rowGap = DashboardScaleDialogValue(14, state->dpi);
    int smallGap = DashboardScaleDialogValue(8, state->dpi);
    int labelH = lineH + DashboardScaleDialogValue(6, state->dpi);
    int titleH = lineH + DashboardScaleDialogValue(8, state->dpi);
    int editH = (std::max)(lineH + DashboardScaleDialogValue(10, state->dpi),
        DashboardScaleDialogValue(32, state->dpi));
    int buttonW = DashboardScaleDialogValue(100, state->dpi);
    int buttonH = (std::max)(lineH + DashboardScaleDialogValue(10, state->dpi),
        DashboardScaleDialogValue(34, state->dpi));
    int buttonGap = DashboardScaleDialogValue(14, state->dpi);
    int browseW = DashboardScaleDialogValue(96, state->dpi);

    int contentW = (std::max)(DashboardScaleDialogValue(280, state->dpi), clientW - margin * 2);
    int labelW = (std::min)(DashboardScaleDialogValue(180, state->dpi),
        (std::max)(DashboardScaleDialogValue(120, state->dpi), contentW / 3));
    int editX = margin + labelW + DashboardScaleDialogValue(12, state->dpi);
    int editW = (std::max)(DashboardScaleDialogValue(160, state->dpi), clientW - margin - editX);
    int narrowEditW = (std::min)(DashboardScaleDialogValue(96, state->dpi), editW);

    int titleY = margin;
    int countY = titleY + titleH + DashboardScaleDialogValue(2, state->dpi);
    int introY = countY + labelH + DashboardScaleDialogValue(12, state->dpi);
    int introH = lineH * 2 + DashboardScaleDialogValue(8, state->dpi);
    int recursiveY = introY + introH + rowGap;
    int depthY = recursiveY + labelH + rowGap;
    int excludeY = depthY + editH + rowGap;
    int hintY = excludeY + editH + smallGap;
    int outputY = hintY + labelH + rowGap;
    int naturalButtonY = outputY + buttonH + DashboardScaleDialogValue(24, state->dpi);
    int buttonY = (std::max)(margin, (std::min)(naturalButtonY, clientH - margin - buttonH));

    MoveWindow(state->titleText, margin, titleY, contentW, titleH, TRUE);
    MoveWindow(state->countText, margin, countY, contentW, labelH, TRUE);
    MoveWindow(state->introText, margin, introY, contentW, introH, TRUE);
    MoveWindow(state->recursiveCheck, margin, recursiveY, contentW, labelH, TRUE);
    MoveWindow(state->depthLabel, margin, depthY + DashboardScaleDialogValue(4, state->dpi), labelW, labelH, TRUE);
    MoveWindow(state->depthEdit, editX, depthY, narrowEditW, editH, TRUE);
    MoveWindow(state->excludeLabel, margin, excludeY + DashboardScaleDialogValue(4, state->dpi), labelW, labelH, TRUE);
    MoveWindow(state->excludeEdit, editX, excludeY, editW, editH, TRUE);
    MoveWindow(state->hintText, editX, hintY, editW, labelH, TRUE);
    MoveWindow(state->outputLabel, margin, outputY + DashboardScaleDialogValue(2, state->dpi), labelW, labelH, TRUE);
    int browseX = clientW - margin - browseW;
    int outputTextW = (std::max)(DashboardScaleDialogValue(160, state->dpi), browseX - buttonGap - editX);
    MoveWindow(state->outputText, editX, outputY + DashboardScaleDialogValue(2, state->dpi), outputTextW, labelH, TRUE);
    MoveWindow(state->outputBrowseBtn, browseX, outputY, browseW, buttonH, TRUE);

    int cancelX = clientW - margin - buttonW;
    int okX = cancelX - buttonGap - buttonW;
    MoveWindow(state->okBtn, okX, buttonY, buttonW, buttonH, TRUE);
    MoveWindow(state->cancelBtn, cancelX, buttonY, buttonW, buttonH, TRUE);
}

LRESULT CALLBACK FolderImportOptionsDialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FolderImportOptionsDialogState* state = reinterpret_cast<FolderImportOptionsDialogState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<FolderImportOptionsDialogState*>(cs ? cs->lpCreateParams : nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state) return -1;

        state->hwnd = hwnd;
        state->titleText = CreateWindowExW(
            0, L"STATIC", L"Folder import",
            WS_CHILD | WS_VISIBLE,
            0, 0, 1, 1,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        size_t shownCount = state->directoryCount > 0 ? state->directoryCount : 1;
        std::wstring countText = L"Folders: " + WideFormatIntLabel((int)shownCount);
        state->countText = CreateWindowExW(
            0, L"STATIC", countText.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 1, 1,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        const bool zh = S::IsChinese();
        int margin = DashboardScaleDialogValue(18, state->dpi);
        int editH = DashboardScaleDialogValue(26, state->dpi);
        int labelW = DashboardScaleDialogValue(430, state->dpi);

        std::wstring intro = zh
            ? L"检测到文件夹来源。请选择扫描方式；这些设置会保存到下次导入。"
            : L"Folder sources detected. Choose how folders are scanned; these settings are saved for next time.";
        if (state->directoryCount > 1) {
            intro += zh ? L"\r\n文件夹数量：" : L"\r\nFolders: ";
            intro += WideFormatIntLabel((int)state->directoryCount);
        }
        state->introText = CreateWindowExW(
            0, L"STATIC", intro.c_str(),
            WS_CHILD | WS_VISIBLE,
            margin, DashboardScaleDialogValue(14, state->dpi),
            labelW, DashboardScaleDialogValue(48, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->introText, state->font);

        state->recursiveCheck = CreateWindowExW(
            0, L"BUTTON", zh ? L"包含子文件夹" : L"Include subfolders",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            margin, DashboardScaleDialogValue(68, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFolderOptionsRecursiveId)),
            GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->recursiveCheck, state->font);
        SendMessageW(state->recursiveCheck, BM_SETCHECK, state->recursive ? BST_CHECKED : BST_UNCHECKED, 0);

        state->depthLabel = CreateWindowExW(
            0, L"STATIC", zh ? L"最大递归深度 (0-64):" : L"Max recursion depth (0-64):",
            WS_CHILD | WS_VISIBLE,
            margin, DashboardScaleDialogValue(104, state->dpi),
            DashboardScaleDialogValue(180, state->dpi), DashboardScaleDialogValue(20, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->depthLabel, state->font);

        state->depthEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT",
            WideFormatIntLabel(DashboardNormalizeFolderImportDepth(state->maxDepth)).c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            DashboardScaleDialogValue(206, state->dpi), DashboardScaleDialogValue(100, state->dpi),
            DashboardScaleDialogValue(80, state->dpi), editH,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFolderOptionsDepthId)),
            GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->depthEdit, state->font);
        EnableWindow(state->depthEdit, state->recursive);

        state->excludeLabel = CreateWindowExW(
            0, L"STATIC",
            zh ? L"排除目录名/通配符（用 ; 分隔）:" : L"Excluded folder names/globs (; separated):",
            WS_CHILD | WS_VISIBLE,
            margin, DashboardScaleDialogValue(140, state->dpi),
            labelW, DashboardScaleDialogValue(20, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->excludeLabel, state->font);

        state->excludeEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", state->excludePatterns.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            margin, DashboardScaleDialogValue(166, state->dpi),
            labelW, editH,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFolderOptionsExcludeId)),
            GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->excludeEdit, state->font);

        state->hintText = CreateWindowExW(
            0, L"STATIC",
            zh ? L"示例：.git;node_modules;build;tmp_*" : L"Example: .git;node_modules;build;tmp_*",
            WS_CHILD | WS_VISIBLE,
            margin, DashboardScaleDialogValue(198, state->dpi),
            labelW, DashboardScaleDialogValue(22, state->dpi),
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->hintText, state->font);

        state->outputLabel = CreateWindowExW(
            0, L"STATIC", L"Output",
            WS_CHILD | WS_VISIBLE,
            0, 0, 1, 1,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->outputLabel, state->font);

        std::wstring outputText = FormatFolderImportOutputText(state->outputRoot);
        state->outputText = CreateWindowExW(
            0, L"STATIC", outputText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
            0, 0, 1, 1,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->outputText, state->font);

        state->outputBrowseBtn = CreateWindowExW(
            0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 1, 1,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFolderOptionsBrowseOutputId)),
            GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->outputBrowseBtn, state->font);

        int buttonY = DashboardScaleDialogValue(236, state->dpi);
        int buttonW = DashboardScaleDialogValue(92, state->dpi);
        int buttonH = DashboardScaleDialogValue(30, state->dpi);
        state->okBtn = CreateWindowExW(
            0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            DashboardScaleDialogValue(268, state->dpi), buttonY, buttonW, buttonH,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFolderOptionsOkId)),
            GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->okBtn, state->font);
        state->cancelBtn = CreateWindowExW(
            0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            DashboardScaleDialogValue(370, state->dpi), buttonY, buttonW, buttonH,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFolderOptionsCancelId)),
            GetModuleHandleW(nullptr), nullptr);
        DashboardSetControlFont(state->cancelBtn, state->font);

        SetWindowTextW(state->introText,
            L"Choose how folders are scanned. These settings are saved for next import.");
        SetWindowTextW(state->depthLabel, L"Max depth (0-64)");
        SetWindowTextW(state->excludeLabel, L"Exclude folders");
        SetWindowTextW(state->hintText, L"Example: .git;node_modules;build;tmp_*");
        SendMessageW(state->excludeEdit, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L".git;node_modules;build;tmp_*"));
        ApplyFolderImportOptionsDialogFont(state);
        LayoutFolderImportOptionsDialog(state);
        DashboardCenterWindowOnOwner(hwnd, GetWindow(hwnd, GW_OWNER));
        SetFocus(state->recursiveCheck);
        return 0;
    }
    case WM_SIZE:
        if (state) {
            LayoutFolderImportOptionsDialog(state);
        }
        return 0;
    case WM_DPICHANGED:
        if (state) {
            UINT newDpi = HIWORD(wParam);
            if (newDpi > 0 && newDpi != state->dpi) {
                state->dpi = newDpi;
                HFONT oldFont = state->ownsFont ? state->font : nullptr;
                HFONT nextFont = DashboardCreateDialogFont(20, state->dpi);
                if (nextFont) {
                    state->font = nextFont;
                    state->ownsFont = true;
                    if (oldFont) DeleteObject(oldFont);
                    ApplyFolderImportOptionsDialogFont(state);
                }
            }
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SIZE nextSize = {
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top
                };
                nextSize = DashboardClampDialogSizeToWorkArea(nextSize, hwnd, state->dpi);
                SetWindowPos(hwnd, nullptr,
                    suggested->left,
                    suggested->top,
                    nextSize.cx,
                    nextSize.cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            LayoutFolderImportOptionsDialog(state);
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_COMMAND:
        if (!state) break;
        switch (LOWORD(wParam)) {
        case kFolderOptionsRecursiveId: {
            bool recursive = IsDlgButtonChecked(hwnd, kFolderOptionsRecursiveId) == BST_CHECKED;
            if (state->depthEdit) EnableWindow(state->depthEdit, recursive);
            return 0;
        }
        case kFolderOptionsBrowseOutputId: {
            std::wstring selectedRoot;
            if (DashboardSelectOcrOptionsOutputRoot(
                    hwnd, state->outputRoot, L"Choose OCR output folder", selectedRoot)) {
                state->outputRoot = selectedRoot;
                UpdateFolderImportOutputSummary(state);
                LayoutFolderImportOptionsDialog(state);
            }
            return 0;
        }
        case kFolderOptionsOkId: {
            bool recursive = IsDlgButtonChecked(hwnd, kFolderOptionsRecursiveId) == BST_CHECKED;
            int depth = 0;
            if (recursive) {
                std::wstring depthText = DashboardTrimWide(
                    DashboardGetWindowTextWide(state->depthEdit));
                if (depthText.empty()) {
                    MessageBoxW(hwnd, L"Max recursion depth is required.", L"ZenCrop",
                        MB_OK | MB_ICONWARNING);
                    return 0;
                }
                depth = WideParseJsonIntToken(depthText);
                if (depth < 0 || depth > kFolderImportMaxDepthLimit) {
                    MessageBoxW(hwnd, L"Max recursion depth must be between 0 and 64.", L"ZenCrop",
                        MB_OK | MB_ICONWARNING);
                    return 0;
                }
            }
            std::wstring outputRoot = DashboardTrimWide(state->outputRoot);
            if (outputRoot.empty()) {
                MessageBoxW(hwnd, L"Output folder is required.", L"ZenCrop",
                    MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!BatchOcrWriter::EnsureDirectory(outputRoot)) {
                MessageBoxW(hwnd, L"Failed to create the selected output folder.", L"ZenCrop",
                    MB_OK | MB_ICONWARNING);
                return 0;
            }
            state->recursive = recursive;
            state->maxDepth = recursive ? depth : 0;
            state->excludePatterns = DashboardTrimWide(
                DashboardGetWindowTextWide(state->excludeEdit));
            state->outputRoot = outputRoot;
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        case kFolderOptionsCancelId:
            state->accepted = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state) {
            state->accepted = false;
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (state) {
            state->hwnd = nullptr;
            state->done = true;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterFolderImportOptionsDialogClass()
{
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = FolderImportOptionsDialogWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wcex.lpszClassName = kFolderImportOptionsDialogClass;
    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}

}  // namespace

bool DashboardSelectOcrOptionsOutputRoot(
    HWND owner,
    const std::wstring& currentRoot,
    const wchar_t* title,
    std::wstring& outputRoot)
{
    outputRoot.clear();

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(title && *title
        ? title
        : (S::IsChinese() ? L"选择 PDF OCR 输出目录" : L"Choose PDF OCR output folder"));

    if (!currentRoot.empty() && DirectoryExistsWideLocal(currentRoot)) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(currentRoot.c_str(), nullptr, IID_PPV_ARGS(&folder)))
            && folder) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    bool selected = false;
    if (SUCCEEDED(dialog->Show(owner))) {
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

DashboardFolderImportOptionsRun DashboardRunFolderImportOptionsDialog(
    HWND owner,
    UINT dpi,
    HFONT font,
    size_t directoryCount,
    bool recursiveSeed,
    int maxDepthSeed,
    const std::wstring& excludeSeed,
    const std::wstring& outputRootSeed)
{
    DashboardFolderImportOptionsRun run;

    if (!RegisterFolderImportOptionsDialogClass()) {
        run.dialogFailedOpen = true;
        return run;
    }

    FolderImportOptionsDialogState state;
    state.dpi = dpi > 0 ? dpi : kDashboardDialogDesignDpi;
    state.font = font ? font : DashboardCreateDialogFont(20, state.dpi);
    state.ownsFont = state.font && state.font != font;
    if (!state.font) {
        state.font = font;
        state.ownsFont = false;
    }
    state.recursive = recursiveSeed;
    state.maxDepth = DashboardNormalizeFolderImportDepth(maxDepthSeed);
    state.excludePatterns = excludeSeed;
    state.outputRoot = outputRootSeed;
    state.directoryCount = directoryCount;

    SIZE dialogSize = DashboardClampDialogSizeToWorkArea(
        GetFolderImportOptionsDialogWindowSize(state.dpi), owner, state.dpi);
    int w = dialogSize.cx;
    int h = dialogSize.cy;
    BOOL parentWasEnabled = owner ? IsWindowEnabled(owner) : FALSE;
    if (owner) EnableWindow(owner, FALSE);

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kFolderImportOptionsDialogClass,
        S::IsChinese() ? L"文件夹导入选项" : L"Folder import options",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        owner, nullptr, GetModuleHandleW(nullptr), &state);

    if (!dialog) {
        if (owner && parentWasEnabled) EnableWindow(owner, TRUE);
        if (state.ownsFont && state.font) DeleteObject(state.font);
        run.dialogFailedOpen = true;
        return run;
    }

    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg = {};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (state.hwnd && IsWindow(state.hwnd)) {
        DestroyWindow(state.hwnd);
    }
    if (owner && parentWasEnabled) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (state.ownsFont && state.font) DeleteObject(state.font);

    run.result.accepted = state.accepted;
    run.result.recursive = state.recursive;
    run.result.maxDepth = state.maxDepth;
    run.result.excludePatterns = state.excludePatterns;
    run.result.outputRoot = state.outputRoot;
    return run;
}
