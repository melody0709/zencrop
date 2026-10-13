#include "SmartDetector.h"
#include "BrowserDomDetector.h"
#include "Utils.h"
#include "core/WideCaseOps.h"
#include "core/WideCompareOps.h"
#include <dwmapi.h>
#include <oleauto.h>
#include <psapi.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// P1: Process name cache (PID -> lowercased process name).
// Avoids repeated OpenProcess + K32GetModuleBaseNameW calls on every hover.
// Cache process names lazily by PID.
std::map<DWORD, std::wstring> g_processNameCache;
std::mutex g_processNameCacheMutex;

std::wstring GetProcessNameCached(DWORD pid) {
    if (pid == 0) {
        return {};
    }
    {
        std::lock_guard<std::mutex> lock(g_processNameCacheMutex);
        auto it = g_processNameCache.find(pid);
        if (it != g_processNameCache.end()) {
            return it->second;
        }
    }
    // Cache miss: query the process name.
    std::wstring name;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t path[MAX_PATH * 4] = {};
        DWORD size = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(hProcess, 0, path, &size) && size > 0) {
            const wchar_t* base = wcsrchr(path, L'\\');
            const wchar_t* slash = wcsrchr(path, L'/');
            if (slash && (!base || slash > base)) {
                base = slash;
            }
            name = base ? base + 1 : path;
        }
        CloseHandle(hProcess);
    }
    if (name.empty()) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess) {
            wchar_t buf[MAX_PATH] = {};
            if (K32GetModuleBaseNameW(hProcess, NULL, buf, ARRAYSIZE(buf))) {
                name = buf;
            }
            CloseHandle(hProcess);
        }
    }
    // OWN-79: pure lower (WideStringUtils).
    name = WideToLower(std::move(name));
    std::lock_guard<std::mutex> lock(g_processNameCacheMutex);
    // Emplace even if empty (negative cache: avoid retrying OpenProcess).
    auto result = g_processNameCache.emplace(pid, std::move(name));
    return result.first->second;
}

// P0: Hang check cache (HWND -> {isHung, timestamp}).
// Avoids repeated SendMessageTimeoutW(50ms) on slow-responding windows.
// TTL = 500ms: if the window was responsive 500ms ago, don't re-probe.
// Cache hang results separately from the window snapshot.
struct HangCacheEntry {
    bool isHung;
    DWORD checkTick;
};
std::map<HWND, HangCacheEntry> g_hangCheckCache;
std::mutex g_hangCheckCacheMutex;
constexpr DWORD kHangCacheTtlMs = 500;

