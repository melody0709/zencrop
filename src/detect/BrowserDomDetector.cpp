#include "BrowserDomDetector.h"
#include "net/BrowserCompanionBridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace browser_companion {
bool IsBrowserProcessName(std::wstring_view name) {
    static constexpr std::array list = {L"360browser.exe",
                                        L"360chrome.exe",
                                        L"360se.exe",
                                        L"7star.exe",
                                        L"amigobrowser.exe",
                                        L"avastbrowser.exe",
                                        L"baidubrowser.exe",
                                        L"blackhawk.exe",
                                        L"bliskbrowser.exe",
                                        L"brave.exe",
                                        L"centbrowser.exe",
                                        L"chedot.exe",
                                        L"chrome.exe",
                                        L"chrome sxs.exe",
                                        L"chromeplus.exe",
                                        L"chromium.exe",
                                        L"citrio.exe",
                                        L"coccbrowser.exe",
                                        L"coowon.exe",
                                        L"cyberfox.exe",
                                        L"dragon.exe",
                                        L"edge.exe",
                                        L"epic privacy browser.exe",
                                        L"firefox.exe",
                                        L"ghostbrowser.exe",
                                        L"icedragon.exe",
                                        L"iridium.exe",
                                        L"kinza.exe",
                                        L"kometabrowser.exe",
                                        L"liebao.exe",
                                        L"liebaobrowser.exe",
                                        L"maxthon.exe",
                                        L"msedge.exe",
                                        L"nichrome.exe",
                                        L"opera.exe",
                                        L"orbitum.exe",
                                        L"palemoon.exe",
                                        L"qip surf.exe",
                                        L"qqbrowser.exe",
                                        L"quark.exe",
                                        L"seamonkey.exe",
                                        L"sleipnir5.exe",
                                        L"slimbrowser.exe",
                                        L"slimjet.exe",
                                        L"slbrowser.exe",
                                        L"sogouexplorer.exe",
                                        L"spider.exe",
                                        L"sputnik.exe",
                                        L"superbird.exe",
                                        L"theworld.exe",
                                        L"torch.exe",
                                        L"ucbrowser.exe",
                                        L"uran.exe",
                                        L"vivaldi.exe",
                                        L"xvast.exe",
                                        L"xpombrowser.exe"};
    return std::ranges::any_of(list, [name](const wchar_t *item) {
        return name.size() == std::wcslen(item) && _wcsnicmp(name.data(), item, name.size()) == 0;
    });
}

bool IsBrowserWindow(HWND window, std::wstring_view processName) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (IsBrowserProcessName(processName) || Bridge::Instance().IsConnectedBrowser(pid))
        return true;
    // A process-name cache keyed by PID alone cannot authorize accessibility after Windows reuses a PID.
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const bool read = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
    CloseHandle(process);
    if (!read)
        return false;
    path.resize(size);
    const size_t slash = path.find_last_of(L"\\/");
    return IsBrowserProcessName(std::wstring_view(path).substr(slash == std::wstring::npos ? 0 : slash + 1));
}

