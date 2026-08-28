#include "OcrResultWindow.h"
#include "Strings.h"
#include "Settings.h"
#include "OcrUtils.h"
#include "core/ClipboardUtils.h"
#include "core/WideStringUtils.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <cwctype>

const wchar_t* OcrResultWindow::ClassName = L"ZenCrop.OcrResult";

const COLORREF OcrResultWindow::BorderColor = RGB(100, 149, 237);
const COLORREF OcrResultWindow::BgColor = RGB(32, 32, 32);
const COLORREF OcrResultWindow::TextColor = RGB(240, 240, 240);
const COLORREF OcrResultWindow::BtnNormalBg = RGB(45, 45, 45);
const COLORREF OcrResultWindow::BtnHoverBg = RGB(60, 60, 60);
const COLORREF OcrResultWindow::BtnPressedBg = RGB(75, 75, 75);
const COLORREF OcrResultWindow::BtnTextColor = RGB(220, 220, 220);
const COLORREF OcrResultWindow::StatusColor = RGB(130, 190, 130);

namespace {

// OWN-74: pos-aware thin wrapper over pure WideStartsWithNoCaseAt.
bool StartsWithNoCase(const std::wstring& text, size_t pos, const wchar_t* prefix) {
    return WideStartsWithNoCaseAt(text, pos, prefix);
}

bool IsUrlEndChar(wchar_t ch) {
    return iswspace(ch) || ch == L'"' || ch == L'\'' || ch == L'<' ||
        ch == L'>' || ch == L')' || ch == L']';
}

std::wstring BuildSizingText(const std::wstring& text) {
    std::wstring sizing;
    sizing.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        if (StartsWithNoCase(text, i, L"<img")) {
            size_t end = text.find(L'>', i);
            sizing += L"[Image]";
            i = (end == std::wstring::npos) ? text.size() : end + 1;
        } else if (StartsWithNoCase(text, i, L"http://") ||
                   StartsWithNoCase(text, i, L"https://")) {
            size_t end = i;
            while (end < text.size() && !IsUrlEndChar(text[end])) end++;
            sizing += L"[URL]";
            i = end;
        } else {
            sizing += text[i++];
        }
    }

    return sizing;
}

int MeasureUsefulTextWidth(HDC hdc, const std::wstring& text, int maxWidth) {
    int longest = 0;
    size_t lineStart = 0;

    for (size_t i = 0; i <= text.size(); i++) {
        bool isLineEnd = i == text.size() || text[i] == L'\r' || text[i] == L'\n';
        if (!isLineEnd) continue;

        int len = (int)(i - lineStart);
        if (len > 0) {
            SIZE lineSize = {};
            GetTextExtentPoint32W(hdc, text.c_str() + lineStart, len, &lineSize);
            longest = max(longest, lineSize.cx);
        }

        if (i < text.size() && text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') i++;
        lineStart = i + 1;
    }

    return min(longest, maxWidth);
}

SIZE CalculateInitialWindowSize(HDC hdc, const std::wstring& sizingText, RECT cropRect) {
    const int minW = 500;
    const int minH = 300;
    const int bottomBarH = 44;
    const int horizontalPadding = 56;
    const int verticalPadding = 24;

    HMONITOR hMon = MonitorFromRect(&cropRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    int workW = mi.rcWork.right - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    int maxW = max(minW, min(workW - 40, 1100));
    int maxH = max(minH, workH * 3 / 4);

    int cropW = cropRect.right - cropRect.left;
    int weakCropHintW = min(max(cropW * 8 / 10, 0), 720);
    int contentW = MeasureUsefulTextWidth(hdc, sizingText, maxW - horizontalPadding);
    int preferredW = max(contentW + horizontalPadding, weakCropHintW);
    int winW = max(min(preferredW, maxW), minW);

    RECT textRect = { 0, 0, max(winW - 24, 1), 0 };
    DrawTextW(hdc, sizingText.c_str(), -1, &textRect, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL);

    int preferredH = textRect.bottom + bottomBarH + verticalPadding;
    int winH = max(min(preferredH, maxH), minH);

    return { winW, winH };
}

}

void OcrResultWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.lpszClassName = ClassName;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    RegisterClassExW(&wcex);
}