bool IsWindowHung(HWND hWnd) {
    // Probe window responsiveness before accessibility work:
    //   1. Skip null or invalid windows -> return true (treat as hung)
    //   2. Skip windows belonging to current process -> return false (never hang on ourselves)
    //   3. IsHungAppWindow -> system-level hang detection
    //   4. SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG, 50ms) -> responsive probe
    // Combine SMTO_ABORTIFHUNG and SMTO_BLOCK so the probe is bounded.
    // Keep the timeout at 50ms to avoid blocking hover updates.
    if (!hWnd || !IsWindow(hWnd)) return true;

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == GetCurrentProcessId()) return false;

    // Check cache first (TTL-based: avoid re-probing within 500ms).
    DWORD now = GetTickCount();
    {
        std::lock_guard<std::mutex> lock(g_hangCheckCacheMutex);
        auto it = g_hangCheckCache.find(hWnd);
        if (it != g_hangCheckCache.end()) {
            DWORD age = now - it->second.checkTick;
            if (age < kHangCacheTtlMs) {
                return it->second.isHung;
            }
        }
    }

    bool hung = false;
    if (IsHungAppWindow(hWnd)) {
        hung = true;
    } else {
        DWORD_PTR result = 0;
        LRESULT ok = SendMessageTimeoutW(hWnd, WM_NULL, 0, 0,
                                          SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result);
        hung = (ok == 0);
    }

    // Update cache.
    {
        std::lock_guard<std::mutex> lock(g_hangCheckCacheMutex);
        g_hangCheckCache[hWnd] = { hung, now };
        // Opportunistic cleanup: remove stale entries (window closed or expired).
        for (auto it = g_hangCheckCache.begin(); it != g_hangCheckCache.end(); ) {
            if (!IsWindow(it->first) || (now - it->second.checkTick) > kHangCacheTtlMs * 10) {
                it = g_hangCheckCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    return hung;
}

bool RectContains(const RECT& outer, const RECT& inner) {
    return outer.left <= inner.left &&
        outer.top <= inner.top &&
        outer.right >= inner.right &&
        outer.bottom >= inner.bottom;
}

} // namespace

SmartDetector::~SmartDetector() {
    Shutdown();
}

SmartDetector::AccessibleElement::AccessibleElement() {
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = CHILDID_SELF;
}

SmartDetector::AccessibleElement::~AccessibleElement() {
    VariantClear(&child);
    if (acc) {
        acc->Release();
        acc = nullptr;
    }
}


SmartDetector::WindowCandidate::~WindowCandidate() {
    ReleaseRoot();
}

SmartDetector::WindowCandidate::WindowCandidate(WindowCandidate&& other) noexcept {
    hwnd = other.hwnd;
    rect = other.rect;
    browserViewport = other.browserViewport;
    browser = other.browser;
    detectMode = other.detectMode;
    rootAcc = other.rootAcc;
    className = std::move(other.className);
    processName = std::move(other.processName);
    flags = other.flags;
    accValidState = other.accValidState;
    other.hwnd = nullptr;
    other.rootAcc = nullptr;
    other.rect = {};
    other.accValidState = 0;
}

SmartDetector::WindowCandidate& SmartDetector::WindowCandidate::operator=(WindowCandidate&& other) noexcept {
    if (this != &other) {
        ReleaseRoot();
        hwnd = other.hwnd;
        rect = other.rect;
        browserViewport = other.browserViewport;
        browser = other.browser;
        detectMode = other.detectMode;
        rootAcc = other.rootAcc;
        className = std::move(other.className);
        processName = std::move(other.processName);
        flags = other.flags;
        accValidState = other.accValidState;
        other.hwnd = nullptr;
        other.rootAcc = nullptr;
        other.rect = {};
        other.accValidState = 0;
    }
    return *this;
}

void SmartDetector::WindowCandidate::ReleaseRoot() {
    if (rootAcc) {
        rootAcc->Release();
        rootAcc = nullptr;
    }
}

bool SmartDetector::Initialize() {
    if (m_initialized) return true;
    m_initialized = true;
    return true;
}

void SmartDetector::Shutdown() {
    ClearDetectorCache();
    m_initialized = false;
}

void SmartDetector::ClearDetectorCache() {
    ResetRuntimeState();
    m_windowSnapshot.clear();
    m_excludeHwnd = nullptr;
}

void SmartDetector::ResetRuntimeState() {
    m_currentElement.reset();
    m_browserDetector.Clear();
    m_activeHwnd = nullptr;
    m_traversedByWheel = false;
}

RECT SmartDetector::ClampRect(RECT r, const RECT& bounds) {
    if (r.left < bounds.left) r.left = bounds.left;
    if (r.top < bounds.top) r.top = bounds.top;
    if (r.right > bounds.right) r.right = bounds.right;
    if (r.bottom > bounds.bottom) r.bottom = bounds.bottom;
    return r;
}

long long SmartDetector::RectArea(const RECT& r) {
    return (long long)(r.right - r.left) * (long long)(r.bottom - r.top);
}

bool SmartDetector::RectEqual(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

RECT SmartDetector::ClipCandidateRect(RECT rect, const WindowCandidate& win, POINT pt) const {
    RECT clipped = ClampRect(rect, win.rect);
    if ((win.flags & 1) != 0) {
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (hMon && GetMonitorInfoW(hMon, &mi)) {
            RECT workClipped = {};
            if (IntersectRect(&workClipped, &clipped, &mi.rcWork) &&
                PtInRect(&workClipped, pt)) {
                clipped = workClipped;
            }
        }
    }
    return clipped;
}

bool SmartDetector::BuildWindowSnapshot(HWND excludeHwnd) {
    m_windowSnapshot.clear();
    m_excludeHwnd = excludeHwnd;
    ResetRuntimeState();

    struct EnumData {
        SmartDetector* detector;
        HWND excludeHwnd;
        RECT screenRect;
    } data = { this, excludeHwnd, GetVirtualScreenRect() };

    EnumDesktopWindows(nullptr, [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* data = reinterpret_cast<EnumData*>(lParam);
        if (!hwnd || hwnd == data->excludeHwnd) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;

        wchar_t classNameBuf[256] = {};
        GetClassNameW(hwnd, classNameBuf, ARRAYSIZE(classNameBuf));
        if (WideEquals(classNameBuf, L"ZenCrop.OverlayWindow")) return TRUE;

        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TRANSPARENT) != 0) return TRUE;

        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
            return TRUE;
        }

        RECT rect = {};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
            GetWindowRect(hwnd, &rect);
        }

        RECT clipped = {};
        if (!IntersectRect(&clipped, &rect, &data->screenRect)) return TRUE;
        rect = clipped;
        if (rect.right - rect.left <= 1 || rect.bottom - rect.top <= 1) return TRUE;

        // Trim one pixel to avoid DWM border and shadow bleed.
        rect.right -= 1;
        rect.bottom -= 1;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);

        WindowCandidate win;
        win.hwnd = hwnd;
        win.rect = rect;
        win.detectMode = 1;
        win.className = classNameBuf;
        win.processName = GetProcessNameCached(pid);
        win.browser = browser_companion::IsBrowserWindow(hwnd, win.processName);
        if (win.browser) win.browserViewport = browser_companion::FindBrowserViewport(hwnd);
        win.flags = 0;
        // OWN-94: pure case-insensitive process name compare.
        if (WideEqualsNoCase(win.processName, L"Explorer.EXE") &&
            WideEquals(win.className.c_str(), L"WorkerW")) {
            win.flags |= 1;
        }
        win.accValidState = 0;
        data->detector->m_windowSnapshot.push_back(std::move(win));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return !m_windowSnapshot.empty();
}