std::vector<RECT> FindBrowserViewports(HWND window) {
    const auto oldDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct Search {
        HWND root;
        std::vector<RECT> rects;
    } search{window};
    EnumChildWindows(
        window,
        [](HWND child, LPARAM data) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(data);
            wchar_t name[256] = {};
            GetClassNameW(child, name, 256);
            if (std::wstring_view(name) != L"Chrome_RenderWidgetHostHWND" || !IsWindowVisible(child) ||
                GetAncestor(child, GA_ROOT) != search.root)
                return TRUE;
            RECT rect = {}, root = {}, clipped = {};
            if (!GetWindowRect(child, &rect) || !GetWindowRect(search.root, &root) ||
                !IntersectRect(&clipped, &rect, &root) || !EqualRect(&clipped, &rect))
                return TRUE;
            if (rect.right <= rect.left || rect.bottom <= rect.top || search.rects.size() >= 64)
                return TRUE;
            if (!std::ranges::any_of(search.rects, [&](const RECT &existing) { return EqualRect(&existing, &rect); })) {
                search.rects.push_back(rect);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    SetThreadDpiAwarenessContext(oldDpi);
    return search.rects;
}

RECT FindBrowserViewport(HWND window) {
    auto rects = FindBrowserViewports(window);
    const auto area = [](RECT rect) { return static_cast<int64_t>(rect.right - rect.left) * (rect.bottom - rect.top); };
    std::ranges::sort(rects, std::greater{}, area);
    return rects.empty() || (rects.size() > 1 && area(rects[0]) == area(rects[1])) ? RECT{} : rects.front();
}

DomDetector::~DomDetector() {
    Clear();
}
void DomDetector::Clear() {
    if (!m_session.empty())
        Bridge::Instance().End(m_session);
    m_window = nullptr;
    m_candidates.clear();
    m_document.clear();
    m_session.clear();
    m_index = 0;
    m_wheelSelected = false;
    m_pageSelected = false;
    m_frozenInvalid = false;
    m_frozenDocument.clear();
    m_frozenConnection.clear();
}
void DomDetector::Configure(std::string session, bool frozen, std::stop_token stop) {
    if (m_baseSession != session) {
        Clear();
        m_baseSession = std::move(session);
    }
    m_frozen = frozen;
    m_stop = stop;
}

RECT DomDetector::Inspect(HWND window, RECT viewport, POINT point, int direction, bool foregroundAtCapture) {
    if (!m_frozen && !m_document.empty() && !Bridge::Instance().IsValid(m_session, m_document))
        Clear();
    if (window != m_window) {
        Clear();
        m_window = window;
        m_viewport = viewport;
        m_initialViewports = FindBrowserViewports(window);
        m_session = std::format("{}.{}", m_baseSession, ++m_binding);
    }
    const auto unavailable = [&](std::string status) {
        m_status = std::move(status);
        if (m_frozen && (m_status == "geometry_invalidated" || m_status == "invalid_geometry" ||
                         m_status == "viewport_unavailable" || !m_frozenDocument.empty()))
            m_frozenInvalid = true;
        m_candidates.clear();
        m_pageSelected = false;
        m_document.clear();
        Bridge::Instance().End(m_session);
        if (!m_frozen)
            Clear();
        return RECT{};
    };
    const auto miss = [&](std::string status) {
        // Browser chrome and empty hits do not change the captured document or invalidate its session.
        m_status = std::move(status);
        m_candidates.clear();
        m_document.clear();
        m_index = 0;
        m_wheelSelected = false;
        m_pageSelected = false;
        return RECT{};
    };
    if (m_frozenInvalid)
        return unavailable("geometry_invalidated");
    const auto currentViewports = FindBrowserViewports(window);
    const RECT largestViewport = FindBrowserViewport(window);
    const auto equalSets = [](const auto &a, const auto &b) {
        return a.size() == b.size() && std::ranges::all_of(a, [&](const RECT &r) {
                   return std::ranges::any_of(b, [&](const RECT &other) { return EqualRect(&r, &other); });
               });
    };
    if (!IsWindow(window) || currentViewports.empty() ||
        (m_frozen && (!equalSets(currentViewports, m_initialViewports) || !EqualRect(&viewport, &largestViewport)))) {
        return unavailable("viewport_unavailable");
    }
    if (!std::ranges::any_of(currentViewports, [&](const RECT &rect) { return PtInRect(&rect, point); }))
        return miss("outside_viewport");
    const RECT previous = m_candidates.empty() ? RECT{} : m_candidates[m_index];
    const RECT previousLeaf = m_candidates.empty() ? RECT{} : m_candidates.front();
    auto reply = Bridge::Instance().Query(window, viewport, point, m_session, std::to_string(++m_request), m_frozen,
                                          m_stop, currentViewports, foregroundAtCapture);
    m_candidates.clear();
    if (!reply) {
        if (reply.error() == "outside_viewport" || reply.error() == "empty_hit" ||
            reply.error() == "viewport_ambiguous")
            return miss(reply.error());
        return unavailable(reply.error());
    }
    if (!reply->contains("scale") || !(*reply)["scale"].is_number() ||
        (reply->contains("page") && !(*reply)["page"].is_boolean())) {
        return unavailable("invalid_geometry");
    }
    const double scale = (*reply)["scale"].get<double>();
    if (!reply->contains("viewport") || !(*reply)["viewport"].is_array() || (*reply)["viewport"].size() != 4) {
        return unavailable("invalid_geometry");
    }
    const auto &returned = (*reply)["viewport"];
    for (const auto &value : returned)
        if (!value.is_number_integer() || std::abs(value.get<double>()) > 10000000) {
            return unavailable("invalid_geometry");
        }
    viewport = {returned[0].get<LONG>(), returned[1].get<LONG>(), returned[2].get<LONG>(), returned[3].get<LONG>()};
    if (!std::ranges::any_of(currentViewports,
                             [&](const RECT &candidate) { return EqualRect(&candidate, &viewport); })) {
        return unavailable("invalid_geometry");
    }
    m_viewport = viewport;
    for (const auto &candidate : (*reply)["candidates"]) {
        auto rect = MapCandidate(candidate, viewport, scale, point);
        if (!rect) {
            return unavailable(rect.error());
        }
        if (!m_candidates.empty()) {
            const auto &last = m_candidates.back();
            if (EqualRect(&last, &*rect))
                continue;
            if (rect->left > last.left || rect->top > last.top || rect->right < last.right ||
                rect->bottom < last.bottom) {
                continue;
            }
        }
        m_candidates.push_back(*rect);
    }
    m_document = (*reply)["documentId"].get<std::string>();
    const auto connection = (*reply)["connectionId"].get<std::string>();
    if (m_frozen && !m_frozenDocument.empty() && (m_frozenDocument != m_document || m_frozenConnection != connection))
        return unavailable("geometry_invalidated");
    if (m_frozen) {
        m_frozenDocument = m_document;
        m_frozenConnection = connection;
    }
    const auto finalViewports = FindBrowserViewports(window);
    if (m_candidates.empty() || m_stop.stop_requested() || !Bridge::Instance().IsValid(m_session, m_document) ||
        !equalSets(currentViewports, finalViewports)) {
        return unavailable("geometry_invalidated");
    }
    m_index = 0;
    const bool sameLeaf = EqualRect(&previousLeaf, &m_candidates.front());
    // A whole-page ancestor must not lock every subsequent leaf; ordinary wheel-selected regions stay locked.
    if (direction != 0 || (m_wheelSelected && PtInRect(&previous, point) && (!m_pageSelected || sameLeaf))) {
        for (size_t i = 0; i < m_candidates.size(); ++i)
            if (EqualRect(&previous, &m_candidates[i])) {
                m_index = i;
                break;
            }
        if (direction > 0)
            m_index = (std::min)(m_index + 1, m_candidates.size() - 1);
        else if (direction < 0 && m_index > 0)
            --m_index;
        if (direction != 0)
            m_wheelSelected = true;
    } else {
        m_wheelSelected = false;
    }
    m_pageSelected = reply->value("page", false) && m_index == m_candidates.size() - 1;
    m_status = "connected";
    return m_candidates[m_index];
}
}
