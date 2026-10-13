#pragma once

#include <windows.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace browser_companion {
bool IsBrowserProcessName(std::wstring_view name);
bool IsBrowserWindow(HWND window, std::wstring_view processName);
RECT FindBrowserViewport(HWND window);
std::vector<RECT> FindBrowserViewports(HWND window);

class DomDetector {
public:
    ~DomDetector();
    void Configure(std::string session, bool frozen, std::stop_token stop);
    RECT Inspect(HWND window, RECT viewport, POINT point, int direction = 0, bool foregroundAtCapture = false);
    void Clear();
    const std::string &Session() const { return m_session; }
    const std::string &Document() const { return m_document; }
    const std::string &Status() const { return m_status; }
    RECT Viewport() const { return m_viewport; }
    bool Frozen() const { return m_frozen; }

private:
    std::string m_baseSession;
    std::string m_session;
    std::string m_document;
    std::string m_status;
    std::stop_token m_stop;
    bool m_frozen = false;
    HWND m_window = nullptr;
    RECT m_viewport = {};
    std::vector<RECT> m_initialViewports;
    uint64_t m_request = 0;
    uint64_t m_binding = 0;
    std::vector<RECT> m_candidates;
    size_t m_index = 0;
    bool m_wheelSelected = false;
    bool m_pageSelected = false;
    bool m_frozenInvalid = false;
    std::string m_frozenDocument;
    std::string m_frozenConnection;
};
}