SmartDetector::WindowCandidate* SmartDetector::FindWindowAtPoint(POINT pt) {
    for (auto& win : m_windowSnapshot) {
        if (PtInRect(&win.rect, pt)) {
            return &win;
        }
    }
    return nullptr;
}

bool SmartDetector::CheckWindowAlive(WindowCandidate& win) {
    if (!win.hwnd || !IsWindow(win.hwnd)) {
        win.ReleaseRoot();
        win.accValidState = 0;
        return false;
    }

    RECT current = {};
    if (FAILED(DwmGetWindowAttribute(win.hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &current, sizeof(current)))) {
        GetWindowRect(win.hwnd, &current);
    }
    if (win.browser && !m_browserDetector.Frozen()) {
        RECT clipped = {};
        const RECT screen = GetVirtualScreenRect();
        if (IntersectRect(&clipped, &current, &screen)) {
            --clipped.right;
            --clipped.bottom;
        }
        return EqualRect(&clipped, &win.rect) && IsWindowVisible(win.hwnd);
    }
    bool containsSnapshot =
        current.left <= win.rect.left &&
        current.top <= win.rect.top &&
        current.right >= win.rect.right &&
        current.bottom >= win.rect.bottom;
    if (containsSnapshot) {
        return true;
    }

    win.ReleaseRoot();
    win.accValidState = 0;
    return false;
}

void SmartDetector::RefreshLiveBrowserSnapshot(HWND excludeHwnd) {
    if (!m_browserDetector.Frozen() && std::ranges::any_of(m_windowSnapshot, [&](WindowCandidate &win) {
            return win.browser && !CheckWindowAlive(win);
        })) {
        BuildWindowSnapshot(excludeHwnd);
    }
}

bool SmartDetector::EnsureAccessibleRoot(WindowCandidate& win, POINT pt, HWND excludeHwnd) {
    if (browser_companion::IsBrowserWindow(win.hwnd, win.processName)) return false;
    if (win.detectMode == 2) return false;
    if (win.rootAcc) return true;
    if (win.accValidState == 1) return false;
    if (IsWindowHung(win.hwnd)) {
        win.accValidState = 1;
        win.detectMode = 2;
        return false;
    }

    IAccessible* root = nullptr;
    HRESULT hr = AccessibleObjectFromWindow(win.hwnd, OBJID_WINDOW, IID_IAccessible, (void**)&root);
    if (FAILED(hr) || !root) {
        win.accValidState = 1;
        return false;
    }

    win.rootAcc = root;
    win.accValidState = 2;
    return true;
}

