#pragma once

// D-B-7: OLE drop target extracted from OcrDashboardWindow.cpp mega-TU.
// COM IDropTarget; Window remains owner of accept/handle policy methods.

#include <windows.h>
#include <oleidl.h>

class OcrDashboardWindow;

class DashboardOleDropTarget final : public IDropTarget {
public:
    explicit DashboardOleDropTarget(OcrDashboardWindow* owner);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE DragEnter(
        IDataObject* dataObject,
        DWORD grfKeyState,
        POINTL pt,
        DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(
        DWORD grfKeyState,
        POINTL pt,
        DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(
        IDataObject* dataObject,
        DWORD grfKeyState,
        POINTL pt,
        DWORD* effect) override;

private:
    void SetEffect(IDataObject* dataObject, DWORD* effect);

    LONG m_refCount = 1;
    OcrDashboardWindow* m_owner = nullptr;
    bool m_lastAccept = false;
};