POINT OcrResultWindow::CalcWindowPosition(RECT cropRect, int winW, int winH) {
    const int gap = 10;
    POINT pos;

    HMONITOR hMon = MonitorFromRect(&cropRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    // Check if below is enough space
    bool belowFits = (cropRect.bottom + gap + winH <= mi.rcWork.bottom);
    // Check if above is enough space
    bool aboveFits = (cropRect.top - gap - winH >= mi.rcWork.top);

    if (!belowFits && !aboveFits) {
        // Both top and bottom do not have enough space. Place side-by-side on the right!
        pos.x = cropRect.right + gap;
        pos.y = cropRect.top;

        // If right side does not have enough space, try left side
        if (pos.x + winW > mi.rcWork.right) {
            pos.x = cropRect.left - winW - gap;
        }
    } else if (belowFits) {
        // Place below
        pos.x = cropRect.left;
        pos.y = cropRect.bottom + gap;
    } else {
        // Place above
        pos.x = cropRect.left;
        pos.y = cropRect.top - winH - gap;
    }

    // Clamp coordinates to remain within rcWork
    if (pos.x + winW > mi.rcWork.right)
        pos.x = mi.rcWork.right - winW - 10;
    if (pos.x < mi.rcWork.left)
        pos.x = mi.rcWork.left + 10;

    if (pos.y + winH > mi.rcWork.bottom)
        pos.y = mi.rcWork.bottom - winH - 10;
    if (pos.y < mi.rcWork.top)
        pos.y = mi.rcWork.top + 10;

    return pos;
}

LRESULT CALLBACK OcrResultWindow::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        HWND parent = GetParent(hwnd);
        DestroyWindow(parent);
        return 0;
    }
    WNDPROC origProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (origProc) return CallWindowProcW(origProc, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

OcrResultWindow::OcrResultWindow(const std::wstring& text, RECT cropRect,
                                 bool autoCopied, bool showTitlebar, int fontSize,
                                 DWORD elapsedMs, bool resultOnTop)
    : m_text(text), m_showTitlebar(showTitlebar), m_autoCopied(autoCopied), m_fontSize(fontSize), m_elapsedMs(elapsedMs), m_resultOnTop(resultOnTop) {
    RegisterWindowClass();

    std::wstring normalizedText = NormalizeEditText(m_text);
    std::wstring sizingText = BuildSizingText(normalizedText);

    HFONT hCalcFont = CreateFontW(-m_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HDC hDC = GetDC(nullptr);
    HFONT hOldFont = (HFONT)SelectObject(hDC, hCalcFont);

    SIZE initialSize = CalculateInitialWindowSize(hDC, sizingText, cropRect);
    SelectObject(hDC, hOldFont);
    ReleaseDC(nullptr, hDC);
    DeleteObject(hCalcFont);

    int winWidth = initialSize.cx;
    int winHeight = initialSize.cy;

    POINT pos = CalcWindowPosition(cropRect, winWidth, winHeight);

    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN;
    DWORD exStyle = m_resultOnTop ? WS_EX_TOPMOST : 0;
    if (m_showTitlebar) {
        style |= WS_CAPTION | WS_SYSMENU;
    }

    m_hwnd = CreateWindowExW(
        exStyle,
        ClassName, L"OCR",
        style,
        pos.x, pos.y, winWidth, winHeight,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (m_hwnd) {
        BOOL darkValue = TRUE;
        DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkValue, sizeof(darkValue));

        m_hUiFont = CreateFontW(-m_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        m_edit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 0, 0,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr
        );
        SendMessage(m_edit, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

        SetWindowTextW(m_edit, normalizedText.c_str());

        m_editOrigProc = (WNDPROC)SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SetWindowLongPtrW(m_edit, GWLP_USERDATA, (LONG_PTR)m_editOrigProc);

        m_statusText = CreateWindowExW(
            0, L"STATIC",
            m_autoCopied ? (S::IsChinese() ? L"\u2713 \u5DF2\u81EA\u52A8\u590D\u5236" : L"\u2713 Auto-copied") : L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr
        );
        SendMessage(m_statusText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

        m_hintText = CreateWindowExW(
            0, L"STATIC",
            S::IsChinese() ? L"Esc \u5173\u95ED" : L"Esc closes",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr
        );
        SendMessage(m_hintText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

        // OWN-113: pure seconds label (WideStringUtils). Both locales use %.1fs.
        const std::wstring elapsedText = WideFormatSeconds1(m_elapsedMs / 1000.0);
        m_elapsedText = CreateWindowExW(
            0, L"STATIC", elapsedText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            0, 0, 0, 0,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr
        );
        SendMessage(m_elapsedText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

        m_copyBtn = CreateWindowExW(
            0, L"BUTTON",
            S::IsChinese() ? L"\u590D\u5236" : L"Copy",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            0, 0, 0, 0,
            m_hwnd, (HMENU)(INT_PTR)ID_COPY, GetModuleHandle(nullptr), nullptr
        );
        SendMessage(m_copyBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

        m_closeBtn = CreateWindowExW(
            0, L"BUTTON",
            S::IsChinese() ? L"\u5173\u95ED" : L"Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            0, 0, 0, 0,
            m_hwnd, (HMENU)(INT_PTR)ID_CLOSE, GetModuleHandle(nullptr), nullptr
        );
        SendMessage(m_closeBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

        LayoutControls();
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
        UpdateWindow(m_hwnd);
    }
}

OcrResultWindow::~OcrResultWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
}

void OcrResultWindow::LayoutControls() {
    if (!m_hwnd) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int bw = m_showTitlebar ? 0 : BorderWidth;
    int innerL = bw + 8;
    int innerR = rc.right - bw - 8;
    int innerT = bw + 8;
    int innerB = rc.bottom - bw - 8;
    int btnH = 28;
    int btnW = 72;
    int elapsedW = 60;
    int bottomBarH = btnH;
    int editBottom = innerB - bottomBarH - 8;
    int elapsedX = innerR - btnW * 2 - 16 - elapsedW;
    int statusW = m_autoCopied ? 160 : 0;

    if (m_edit)
        MoveWindow(m_edit, innerL, innerT, innerR - innerL, max(editBottom - innerT, 20), TRUE);
    if (m_statusText)
        MoveWindow(m_statusText, innerL, innerB - btnH, statusW, btnH, TRUE);
    if (m_hintText) {
        int hintX = innerL + (statusW > 0 ? statusW + 8 : 0);
        int hintW = elapsedX - hintX - 8;
        if (hintW > 40) {
            MoveWindow(m_hintText, hintX, innerB - btnH, hintW, btnH, TRUE);
            ShowWindow(m_hintText, SW_SHOW);
        } else {
            ShowWindow(m_hintText, SW_HIDE);
        }
    }
    if (m_elapsedText)
        MoveWindow(m_elapsedText, elapsedX, innerB - btnH, elapsedW, btnH, TRUE);
    if (m_closeBtn)
        MoveWindow(m_closeBtn, innerR - btnW, innerB - btnH, btnW, btnH, TRUE);
    if (m_copyBtn)
        MoveWindow(m_copyBtn, innerR - btnW * 2 - 8, innerB - btnH, btnW, btnH, TRUE);
}

void OcrResultWindow::CopyToClipboard() {
    if (!m_edit) return;
    int len = GetWindowTextLengthW(m_edit);
    if (len == 0) return;
    std::wstring buf(len + 1, L'\0');
    GetWindowTextW(m_edit, &buf[0], len + 1);
    buf.resize(len);

    if (!CopyTextToClipboard(m_hwnd, buf)) return;
    m_copied = true;

    SetWindowTextW(m_copyBtn, S::IsChinese() ? L"\u2713 \u5DF2\u590D\u5236" : L"\u2713 Copied");
    SetTimer(m_hwnd, ID_COPY, 2000, nullptr);
}

void OcrResultWindow::PaintBorder(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    HBRUSH bgBrush = CreateSolidBrush(BgColor);
    FillRect(hdc, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    if (!m_showTitlebar) {
        HBRUSH borderBrush = CreateSolidBrush(BorderColor);
        RECT topRect = { clientRect.left, clientRect.top, clientRect.right, clientRect.top + BorderWidth };
        RECT bottomRect = { clientRect.left, clientRect.bottom - BorderWidth, clientRect.right, clientRect.bottom };
        RECT leftRect = { clientRect.left, clientRect.top + BorderWidth, clientRect.left + BorderWidth, clientRect.bottom - BorderWidth };
        RECT rightRect = { clientRect.right - BorderWidth, clientRect.top + BorderWidth, clientRect.right, clientRect.bottom - BorderWidth };
        FillRect(hdc, &topRect, borderBrush);
        FillRect(hdc, &bottomRect, borderBrush);
        FillRect(hdc, &leftRect, borderBrush);
        FillRect(hdc, &rightRect, borderBrush);
        DeleteObject(borderBrush);
    }

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK OcrResultWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OcrResultWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        auto* pcs = (CREATESTRUCT*)lParam;
        pThis = (OcrResultWindow*)pcs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (OcrResultWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (pThis) return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OcrResultWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (!m_showTitlebar && wParam) {
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        if (!m_showTitlebar) {
            DefWindowProcW(hwnd, msg, FALSE, lParam);
            return TRUE;
        }
        break;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc;
            GetWindowRect(hwnd, &rc);

            if (!m_showTitlebar) {
                const int hitBorder = max(BorderWidth, 6);
                bool isLeft = (pt.x >= rc.left && pt.x < rc.left + hitBorder);
                bool isRight = (pt.x <= rc.right && pt.x > rc.right - hitBorder);
                bool isTop = (pt.y >= rc.top && pt.y < rc.top + hitBorder);
                bool isBottom = (pt.y <= rc.bottom && pt.y > rc.bottom - hitBorder);

                if (isLeft && isTop) return HTTOPLEFT;
                if (isRight && isTop) return HTTOPRIGHT;
                if (isLeft && isBottom) return HTBOTTOMLEFT;
                if (isRight && isBottom) return HTBOTTOMRIGHT;
                if (isLeft) return HTLEFT;
                if (isRight) return HTRIGHT;
                if (isTop) return HTTOP;
                if (isBottom) return HTBOTTOM;
            }

            POINT clientPt = pt;
            ScreenToClient(hwnd, &clientPt);
            RECT editRc;
            if (m_edit && GetWindowRect(m_edit, &editRc)) {
                MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&editRc, 2);
                if (PtInRect(&editRc, clientPt))
                    return HTCLIENT;
            }
            return HTCAPTION;
        }
        return hit;
    }
    case WM_PAINT:
        PaintBorder(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, BgColor);
        SetTextColor(hdc, TextColor);
        static HBRUSH hEditBrush = CreateSolidBrush(BgColor);
        return (LRESULT)hEditBrush;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, BgColor);
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == m_elapsedText || hCtrl == m_hintText)
            SetTextColor(hdc, RGB(160, 160, 160));
        else
            SetTextColor(hdc, StatusColor);
        static HBRUSH hStaticBrush = CreateSolidBrush(BgColor);
        return (LRESULT)hStaticBrush;
    }
    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType != ODT_BUTTON) break;
        bool hovered = (dis->itemState & ODS_HOTLIGHT) != 0;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        COLORREF bg = pressed ? BtnPressedBg : hovered ? BtnHoverBg : BtnNormalBg;
        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, brush);
        DeleteObject(brush);

        HPEN pen = CreatePen(PS_SOLID, 1, BorderColor);
        HPEN oldPen = (HPEN)SelectObject(dis->hDC, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, oldPen);
        SelectObject(dis->hDC, oldBrush);
        DeleteObject(pen);

        wchar_t text[64];
        GetWindowTextW(dis->hwndItem, text, 64);
        SetTextColor(dis->hDC, BtnTextColor);
        SetBkMode(dis->hDC, TRANSPARENT);
        DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_COPY) {
            CopyToClipboard();
        } else if (LOWORD(wParam) == ID_CLOSE) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_TIMER:
        if (wParam == ID_COPY) {
            KillTimer(hwnd, ID_COPY);
            SetWindowTextW(m_copyBtn, S::IsChinese() ? L"\u590D\u5236" : L"Copy");
        }
        return 0;
    case WM_SIZE:
        LayoutControls();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (m_edit && m_editOrigProc) {
            SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)m_editOrigProc);
            m_editOrigProc = nullptr;
        }
        if (m_hUiFont) {
            DeleteObject(m_hUiFont);
            m_hUiFont = nullptr;
        }
        m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