std::shared_ptr<SmartDetector::AccessibleElement> SmartDetector::MakeElement(
    IAccessible* acc, const VARIANT& child, bool addRef) {
    if (!acc) return nullptr;
    if (addRef) {
        acc->AddRef();
    }

    auto element = std::make_shared<AccessibleElement>();
    element->acc = acc;
    VariantClear(&element->child);
    VariantInit(&element->child);
    if (FAILED(VariantCopy(&element->child, const_cast<VARIANT*>(&child)))) {
        return nullptr;
    }
    return element;
}

std::shared_ptr<SmartDetector::AccessibleElement> SmartDetector::MakeSelfElement(
    IAccessible* acc, bool addRef) {
    VARIANT self;
    VariantInit(&self);
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    auto element = MakeElement(acc, self, addRef);
    VariantClear(&self);
    return element;
}

bool SmartDetector::IsSelfElement(const std::shared_ptr<AccessibleElement>& element) const {
    return element &&
           element->child.vt == VT_I4 &&
           element->child.lVal == CHILDID_SELF;
}

std::shared_ptr<SmartDetector::AccessibleElement> SmartDetector::ElementFromPoint(IAccessible* rootAcc, POINT pt) {
    if (!rootAcc) return nullptr;

    IAccessible* current = rootAcc;
    current->AddRef();
    bool finished = false;

    VARIANT child;
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = CHILDID_SELF;

    for (int depth = 0; depth < 31; ++depth) {
        VARIANT hit;
        VariantInit(&hit);
        HRESULT hr = current->accHitTest(pt.x, pt.y, &hit);
        if (FAILED(hr)) {
            VariantClear(&hit);
            break;
        }

        if (hit.vt == VT_DISPATCH && hit.pdispVal) {
            IAccessible* next = nullptr;
            HRESULT qi = hit.pdispVal->QueryInterface(IID_IAccessible, (void**)&next);
            VariantClear(&hit);
            if (FAILED(qi) || !next) break;

            current->Release();
            current = next;
            VariantClear(&child);
            child.vt = VT_I4;
            child.lVal = CHILDID_SELF;
            continue;
        }

        if (hit.vt == VT_I4) {
            VariantClear(&child);
            VariantInit(&child);
            child.vt = VT_I4;
            child.lVal = hit.lVal;
            VariantClear(&hit);
            finished = true;
            break;
        }

        if (hit.vt == VT_EMPTY) {
            VariantClear(&hit);
            VariantClear(&child);
            VariantInit(&child);
            child.vt = VT_I4;
            child.lVal = CHILDID_SELF;
            finished = true;
            break;
        }

        VariantClear(&hit);
        break;
    }

    if (!finished) {
        current->Release();
        VariantClear(&child);
        return nullptr;
    }

    auto element = MakeElement(current, child, false); // Takes current ref.
    VariantClear(&child);
    if (!element) return nullptr;

    RECT elementRect = {};
    if (GetElementRect(element, elementRect) && PtInRect(&elementRect, pt)) {
        return element;
    }

    // Fall back to the parent once when the final wrapper rect misses the point.
    return GetParentElement(element, pt);
}

bool SmartDetector::GetElementRect(const std::shared_ptr<AccessibleElement>& element, RECT& outRect) {
    outRect = {};
    if (!element || !element->acc) return false;
    if (element->hasCachedRect) {
        outRect = element->cachedRect;
        return RectArea(outRect) > 25;
    }

    long left = 0, top = 0, width = 0, height = 0;
    VARIANT child;
    VariantInit(&child);
    VariantCopy(&child, const_cast<VARIANT*>(&element->child));
    HRESULT hr = element->acc->accLocation(&left, &top, &width, &height, child);
    VariantClear(&child);
    if (FAILED(hr) || width <= 0 || height <= 0) return false;

    element->cachedRect = { left, top, left + width, top + height };
    element->hasCachedRect = true;
    outRect = element->cachedRect;
    return RectArea(outRect) > 25;
}

