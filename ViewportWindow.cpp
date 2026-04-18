#include "ViewportWindow.h"
#include "AlwaysOnTop.h"

HRGN ViewportWindow::DuplicateRegion(HRGN region) {
    if (!region) return nullptr;

    HRGN copy = CreateRectRgn(0, 0, 0, 0);
    if (!copy) return nullptr;

    if (CombineRgn(copy, region, nullptr, RGN_COPY) == ERROR) {
        DeleteObject(copy);
        return nullptr;
    }

    return copy;
}

HRGN ViewportWindow::CaptureWindowRegion(HWND hwnd, bool& hadRegion) {
    hadRegion = false;

    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (!region) return nullptr;

    int result = GetWindowRgn(hwnd, region);
    if (result == ERROR) {
        DeleteObject(region);
        return nullptr;
    }

    hadRegion = true;
    return region;
}

ViewportWindow::ViewportWindow(HWND targetWindow, RECT cropRect, bool pinOnTop)
    : m_targetWindow(targetWindow) {
    if (!m_targetWindow || !IsWindow(m_targetWindow)) return;

    SaveOriginalState();
    PreprocessMaximizedWindow();
    ApplyViewport(cropRect);
    ApplyPinState(pinOnTop);
}

ViewportWindow::~ViewportWindow() {
    RestoreOriginalState();

    if (m_originalRegion) {
        DeleteObject(m_originalRegion);
        m_originalRegion = nullptr;
    }
}

void ViewportWindow::SaveOriginalState() {
    m_originalStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    m_originalExStyle = GetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE);
    GetWindowRect(m_targetWindow, &m_originalRect);

    m_originalPlacement = { sizeof(WINDOWPLACEMENT) };
    if (GetWindowPlacement(m_targetWindow, &m_originalPlacement)) {
        m_wasMaximized = (m_originalPlacement.showCmd == SW_SHOWMAXIMIZED);
    }

    m_originalRegion = CaptureWindowRegion(m_targetWindow, m_hadOriginalRegion);
}

void ViewportWindow::PreprocessMaximizedWindow() {
    if (!m_wasMaximized) return;

    LONG_PTR style = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    style &= ~WS_MAXIMIZE;
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, style);

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(m_targetWindow, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowPos(m_targetWindow, nullptr,
        mi.rcWork.left, mi.rcWork.top,
        mi.rcWork.right - mi.rcWork.left,
        mi.rcWork.bottom - mi.rcWork.top,
        SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void ViewportWindow::ApplyPinState(bool pinOnTop) {
    if (!pinOnTop || !m_active || !m_targetWindow || !IsWindow(m_targetWindow)) return;

    auto& manager = AlwaysOnTopManager::Instance();
    if (!manager.IsPinned(m_targetWindow)) {
        manager.PinWindow(m_targetWindow);
        m_pinnedByViewport = true;
    }
}

void ViewportWindow::ApplyViewport(RECT cropRect) {
    POINT oldClientPt = {0, 0};
    ClientToScreen(m_targetWindow, &oldClientPt);
    RECT oldWindowRect = {};
    GetWindowRect(m_targetWindow, &oldWindowRect);

    // Strip caption and borders before applying region so DWM/FrameHost 
    // doesn't draw a new title bar at the top of the cropped region
    LONG_PTR style = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME);
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, style);
    SetWindowPos(m_targetWindow, nullptr, 0, 0, 0, 0, 
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    POINT newClientPt = {0, 0};
    ClientToScreen(m_targetWindow, &newClientPt);
    RECT currentRect = {};
    if (!GetWindowRect(m_targetWindow, &currentRect)) return;

    int clientOffsetX = cropRect.left - oldClientPt.x;
    int clientOffsetY = cropRect.top - oldClientPt.y;

    int cropInWindowLeft = clientOffsetX + (newClientPt.x - currentRect.left);
    int cropInWindowTop = clientOffsetY + (newClientPt.y - currentRect.top);
    int cropWidth = cropRect.right - cropRect.left;
    int cropHeight = cropRect.bottom - cropRect.top;

    if (cropWidth <= 0 || cropHeight <= 0) {
        return;
    }

    HRGN finalRegion = CreateRectRgn(cropInWindowLeft, cropInWindowTop, cropInWindowLeft + cropWidth, cropInWindowTop + cropHeight);
    if (!finalRegion) return;

    if (m_hadOriginalRegion && m_originalRegion) {
        int shiftX = (newClientPt.x - currentRect.left) - (oldClientPt.x - oldWindowRect.left);
        int shiftY = (newClientPt.y - currentRect.top) - (oldClientPt.y - oldWindowRect.top);
        
        HRGN shiftedOriginal = DuplicateRegion(m_originalRegion);
        if (shiftedOriginal) {
            OffsetRgn(shiftedOriginal, shiftX, shiftY);
            if (CombineRgn(finalRegion, finalRegion, shiftedOriginal, RGN_AND) == ERROR) {
                DeleteObject(finalRegion);
                DeleteObject(shiftedOriginal);
                return;
            }
            DeleteObject(shiftedOriginal);
        }
    }

    RECT regionBounds = {};
    int regionType = GetRgnBox(finalRegion, &regionBounds);
    if (regionType == ERROR || regionType == NULLREGION) {
        DeleteObject(finalRegion);
        return;
    }

    int width = currentRect.right - currentRect.left;
    int height = currentRect.bottom - currentRect.top;
    int newLeft = cropRect.left - regionBounds.left;
    int newTop = cropRect.top - regionBounds.top;

    if (SetWindowRgn(m_targetWindow, finalRegion, TRUE) == 0) {
        DeleteObject(finalRegion);
        return;
    }

    SetWindowPos(m_targetWindow, nullptr,
        newLeft, newTop, width, height,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    m_active = true;
}

void ViewportWindow::RestoreOriginalState() {
    if (m_restored) return;
    m_restored = true;
    m_active = false;

    if (m_pinnedByViewport) {
        AlwaysOnTopManager::Instance().UnpinWindow(m_targetWindow);
        m_pinnedByViewport = false;
    }

    if (!m_targetWindow || !IsWindow(m_targetWindow)) return;

    HRGN restoreRegion = nullptr;
    if (m_hadOriginalRegion && m_originalRegion) {
        restoreRegion = DuplicateRegion(m_originalRegion);
    }
    SetWindowRgn(m_targetWindow, restoreRegion, TRUE);

    SetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE, m_originalExStyle);
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, m_originalStyle);

    if (m_wasMaximized) {
        RECT& rc = m_originalPlacement.rcNormalPosition;
        SetWindowPos(m_targetWindow, nullptr,
            rc.left, rc.top,
            rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        int width = m_originalRect.right - m_originalRect.left;
        int height = m_originalRect.bottom - m_originalRect.top;
        SetWindowPos(m_targetWindow, nullptr,
            m_originalRect.left, m_originalRect.top,
            width, height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    WINDOWPLACEMENT placement = m_originalPlacement;
    if (placement.showCmd != SW_SHOWMAXIMIZED) {
        placement.showCmd = SW_RESTORE;
    }
    SetWindowPlacement(m_targetWindow, &placement);

    SetWindowPos(m_targetWindow, nullptr, 0, 0, 0, 0,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
}
