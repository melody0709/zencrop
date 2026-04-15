#include "SmartDetector.h"
#include "Utils.h"

SmartDetector& SmartDetector::Instance() {
    static SmartDetector instance;
    return instance;
}

SmartDetector::~SmartDetector() {
    Shutdown();
}

bool SmartDetector::Initialize() {
    if (m_initialized) return true;

    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
        IID_IUIAutomation, (void**)&m_pAutomation);

    if (SUCCEEDED(hr) && m_pAutomation) {
        m_initialized = true;
        return true;
    }
    m_pAutomation = nullptr;
    return false;
}

void SmartDetector::Shutdown() {
    if (m_pAutomation) {
        m_pAutomation->Release();
        m_pAutomation = nullptr;
    }
    m_initialized = false;
}

bool SmartDetector::GetElementRectAtPoint(POINT pt, RECT& outRect, HWND excludeHwnd) {
    if (!m_initialized || !m_pAutomation) return false;

    IUIAutomationElement* pElement = nullptr;
    HRESULT hr = m_pAutomation->ElementFromPoint(pt, &pElement);
    if (FAILED(hr) || !pElement) return false;

    RECT rect = {};
    hr = pElement->get_CurrentBoundingRectangle(&rect);
    if (FAILED(hr)) {
        pElement->Release();
        return false;
    }

    if (excludeHwnd) {
        UIA_HWND nativeHwnd = nullptr;
        hr = pElement->get_CurrentNativeWindowHandle(&nativeHwnd);
        if (SUCCEEDED(hr) && (HWND)nativeHwnd == excludeHwnd) {
            pElement->Release();
            return false;
        }
    }

    pElement->Release();

    if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) {
        return false;
    }

    outRect = rect;
    return true;
}

bool SmartDetector::GetChildWindowRectAtPoint(HWND hwnd, POINT screenPt, RECT& outRect) {
    if (!hwnd) return false;

    POINT localPt = screenPt;
    ScreenToClient(hwnd, &localPt);

    HWND child = RealChildWindowFromPoint(hwnd, localPt);
    if (!child || child == hwnd) return false;

    RECT childRect = {};
    if (!GetWindowRect(child, &childRect)) return false;

    RECT parentClient = GetClientRectInScreenSpace(hwnd);

    RECT clamped = childRect;
    if (clamped.left < parentClient.left) clamped.left = parentClient.left;
    if (clamped.top < parentClient.top) clamped.top = parentClient.top;
    if (clamped.right > parentClient.right) clamped.right = parentClient.right;
    if (clamped.bottom > parentClient.bottom) clamped.bottom = parentClient.bottom;

    if (clamped.right - clamped.left <= 0 || clamped.bottom - clamped.top <= 0) {
        return false;
    }

    outRect = clamped;
    return true;
}