int SmartDetector::GetElementChildCount(const std::shared_ptr<AccessibleElement>& element) {
    if (!element || !element->acc) return 0;
    if (element->cachedChildCount >= 0) return element->cachedChildCount;
    long count = 0;
    if (FAILED(element->acc->get_accChildCount(&count)) || count < 0) {
        count = 0;
    }
    element->cachedChildCount = (int)count;
    return element->cachedChildCount;
}

std::shared_ptr<SmartDetector::AccessibleElement> SmartDetector::GetParentElement(
    const std::shared_ptr<AccessibleElement>& element, POINT pt) {
    if (!element || !element->acc) return nullptr;

    // Only CHILDID_SELF wrappers can safely traverse through get_accParent.
    // Non-self MSAA child IDs return null.
    if (!IsSelfElement(element)) return nullptr;

    RECT previousRect = {};
    GetElementRect(element, previousRect);

    IDispatch* parentDisp = nullptr;
    HRESULT hr = element->acc->get_accParent(&parentDisp);
    if (FAILED(hr) || !parentDisp) return nullptr;

    IAccessible* ancestorAcc = nullptr;
    hr = parentDisp->QueryInterface(IID_IAccessible, (void**)&ancestorAcc);
    parentDisp->Release();
    if (FAILED(hr) || !ancestorAcc) return nullptr;

    for (int depth = 0; depth < 31 && ancestorAcc; ++depth) {
        auto ancestor = MakeSelfElement(ancestorAcc, false); // takes ancestorAcc ref
        ancestorAcc = nullptr;
        if (!ancestor) return nullptr;

        RECT ancestorRect = {};
        if (GetElementRect(ancestor, ancestorRect) &&
            PtInRect(&ancestorRect, pt) &&
            !RectEqual(ancestorRect, previousRect)) {
            return ancestor;
        }

        if (RectArea(ancestorRect) > 0) {
            previousRect = ancestorRect;
        }

        IDispatch* nextDisp = nullptr;
        hr = ancestor->acc->get_accParent(&nextDisp);
        if (FAILED(hr) || !nextDisp) {
            break;
        }

        IAccessible* nextAcc = nullptr;
        hr = nextDisp->QueryInterface(IID_IAccessible, (void**)&nextAcc);
        nextDisp->Release();
        if (FAILED(hr) || !nextAcc) {
            break;
        }
        ancestorAcc = nextAcc;
    }

    if (ancestorAcc) {
        ancestorAcc->Release();
    }
    return nullptr;
}

