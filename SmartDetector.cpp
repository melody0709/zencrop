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

    if (FAILED(hr) || !m_pAutomation) {
        m_pAutomation = nullptr;
        return false;
    }

    m_pAutomation->get_ControlViewWalker(&m_pWalker);

    m_initialized = true;
    return true;
}

void SmartDetector::Shutdown() {
    if (m_pWalker) {
        m_pWalker->Release();
        m_pWalker = nullptr;
    }
    if (m_pAutomation) {
        m_pAutomation->Release();
        m_pAutomation = nullptr;
    }
    m_initialized = false;
}

bool SmartDetector::IsContainerControlType(int ct) {
    return ct == UIA_DocumentControlTypeId ||
           ct == UIA_EditControlTypeId ||
           ct == UIA_PaneControlTypeId ||
           ct == UIA_GroupControlTypeId ||
           ct == UIA_TabItemControlTypeId ||
           ct == UIA_ListControlTypeId ||
           ct == UIA_DataGridControlTypeId ||
           ct == UIA_TreeControlTypeId;
}

bool SmartDetector::IsStopControlType(int ct) {
    return ct == UIA_WindowControlTypeId ||
           ct == UIA_TitleBarControlTypeId;
}

bool SmartDetector::IsSmallControlType(int ct) {
    return ct == UIA_TextControlTypeId ||
           ct == UIA_ButtonControlTypeId ||
           ct == UIA_ListItemControlTypeId ||
           ct == UIA_TabControlTypeId ||
           ct == UIA_ToolBarControlTypeId ||
           ct == UIA_MenuBarControlTypeId ||
           ct == UIA_StatusBarControlTypeId ||
           ct == UIA_ImageControlTypeId ||
           ct == UIA_HyperlinkControlTypeId ||
           ct == UIA_CheckBoxControlTypeId ||
           ct == UIA_RadioButtonControlTypeId ||
           ct == UIA_ComboBoxControlTypeId ||
           ct == UIA_SeparatorControlTypeId;
}

bool SmartDetector::FindMeaningfulAncestor(IUIAutomationElement* pLeaf, RECT& outRect, const RECT& clientRect) {
    if (!m_pWalker) return false;

    long long clientArea = (long long)(clientRect.right - clientRect.left) *
                           (long long)(clientRect.bottom - clientRect.top);
    if (clientArea <= 0) return false;

    IUIAutomationElement* pNode = pLeaf;
    pNode->AddRef();

    RECT bestRect = {};
    bool hasBest = false;

    for (int depth = 0; depth < 10; depth++) {
        CONTROLTYPEID ct = 0;
        pNode->get_CurrentControlType(&ct);

        RECT rect = {};
        pNode->get_CurrentBoundingRectangle(&rect);

        long long elemArea = (long long)(rect.right - rect.left) *
                             (long long)(rect.bottom - rect.top);
        double ratio = clientArea > 0 ? (double)elemArea / clientArea : 0;

        if (IsStopControlType(ct)) {
            break;
        }

        if (IsContainerControlType(ct) && ratio >= 0.05) {
            bestRect = rect;
            hasBest = true;
            if (ct == UIA_DocumentControlTypeId || ct == UIA_EditControlTypeId) {
                pNode->Release();
                pNode = nullptr;
                break;
            }
        }

        IUIAutomationElement* pParent = nullptr;
        HRESULT hr = m_pWalker->GetParentElement(pNode, &pParent);
        pNode->Release();

        if (FAILED(hr) || !pParent) {
            pNode = nullptr;
            break;
        }
        pNode = pParent;
    }

    if (pNode) pNode->Release();

    if (hasBest) {
        outRect = bestRect;
        return true;
    }
    return false;
}

bool SmartDetector::GetElementRectAtPoint(POINT pt, RECT& outRect, HWND excludeHwnd, const RECT* clientRect) {
    if (!m_initialized || !m_pAutomation) return false;

    IUIAutomationElement* pElement = nullptr;
    HRESULT hr = m_pAutomation->ElementFromPoint(pt, &pElement);
    if (FAILED(hr) || !pElement) return false;

    if (excludeHwnd) {
        UIA_HWND nativeHwnd = nullptr;
        hr = pElement->get_CurrentNativeWindowHandle(&nativeHwnd);
        if (SUCCEEDED(hr) && (HWND)nativeHwnd == excludeHwnd) {
            pElement->Release();
            return false;
        }
    }

    RECT leafRect = {};
    hr = pElement->get_CurrentBoundingRectangle(&leafRect);
    if (FAILED(hr) || leafRect.right - leafRect.left <= 0 || leafRect.bottom - leafRect.top <= 0) {
        pElement->Release();
        return false;
    }

    if (clientRect) {
        RECT ancestorRect = {};
        if (FindMeaningfulAncestor(pElement, ancestorRect, *clientRect)) {
            pElement->Release();
            outRect = ancestorRect;
            return true;
        }
    }

    pElement->Release();
    outRect = leafRect;
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
