#include "SettingsDialogInternal.h"
#include "SettingsPageInit.h"
#include "core/ResourceIds.h"

#include <commctrl.h>
#include <shobjidl.h>
#include <algorithm>
#include <string>

namespace settings_ui {
SettingsHotkeyDraft* AttachHotkeyDraft(HWND page, LPARAM initParam) {
    const auto* pageInit = reinterpret_cast<const SettingsPageInit*>(initParam);
    auto* draft = pageInit ? pageInit->hotkeyDraft : nullptr;
    if (draft) {
        SetPropW(page, kSettingsHotkeyDraftProperty, reinterpret_cast<HANDLE>(draft));
    }
    return draft;
}

SettingsHotkeyDraft* HotkeyDraftForPage(HWND page) {
    return reinterpret_cast<SettingsHotkeyDraft*>(
        GetPropW(page, kSettingsHotkeyDraftProperty));
}

const HotkeySettings& DraftHotkeysOrShared(const SettingsHotkeyDraft* draft) {
    return draft ? draft->hotkeys : GetSharedSettings().hotkeys;
}

int ReadDlgIntClamped(HWND hPage, int id, int fallback, int minValue, int maxValue) {
    BOOL ok = FALSE;
    int value = GetDlgItemInt(hPage, id, &ok, FALSE);
    if (!ok) value = fallback;
    if (value < minValue) value = minValue;
    if (value > maxValue) value = maxValue;
    return value;
}

void UpdatePageScroll(HWND hPage, int contentHeight) {
    RECT rc;
    GetClientRect(hPage, &rc);
    int clientHeight = rc.bottom - rc.top;

    bool needScroll = (contentHeight > clientHeight);
    ShowScrollBar(hPage, SB_VERT, needScroll);

    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = contentHeight;
    si.nPage = clientHeight > 0 ? (UINT)clientHeight : 0;
    SetScrollInfo(hPage, SB_VERT, &si, TRUE);

    // Relayout placed every child at its unscrolled coordinate. Restore the
    // scrollbar's retained position to the children as well.
    si.fMask = SIF_POS;
    GetScrollInfo(hPage, SB_VERT, &si);
    const int maxPos = needScroll ? (std::max)(0, contentHeight - clientHeight) : 0;
    const int position = (std::clamp)(si.nPos, 0, maxPos);
    if (position != si.nPos) {
        si.nPos = position;
        SetScrollInfo(hPage, SB_VERT, &si, TRUE);
    }
    if (position > 0) {
        ScrollWindowEx(hPage, 0, -position, nullptr, nullptr, nullptr, nullptr,
            SW_ERASE | SW_INVALIDATE | SW_SCROLLCHILDREN);
    }
}

void RestorePageScroll(HWND page, int requestedPosition) {
    SCROLLINFO si = { sizeof(si), SIF_ALL };
    if (!GetScrollInfo(page, SB_VERT, &si)) return;
    const int target = (std::clamp)(requestedPosition, 0,
        (std::max)(0, si.nMax - static_cast<int>(si.nPage)));
    const int previous = si.nPos;
    if (target == previous) return;
    si.fMask = SIF_POS;
    si.nPos = target;
    SetScrollInfo(page, SB_VERT, &si, TRUE);
    GetScrollInfo(page, SB_VERT, &si);
    ScrollWindowEx(page, 0, previous - si.nPos, nullptr, nullptr,
        nullptr, nullptr, SW_ERASE | SW_INVALIDATE | SW_SCROLLCHILDREN);
}

void HandlePageVScroll(HWND hPage, WPARAM wParam) {
    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_ALL;
    GetScrollInfo(hPage, SB_VERT, &si);
    int curPos = si.nPos;

    switch (LOWORD(wParam)) {
    case SB_TOP: si.nPos = si.nMin; break;
    case SB_BOTTOM: si.nPos = si.nMax; break;
    case SB_LINEUP: si.nPos -= 20; break;
    case SB_LINEDOWN: si.nPos += 20; break;
    case SB_PAGEUP: si.nPos -= si.nPage; break;
    case SB_PAGEDOWN: si.nPos += si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        si.nPos = si.nTrackPos;
        break;
    default: break;
    }

    int maxPos = (std::max)(0, si.nMax - (int)si.nPage);
    si.nPos = (std::clamp)(si.nPos, 0, maxPos);

    if (si.nPos != curPos) {
        int dy = curPos - si.nPos;
        ScrollWindowEx(hPage, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_ERASE | SW_INVALIDATE | SW_SCROLLCHILDREN);
        si.fMask = SIF_POS;
        SetScrollInfo(hPage, SB_VERT, &si, TRUE);
        UpdateWindow(hPage);
    }
}

void HandlePageMouseWheel(HWND hPage, short delta) {
    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_ALL;
    GetScrollInfo(hPage, SB_VERT, &si);
    int curPos = si.nPos;

    int lines = delta / WHEEL_DELTA;
    si.nPos -= lines * 30;

    int maxPos = (std::max)(0, si.nMax - (int)si.nPage);
    si.nPos = (std::clamp)(si.nPos, 0, maxPos);

    if (si.nPos != curPos) {
        int dy = curPos - si.nPos;
        ScrollWindowEx(hPage, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_ERASE | SW_INVALIDATE | SW_SCROLLCHILDREN);
        si.fMask = SIF_POS;
        SetScrollInfo(hPage, SB_VERT, &si, TRUE);
        UpdateWindow(hPage);
    }
}

LRESULT CALLBACK PageScrollSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_VSCROLL:
        HandlePageVScroll(hWnd, wParam);
        return 0;
    case WM_MOUSEWHEEL:
        HandlePageMouseWheel(hWnd, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_CTLCOLORSTATIC: {
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        int id = GetDlgCtrlID(hCtrl);
        if (id == IDC_TRANSLATE_SELECTION_COPY_HINT || GetPropW(hCtrl, L"ZenCropHint")) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(110, 110, 110));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, PageScrollSubclassProc, uIdSubclass);
        RemovePropW(hWnd, kSettingsStateProp);
        RemovePropW(hWnd, kSettingsHotkeyDraftProperty);
        RemovePropW(hWnd, kSettingsOcrPendingProp);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

bool BrowseFolderForSettings(HWND owner, const std::wstring& initialDir, const std::wstring& title, std::wstring& resultPath) {
    struct FolderPickParams {
        HWND hOwner = nullptr;
        std::wstring initialDir;
        std::wstring title;
        std::wstring resultPath;
        bool picked = false;
    };

    FolderPickParams params;
    params.hOwner = owner;
    params.initialDir = initialDir;
    params.title = title;

    auto threadFunc = [](LPVOID p) -> DWORD {
        auto* pp = (FolderPickParams*)p;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IFileOpenDialog* pDlg = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
        if (SUCCEEDED(hr)) {
            DWORD flags = 0;
            pDlg->GetOptions(&flags);
            pDlg->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            pDlg->SetTitle(pp->title.empty() ? L"Select Folder" : pp->title.c_str());

            if (!pp->initialDir.empty()) {
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(pp->initialDir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                    pDlg->SetDefaultFolder(folder);
                    folder->Release();
                }
            }

            hr = pDlg->Show(pp->hOwner);
            if (SUCCEEDED(hr)) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(pDlg->GetResult(&item))) {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        pp->resultPath = pszPath;
                        pp->picked = true;
                        CoTaskMemFree(pszPath);
                    }
                    item->Release();
                }
            }
            pDlg->Release();
        }

        CoUninitialize();
        return 0;
    };

    HANDLE hThread = CreateThread(nullptr, 0, threadFunc, &params, 0, nullptr);
    if (!hThread) return false;

    while (true) {
        DWORD waitResult = MsgWaitForMultipleObjects(1, &hThread, FALSE, INFINITE, QS_ALLINPUT);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            // The worker still owns params until it exits, even if the message
            // wait failed unexpectedly.
            WaitForSingleObject(hThread, INFINITE);
            break;
        }
    }
    CloseHandle(hThread);

    if (!params.picked) return false;
    resultPath = params.resultPath;
    return true;
}

void RelayoutPageForTab(SettingsState* state, int tabIndex, UINT dpi) {
    if (!state || tabIndex < 0 || tabIndex >= 6 || !state->m_pages[tabIndex]) return;
    HWND hPage = state->m_pages[tabIndex];
    switch (tabIndex) {
    case 0: RelayoutGeneralPage(hPage, dpi); break;
    case 1: RelayoutZenCropPage(hPage, dpi); break;
    case 2: RelayoutAotPage(hPage, dpi); break;
    case 3: RelayoutOcrPage(hPage, dpi); break;
    case 4: RelayoutScreenshotPage(hPage, dpi); break;
    case 5: RelayoutTranslatePage(hPage, dpi); break;
    }
}

} // namespace settings_ui