SmartRectResult SmartDetector::GetRectByPoint(POINT pt, HWND excludeHwnd) {
    SmartRectResult result;
    result.pt = pt;

    if (!m_windowSnapshot.empty() && m_excludeHwnd == nullptr) m_excludeHwnd = excludeHwnd;
    if (m_windowSnapshot.empty() || m_excludeHwnd != excludeHwnd) {
        BuildWindowSnapshot(excludeHwnd);
    }

    RefreshLiveBrowserSnapshot(excludeHwnd);
    WindowCandidate* win = FindWindowAtPoint(pt);
    if (!win) {
        BuildWindowSnapshot(excludeHwnd);
        win = FindWindowAtPoint(pt);
    }
    if (!win) {
        return result;
    }

    const bool activeWindowChanged = (m_activeHwnd != win->hwnd);
    const bool needsAliveCheck = activeWindowChanged || !m_currentElement;
    const bool browserWindow = browser_companion::IsBrowserWindow(win->hwnd, win->processName);
    if (needsAliveCheck && !browserWindow && !CheckWindowAlive(*win)) {
        BuildWindowSnapshot(excludeHwnd);
        win = FindWindowAtPoint(pt);
        if (!win) {
            return result;
        }
    }

    result.hwnd = win->hwnd;
    result.windowRect = win->rect;
    if (m_activeHwnd != win->hwnd) {
        m_currentElement.reset();
        m_traversedByWheel = false;
        m_activeHwnd = win->hwnd;
    }

    if (browser_companion::IsBrowserWindow(win->hwnd, win->processName)) {
        result.browser = true;
        result.rect = m_browserDetector.Inspect(win->hwnd, win->browserViewport, pt, 0, win->hwnd == m_captureForeground);
        result.browserSession = m_browserDetector.Session();
        result.browserDocument = m_browserDetector.Document();
        result.browserStatus = m_browserDetector.Status();
        if (RectArea(result.rect) <= 25) result.rect = ClipCandidateRect(win->rect, *win, pt);
        result.success = RectArea(result.rect) > 25 && PtInRect(&result.rect, pt);
        return result;
    }
    m_browserDetector.Clear();

    if (m_traversedByWheel && m_currentElement) {
        RECT traversedRect = {};
        if (GetElementRect(m_currentElement, traversedRect)) {
            traversedRect = ClipCandidateRect(traversedRect, *win, pt);
            if (PtInRect(&traversedRect, pt)) {
                result.rect = traversedRect;
                result.success = true;
                return result;
            }
        }
        m_traversedByWheel = false;
    }

    auto selected = std::shared_ptr<AccessibleElement>();
    RECT selectedRect = {};
    bool msaaHit = false;

    if (win->detectMode == 1 && EnsureAccessibleRoot(*win, pt, excludeHwnd)) {
        DWORD hitStart = GetTickCount();
        auto first = ElementFromPoint(win->rootAcc, pt);
        DWORD hitElapsed = GetTickCount() - hitStart;
        bool markWindowOnlyAfterThisHit = false;
        if (win->detectMode == 1 && hitElapsed > 300) {
            markWindowOnlyAfterThisHit = true;
        }

        if (first) {
            RECT firstRect = {};
            if (GetElementRect(first, firstRect)) {
                firstRect = ClipCandidateRect(firstRect, *win, pt);
                if (RectArea(firstRect) > 25 && PtInRect(&firstRect, pt)) {
                    selected = first;
                    selectedRect = firstRect;
                    msaaHit = true;
                }
            }


        }

        if (markWindowOnlyAfterThisHit) {
            win->detectMode = 2;
        }
    }

    if (msaaHit) {
        if (selected) {
            m_currentElement = selected;
        }
        result.rect = selectedRect;
        result.success = true;
    } else {
        result.rect = ClipCandidateRect(win->rect, *win, pt);
        result.success = RectArea(result.rect) > 25 && PtInRect(&result.rect, pt);
        if (win->rootAcc) {
            m_currentElement = MakeSelfElement(win->rootAcc, true);
        } else {
            m_currentElement.reset();
        }
    }

    return result;
}

RECT SmartDetector::GetParentRect(POINT pt, HWND targetHwnd, HWND excludeHwnd, const RECT& clientRect) {
    (void)clientRect;
    if (!m_windowSnapshot.empty() && m_excludeHwnd == nullptr) m_excludeHwnd = excludeHwnd;
    if (m_windowSnapshot.empty() || m_excludeHwnd != excludeHwnd) {
        BuildWindowSnapshot(excludeHwnd);
    }

    RefreshLiveBrowserSnapshot(excludeHwnd);
    WindowCandidate* win = FindWindowAtPoint(pt);
    if (!win && targetHwnd) {
        for (auto& snapshotWin : m_windowSnapshot) {
            if (snapshotWin.hwnd == targetHwnd) {
                win = &snapshotWin;
                break;
            }
        }
    }
    if (!win) return {};
    if (browser_companion::IsBrowserWindow(win->hwnd, win->processName)) {
        RECT rect = m_browserDetector.Inspect(win->hwnd, win->browserViewport, pt, 1, win->hwnd == m_captureForeground);
        return RectArea(rect) > 25 ? rect : ClipCandidateRect(win->rect, *win, pt);
    }
    m_browserDetector.Clear();
    if (!CheckWindowAlive(*win) || !m_currentElement) {
        return ClipCandidateRect(win->rect, *win, pt);
    }

    RECT currentRect = {};
    GetElementRect(m_currentElement, currentRect);
    auto parent = GetParentElement(m_currentElement, pt);
    if (!parent) {
        if (EnsureAccessibleRoot(*win, pt, excludeHwnd)) {
            m_currentElement = MakeSelfElement(win->rootAcc, true);
        }
        m_traversedByWheel = true;
        return ClipCandidateRect(win->rect, *win, pt);
    }

    RECT parentRect = {};
    if (!GetElementRect(parent, parentRect)) return {};
    parentRect = ClipCandidateRect(parentRect, *win, pt);
    currentRect = ClipCandidateRect(currentRect, *win, pt);
    if (!PtInRect(&parentRect, pt) || RectEqual(parentRect, currentRect)) return {};

    m_currentElement = parent;
    m_traversedByWheel = true;
    return parentRect;
}

