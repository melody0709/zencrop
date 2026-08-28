#include "ocr/ui/dashboard/DashboardOleDropTarget.h"
#include "ocr/ui/OcrDashboardWindow.h"

DashboardOleDropTarget::DashboardOleDropTarget(OcrDashboardWindow* owner)
    : m_owner(owner)
{
}

HRESULT STDMETHODCALLTYPE DashboardOleDropTarget::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppvObject = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DashboardOleDropTarget::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE DashboardOleDropTarget::Release()
{
    ULONG count = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (count == 0) delete this;
    return count;
}

HRESULT STDMETHODCALLTYPE DashboardOleDropTarget::DragEnter(
    IDataObject* dataObject,
    DWORD,
    POINTL,
    DWORD* effect)
{
    SetEffect(dataObject, effect);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DashboardOleDropTarget::DragOver(DWORD, POINTL, DWORD* effect)
{
    if (effect) *effect = m_lastAccept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DashboardOleDropTarget::DragLeave()
{
    m_lastAccept = false;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DashboardOleDropTarget::Drop(
    IDataObject* dataObject,
    DWORD,
    POINTL,
    DWORD* effect)
{
    bool accepted = m_owner && m_owner->HandleOleDropDataObject(dataObject);
    m_lastAccept = false;
    if (effect) *effect = accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

void DashboardOleDropTarget::SetEffect(IDataObject* dataObject, DWORD* effect)
{
    m_lastAccept = m_owner && m_owner->CanAcceptOleDropDataObject(dataObject);
    if (effect) *effect = m_lastAccept ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}