RECT SmartDetector::GetChildRect(POINT pt, HWND targetHwnd, HWND excludeHwnd, const RECT& clientRect) {
    (void)clientRect;
    if (!m_windowSnapshot.empty() && m_excludeHwnd == nullptr) m_excludeHwnd = excludeHwnd;
    if (m_windowSnapshot.empty() || m_excludeHwnd != excludeHwnd) {
        BuildWindowSnapshot(excludeHwnd);
    }

    RefreshLiveBrowserSnapshot(excludeHwnd);
    WindowCandidate* win = FindWindowAtPoint(pt);
    if (!win && targetHwnd) {
        for (auto& snapshotWin : m_windowSnapshot) {
            if (snapshotWin.hwnd == targetHwnd) {
                win = &snapshotWin;
                break;
            }
        }
    }
    if (!win) return {};

    if (browser_companion::IsBrowserWindow(win->hwnd, win->processName)) {
        RECT rect = m_browserDetector.Inspect(win->hwnd, win->browserViewport, pt, -1, win->hwnd == m_captureForeground);
        return RectArea(rect) > 25 ? rect : ClipCandidateRect(win->rect, *win, pt);
    }

    m_browserDetector.Clear();
    RECT currentRect = {};
    bool hasCurrent = m_currentElement && GetElementRect(m_currentElement, currentRect);
    if (hasCurrent) {
        currentRect = ClipCandidateRect(currentRect, *win, pt);
    }

    if (!m_traversedByWheel) {
        RECT rect = hasCurrent ? currentRect : ClipCandidateRect(win->rect, *win, pt);
        return rect;
    }

    if (!CheckWindowAlive(*win) || !EnsureAccessibleRoot(*win, pt, excludeHwnd)) {
        RECT rect = hasCurrent ? currentRect : ClipCandidateRect(win->rect, *win, pt);
        return rect;
    }

    auto leaf = ElementFromPoint(win->rootAcc, pt);
    if (!leaf) {
        return hasCurrent ? currentRect : ClipCandidateRect(win->rect, *win, pt);
    }

    std::vector<std::shared_ptr<AccessibleElement>> path;
    RECT leafRect = {};
    if (GetElementRect(leaf, leafRect)) {
        leafRect = ClipCandidateRect(leafRect, *win, pt);
        if (PtInRect(&leafRect, pt)) {
            path.push_back(leaf);
        }
    }

    auto walker = leaf;
    for (int depth = 0; depth < 31 && walker; ++depth) {
        auto parent = GetParentElement(walker, pt);
        if (!parent) break;

        RECT parentRect = {};
        if (!GetElementRect(parent, parentRect)) break;
        parentRect = ClipCandidateRect(parentRect, *win, pt);

        if (hasCurrent && RectContains(parentRect, currentRect)) {
            break;
        }

        if (PtInRect(&parentRect, pt)) {
            path.push_back(parent);
        }
        walker = parent;
    }

    std::shared_ptr<AccessibleElement> selected;
    RECT selectedRect = {};
    if (!path.empty()) {
        selected = path.back();
        GetElementRect(selected, selectedRect);
        selectedRect = ClipCandidateRect(selectedRect, *win, pt);
    } else {
        selected = MakeSelfElement(win->rootAcc, true);
        selectedRect = ClipCandidateRect(win->rect, *win, pt);
    }

    if (!selected) {
        RECT rect = hasCurrent ? currentRect : ClipCandidateRect(win->rect, *win, pt);
        return rect;
    }

    if (hasCurrent && RectEqual(selectedRect, currentRect)) {
        return currentRect;
    }

    m_currentElement = selected;
    m_traversedByWheel = true;
    return selectedRect;
}

void SmartDetector::ClearCache() {
    ResetRuntimeState();
}
