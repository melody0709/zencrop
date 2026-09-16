#include "TranslationResultWindow.h"

#include "core/ClipboardUtils.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "core/WideStringUtils.h"
#include "ocr/ui/OcrMarkdownPreviewHost.h"
#include "selection/SelectionTypes.h"
#include "window/AlwaysOnTop.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <mutex>

namespace translation {
namespace {

struct AsyncErrorPayload {
    uint64_t workflowGeneration = 0;
    std::wstring message;
    bool retryOcr = false;
};

// Translation result UI has its own 150%-DPI design baseline.  This keeps the
// card rhythm close to the screenshot workflow instead of inheriting the
// compact classic-dialog proportions used by the rest of the Win32 shell.
constexpr UINT kTranslationDesignDpi = 144;
constexpr int kTranslationTextEditMargin = 16;
constexpr int kTranslationMetadataTextInset = 12;
constexpr int kTranslationHeaderIconInset = 12;
constexpr int kTranslationControlRowGap = 6;
constexpr int kTranslationCardFooterHeight = 24;
constexpr int kTranslationCardFooterGap = 4;
constexpr int kTranslationSourceMinHeight = 180;
constexpr int kTranslationTranslationMinHeight = 150;
constexpr int kTranslationSourceDefaultPercent = 36;
constexpr int kTranslationSourceMaxPercent = 50;
constexpr int kTranslationAutomaticMinimumWidth = 800;
// The compact OCR header keeps the selector combos, the OCR route combo and the
// recognize button on a single row, so it needs more width than the shared
// minimum: with every combo at the shared floor (150) that row already measures
// ~890 design units, and the common label set needs ~900. Every other case
// (selected-text translation, bordered windows) keeps the shared minimum.
constexpr int kTranslationCompactOcrMinimumWidth = 940;
// The compact header paints every combo at one width so the row reads as a set:
// a row of four mismatched boxes looks accidental, and the provider must keep
// room for longer profile names even while a short one is selected. The shared
// width follows the longest label currently shown, bounded to [150, 200].
constexpr int kTranslationCompactComboMinWidth = 150;
constexpr int kTranslationCompactComboMaxWidth = 200;
constexpr int kTranslationPreviewMetricSafety = 6;
constexpr ULONGLONG kTranslationResizeAnimationDurationMs = 120;
constexpr UINT kTranslationResizeAnimationFrameMs = 15;

constexpr COLORREF kWindowBackground = RGB(29, 29, 32);
constexpr COLORREF kCardBackground = RGB(9, 9, 10);
constexpr COLORREF kCardBorder = RGB(38, 38, 43);
constexpr COLORREF kTextPrimary = RGB(244, 244, 247);
constexpr COLORREF kTextMuted = RGB(164, 164, 174);
constexpr COLORREF kAccent = RGB(49, 130, 246);
constexpr COLORREF kAccentHover = RGB(68, 145, 255);
constexpr COLORREF kAccentPressed = RGB(36, 107, 218);
constexpr COLORREF kControlBackground = RGB(33, 33, 37);
constexpr COLORREF kControlHover = RGB(46, 46, 52);
constexpr COLORREF kControlPressed = RGB(25, 25, 29);
constexpr COLORREF kControlBorder = RGB(62, 62, 70);
constexpr COLORREF kControlBorderHover = RGB(88, 88, 98);
constexpr COLORREF kControlBorderPressed = RGB(75, 111, 165);
constexpr COLORREF kCloseHover = RGB(145, 49, 53);
constexpr int kDwmWindowCornerPreference = 33;
constexpr int kDwmBorderColor = 34;
constexpr DWORD kDwmRoundCornerPreference = 2;
constexpr UINT kTranslationChildKeyboardMessage = WM_APP + 74;
constexpr UINT kTranslationChildZoomMessage = WM_APP + 75;
constexpr WPARAM kTranslationChildKeyShift = static_cast<WPARAM>(1) << 16;
constexpr WPARAM kTranslationChildKeyControl = static_cast<WPARAM>(1) << 17;
constexpr UINT_PTR kTranslationChildKeyboardSubclass = 1;

// Track visibility through the control's own WS_VISIBLE style, not
// IsWindowVisible(): the latter also reports FALSE for children of a
// not-yet-shown top-level window, so hiding a control before Show() was
// dropped as a no-op. The control then kept both its style and its startup
// geometry and reappeared once the parent was displayed, leaving a stale
// source footer ("Copy" / "Characters: N") over the translation card.
void SetControlVisible(HWND control, bool visible) {
    if (!control) return;
    const bool currentlyVisible =
        (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0;
    if (currentlyVisible == visible) return;
    ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

bool IsIdleStageText(const std::wstring& text) {
    return text.empty() || text == L"Ready" || text == L"\u5c31\u7eea";
}

bool StageLabelNeedsPresentation(HWND control) {
    if (!control) return false;
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>((std::max)(0, length)) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>((std::max)(0, length)));
    return !IsIdleStageText(text);
}

bool SetControlText(HWND control, const std::wstring& text) {
    if (!control) return false;
    const int length = GetWindowTextLengthW(control);
    std::wstring current(static_cast<size_t>((std::max)(0, length)) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, current.data(), length + 1);
    current.resize(static_cast<size_t>((std::max)(0, length)));
    if (current == text) return false;
    SetWindowTextW(control, text.c_str());
    return true;
}

UINT NativeWindowDpi(HWND hwnd) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? dpi : kTranslationDesignDpi;
}

int ScaleForDpi(int value, UINT dpi) {
    return (std::max)(1, MulDiv(value, static_cast<int>(dpi),
        static_cast<int>(kTranslationDesignDpi)));
}

UINT MonitorDpi(HMONITOR monitor) {
    UINT x = 0;
    UINT y = 0;
    if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) && x != 0) {
        return x;
    }
    return kTranslationDesignDpi;
}

SIZE CalculateInitialTranslationWindowSize(
    const RECT& sourceRect, UINT dpi, int minWidth) {
    HMONITOR monitor = MonitorFromRect(&sourceRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return { minWidth, ScaleForDpi(680, dpi) };
    }

    const int minHeight = ScaleForDpi(420, dpi);
    const int maxWidth = (std::max)(minWidth,
        (std::min)(static_cast<int>(monitorInfo.rcWork.right - monitorInfo.rcWork.left) -
                       ScaleForDpi(40, dpi),
                   ScaleForDpi(1100, dpi)));
    const int maxHeight = (std::max)(minHeight,
        static_cast<int>((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) * 3 / 4));
    const int cropWidth = (std::max)(0, static_cast<int>(sourceRect.right - sourceRect.left));
    const int cropHeight = (std::max)(0, static_cast<int>(sourceRect.bottom - sourceRect.top));
    const int cropWidthHint = (std::min)(
        MulDiv(cropWidth, 8, 10), ScaleForDpi(720, dpi));
    const int cropHeightHint = (std::min)(
        MulDiv(cropHeight, 8, 10), ScaleForDpi(520, dpi));
    // Compact headers keep every selector on the title bar row, so the chrome
    // holds exactly one control row regardless of the translation source mode.
    const int compactChromeHeight = ScaleForDpi(
        30 + 3 + kTranslationControlRowGap + 4, dpi);
    const int minimumBodyHeight = (std::max)(0, minHeight - compactChromeHeight);
    const int bodyHeight = (std::max)(minimumBodyHeight, cropHeightHint);
    const int width = (std::clamp)((std::max)(minWidth, cropWidthHint),
        minWidth, maxWidth);
    const int height = (std::clamp)(bodyHeight + compactChromeHeight,
        minHeight, maxHeight);
    return { width, height };
}

int ClampWindowCoordinate(int coordinate, int extent, int workStart, int workEnd, int gap) {
    const int minimum = workStart + gap;
    const int maximum = workEnd - extent - gap;
    if (maximum < minimum) return workStart;
    return (std::max)(minimum, (std::min)(coordinate, maximum));
}

HFONT DefaultFont(UINT dpi) {
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    const UINT systemDpi = GetDpiForSystem();
    if (dpi != 0 && systemDpi != 0 && dpi != systemDpi) {
        if (metrics.lfMessageFont.lfHeight != 0) {
            metrics.lfMessageFont.lfHeight = MulDiv(
                metrics.lfMessageFont.lfHeight, static_cast<int>(dpi),
                static_cast<int>(systemDpi));
        }
        if (metrics.lfMessageFont.lfWidth != 0) {
            metrics.lfMessageFont.lfWidth = MulDiv(
                metrics.lfMessageFont.lfWidth, static_cast<int>(dpi),
                static_cast<int>(systemDpi));
        }
    }
    return CreateFontIndirectW(&metrics.lfMessageFont);
}

HFONT CompactDefaultFont(UINT dpi) {
    HFONT base = DefaultFont(dpi);
    if (!base) return nullptr;
    LOGFONTW logFont = {};
    if (GetObjectW(base, sizeof(logFont), &logFont) <= 0) {
        DeleteObject(base);
        return nullptr;
    }
    DeleteObject(base);

    const int step = ScaleForDpi(1, dpi);
    if (logFont.lfHeight < 0) {
        logFont.lfHeight += static_cast<LONG>(step);
    } else if (logFont.lfHeight > 0) {
        logFont.lfHeight = (std::max)(static_cast<LONG>(1),
            logFont.lfHeight - static_cast<LONG>(step));
    }
    return CreateFontIndirectW(&logFont);
}

HFONT TitleFont(UINT dpi) {
    return CreateFontW(-ScaleForDpi(18, dpi), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

HFONT TextFont(int fontSize, UINT dpi) {
    const int scaledSize = ScaleForDpi((std::clamp)(fontSize, 8, 32), dpi);
    return CreateFontW(-scaledSize, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

// Retain OCR hard line breaks in both cards. The native multiline edit still
// performs automatic soft wrapping for long lines, so reflow follows the card
// width without losing the source layout that the translation must mirror.
std::wstring NormalizeCardTextForWrap(const std::wstring& text) {
    std::wstring normalized;
    normalized.reserve(text.size());
    size_t cursor = 0;
    while (cursor < text.size()) {
        const wchar_t current = text[cursor];
        if (current != L'\r' && current != L'\n') {
            normalized.push_back(current);
            ++cursor;
            continue;
        }
        if (current == L'\r' && cursor + 1 < text.size() && text[cursor + 1] == L'\n') {
            ++cursor;
        }
        normalized += L"\r\n";
        ++cursor;
    }
    return normalized;
}

int MeasureWrappedTextHeight(HDC hdc, HFONT font, const std::wstring& text, int width) {
    if (!hdc || !font || width <= 0) return 0;
    HGDIOBJ previousFont = SelectObject(hdc, font);
    TEXTMETRICW metrics = {};
    const int lineHeight = GetTextMetricsW(hdc, &metrics)
        ? metrics.tmHeight + metrics.tmExternalLeading : 1;
    std::wstring measuredText = text.empty() ? L" " : text;
    RECT measured = { 0, 0, width, lineHeight };
    DrawTextW(hdc, measuredText.c_str(), -1, &measured,
        DT_CALCRECT | DT_EDITCONTROL | DT_NOPREFIX | DT_WORDBREAK);
    SelectObject(hdc, previousFont);
    return (std::max)(lineHeight,
        static_cast<int>(measured.bottom - measured.top));
}

int MeasureUsefulTextWidth(HDC hdc, HFONT font, const std::wstring& text, int maxWidth) {
    if (!hdc || !font || maxWidth <= 0) return 0;
    HGDIOBJ previousFont = SelectObject(hdc, font);
    int longest = 0;
    size_t lineStart = 0;
    for (size_t index = 0; index <= text.size(); ++index) {
        const bool lineEnd = index == text.size() || text[index] == L'\r' ||
            text[index] == L'\n';
        if (!lineEnd) continue;
        if (index > lineStart) {
            SIZE lineSize = {};
            GetTextExtentPoint32W(hdc, text.c_str() + lineStart,
                static_cast<int>(index - lineStart), &lineSize);
            longest = (std::max)(longest, static_cast<int>(lineSize.cx));
        }
        if (index < text.size() && text[index] == L'\r' &&
            index + 1 < text.size() && text[index + 1] == L'\n') {
            ++index;
        }
        lineStart = index + 1;
    }
    SelectObject(hdc, previousFont);
    return (std::min)(longest, maxWidth);
}

std::wstring CharacterCountText(const std::wstring& text) {
    return (S::IsChinese() ? L"\u5b57\u7b26: " : L"Characters: ") +
        std::to_wstring(text.size());
}

// OCR engine display names mirror the Settings > OCR "Mode" wording, so the
// translation window's OCR route reads exactly like the mode picked there.
// "paddle_local" (PaddleOCR-VL with document parsing off) is not offered by the
// route menu -- its local entry is the document-parsing variant -- but it can
// still be the resolved engine, so it keeps an explicit suffix.
std::wstring FriendlyOcrProviderLabelImpl(const std::wstring& provider) {
    if (provider == L"local") {
        return S::IsChinese() ? L"\u672c\u5730\uff08Windows OCR\uff09"
                              : L"Local (Windows OCR)";
    }
    if (provider == L"paddle_local_doc") {
        // The route menu keeps the full Settings wording ("PaddleOCR-VL 1.6
        // Local"); the button/badge drops the " Local" suffix to fit the shared
        // one-row header.
        return L"PaddleOCR-VL 1.6";
    }
    if (provider == L"paddle_local") {
        return S::IsChinese() ? L"PaddleOCR-VL 1.6 \u56fe\u50cf"
                              : L"PaddleOCR-VL 1.6 Image";
    }
    if (provider == L"paddle_cloud") {
        return S::IsChinese() ? L"PaddleOCR \u4e91\u7aef" : L"PaddleOCR Cloud";
    }
    if (provider == L"ppocrv6_onnx") {
        return S::IsChinese() ? L"PP-OCRv6 \u672c\u5730" : L"PP-OCRv6 Local";
    }
    return provider;
}

std::wstring CompactOcrRouteLabel(const std::wstring& route) {
    if (route == L"current") {
        return S::IsChinese() ? L"\u5f53\u524d\u8bbe\u7f6e" : L"Current settings";
    }
    return FriendlyOcrProviderLabelImpl(route);
}

std::wstring OcrRouteButtonLabel(const std::wstring& route) {
    return (S::IsChinese() ? L"OCR\uff1a" : L"OCR: ") + CompactOcrRouteLabel(route);
}

std::wstring ProviderButtonLabel(const std::wstring& displayName) {
    return (S::IsChinese() ? L"Provider\uff1a" : L"Provider: ") + displayName;
}

void ConfigureCompactPopupMenu(HMENU menu) {
    if (!menu) return;
    MENUINFO info = { sizeof(info) };
    info.fMask = MIM_STYLE;
    info.dwStyle = MNS_NOCHECK | MNS_AUTODISMISS;
    SetMenuInfo(menu, &info);
}

bool AppendCompactPopupItem(HMENU menu, UINT id, const std::wstring& label) {
    if (!menu) return false;
    // The owner-draw menu is modal and the backing vectors remain unchanged
    // until TrackPopupMenuEx returns; callers must preserve that lifetime.
    MENUITEMINFOW item = { sizeof(item) };
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA;
    item.fType = MFT_OWNERDRAW;
    item.wID = id;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(label.c_str());
    return InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &item) != FALSE;
}

void MeasureCompactPopupItem(MEASUREITEMSTRUCT& measure, HFONT font, UINT dpi,
                             int anchorWidth = 0, bool anchorIsEngineLabel = false) {
    const wchar_t* label = reinterpret_cast<const wchar_t*>(measure.itemData);
    if (!label || !font) return;
    HDC dc = GetDC(nullptr);
    if (!dc) return;
    HGDIOBJ previousFont = SelectObject(dc, font);
    SIZE textSize = {};
    GetTextExtentPoint32W(dc, label, lstrlenW(label), &textSize);
    TEXTMETRICW metrics = {};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previousFont);
    ReleaseDC(nullptr, dc);

    const int horizontalPadding = ScaleForDpi(24, dpi);
    const int verticalPadding = ScaleForDpi(8, dpi);
    const int textWidth = static_cast<int>(textSize.cx);
    const int lineHeight = static_cast<int>(metrics.tmHeight);
    int itemWidth = (std::max)(ScaleForDpi(80, dpi), textWidth + horizontalPadding);
    if (anchorWidth > 0) {
        const int textPadding = ScaleForDpi(anchorIsEngineLabel ? 8 : 10, dpi);
        const int arrowWidth = ScaleForDpi(22, dpi);
        const int arrowMargin = ScaleForDpi(4, dpi);
        const int menuOverhang = ScaleForDpi(10, dpi);
        const int anchorTextWidth = (std::max)(
            0, anchorWidth - textPadding - arrowWidth - arrowMargin - menuOverhang);
        itemWidth = (std::max)(itemWidth, anchorTextWidth + horizontalPadding);
    }
    measure.itemWidth = static_cast<UINT>(itemWidth);
    measure.itemHeight = static_cast<UINT>((std::max)(
        ScaleForDpi(24, dpi), lineHeight + verticalPadding));
}

void DrawCompactPopupItem(const DRAWITEMSTRUCT& draw, HFONT font, UINT dpi) {
    const wchar_t* label = reinterpret_cast<const wchar_t*>(draw.itemData);
    if (!label) return;
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const COLORREF background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_MENU);
    const COLORREF foreground = GetSysColor(disabled
        ? COLOR_GRAYTEXT : (selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(draw.hDC, &draw.rcItem, brush);
    DeleteObject(brush);

    RECT text = draw.rcItem;
    const int padding = ScaleForDpi(12, dpi);
    text.left += padding;
    text.right -= padding;
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, foreground);
    HGDIOBJ previousFont = font ? SelectObject(draw.hDC, font) : nullptr;
    DrawTextW(draw.hDC, label, -1, &text,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont) SelectObject(draw.hDC, previousFont);
}

void FillRoundedRect(HDC hdc, const RECT& rect, int radius,
                     COLORREF fill, COLORREF border) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom,
        radius * 2, radius * 2);

    // GDI's RoundRect can leave the long edges looking slightly uneven when
    // a rounded control is painted next to clipped child windows. Redraw the
    // straight portions explicitly so card/control separators remain crisp.
    const int rectWidth = static_cast<int>(rect.right - rect.left);
    const int rectHeight = static_cast<int>(rect.bottom - rect.top);
    const int edgeRadius = (std::min)(radius, (std::min)(rectWidth, rectHeight) / 2);
    MoveToEx(hdc, rect.left + edgeRadius, rect.top, nullptr);
    LineTo(hdc, rect.right - edgeRadius, rect.top);
    MoveToEx(hdc, rect.left + edgeRadius, rect.bottom - 1, nullptr);
    LineTo(hdc, rect.right - edgeRadius, rect.bottom - 1);
    MoveToEx(hdc, rect.left, rect.top + edgeRadius, nullptr);
    LineTo(hdc, rect.left, rect.bottom - edgeRadius - 1);
    MoveToEx(hdc, rect.right - 1, rect.top + edgeRadius, nullptr);
    LineTo(hdc, rect.right - 1, rect.bottom - edgeRadius - 1);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

bool PointInChild(HWND parent, HWND child, POINT point) {
    if (!child || !IsWindowVisible(child)) return false;
    RECT rect = {};
    if (!GetWindowRect(child, &rect)) return false;
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rect), 2);
    return PtInRect(&rect, point) != FALSE;
}

POINT CalculateWindowPositionNearSource(const RECT& sourceRect, int windowWidth,
                                        int windowHeight) {
    HMONITOR monitor = MonitorFromRect(&sourceRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    const int gap = ScaleForDpi(10, MonitorDpi(monitor));
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return { sourceRect.left, sourceRect.bottom + gap };
    }

    const bool belowFits = sourceRect.bottom + gap + windowHeight <= monitorInfo.rcWork.bottom;
    const bool aboveFits = sourceRect.top - gap - windowHeight >= monitorInfo.rcWork.top;
    POINT position = {};
    if (!belowFits && !aboveFits) {
        position.x = sourceRect.right + gap;
        position.y = sourceRect.top;
        if (position.x + windowWidth > monitorInfo.rcWork.right) {
            position.x = sourceRect.left - windowWidth - gap;
        }
    } else if (belowFits) {
        position.x = sourceRect.left;
        position.y = sourceRect.bottom + gap;
    } else {
        position.x = sourceRect.left;
        position.y = sourceRect.top - windowHeight - gap;
    }

    position.x = ClampWindowCoordinate(position.x, windowWidth,
        monitorInfo.rcWork.left, monitorInfo.rcWork.right, gap);
    position.y = ClampWindowCoordinate(position.y, windowHeight,
        monitorInfo.rcWork.top, monitorInfo.rcWork.bottom, gap);
    return position;
}

LRESULT CALLBACK TranslationChildKeyboardProc(HWND hwnd, UINT message,
                                              WPARAM wParam, LPARAM lParam,
                                              UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEWHEEL &&
        (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            SendMessageW(parent, kTranslationChildZoomMessage,
                GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : static_cast<WPARAM>(-1),
                reinterpret_cast<LPARAM>(hwnd));
        }
        return 0;
    }
    if (message == WM_KEYDOWN &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        (wParam == VK_OEM_PLUS || wParam == VK_ADD ||
         wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT || wParam == L'0')) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            const WPARAM step = (wParam == VK_OEM_PLUS || wParam == VK_ADD)
                ? 1
                : (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT)
                    ? static_cast<WPARAM>(-1)
                    : 0;
            SendMessageW(parent, kTranslationChildZoomMessage, step,
                reinterpret_cast<LPARAM>(hwnd));
        }
        return 0;
    }
    if (message == WM_KEYDOWN &&
        (wParam == VK_TAB || wParam == VK_ESCAPE ||
         (wParam == L'W' && (GetKeyState(VK_CONTROL) & 0x8000) != 0))) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            WPARAM key = wParam;
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) key |= kTranslationChildKeyShift;
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) key |= kTranslationChildKeyControl;
            SendMessageW(parent, kTranslationChildKeyboardMessage, key,
                reinterpret_cast<LPARAM>(hwnd));
        }
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace

std::wstring FriendlyOcrProviderLabel(const std::wstring& provider) {
    return FriendlyOcrProviderLabelImpl(provider);
}

const wchar_t* TranslationResultWindow::ClassName() {
    return L"ZenCrop.TranslationResultWindow";
}

void TranslationResultWindow::RegisterClass() {
    static std::once_flag once;
    std::call_once(once, [] {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &TranslationResultWindow::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = ClassName();
        RegisterClassExW(&wc);
    });
}

TranslationResultWindow::TranslationResultWindow(
    const TranslationRequest& request,
    const TranslationLaunchContext& context,
    CommandCallback callback)
    : layoutDpi_(kTranslationDesignDpi), sourceMode_(context.mode),
      sourceRect_(context.anchorRect), callback_(std::move(callback)) {
    RegisterClass();
    const TranslationSettings initialSettings = LoadTranslationSettings();
    sourcePreviewZoomFactor_ = (std::clamp)(initialSettings.sourcePreviewZoomFactor,
        kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);
    translationPreviewZoomFactor_ = (std::clamp)(
        initialSettings.translationPreviewZoomFactor,
        kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);
    sourceFontSize_ = (std::clamp)(initialSettings.sourceFontSize,
        kTranslationSourceFontSizeMin, kTranslationSourceFontSizeMax);
    sourceEditFontSize_ = sourceFontSize_;
    const UINT initialDpi = MonitorDpi(
        MonitorFromRect(&sourceRect_, MONITOR_DEFAULTTONEAREST));
    const SIZE initialSize = CalculateInitialTranslationWindowSize(
        sourceRect_, initialDpi, MinimumWindowWidth(initialDpi));
    window_ = CreateWindowExW(
        // Keep the result window in the taskbar even after Show() assigns the
        // durable application window as its owner.
        WS_EX_APPWINDOW,
        ClassName(),
        sourceMode_ == TranslationSourceMode::SelectedText
            ? (S::IsChinese() ? L"ZenCrop \u5212\u8bcd\u7ffb\u8bd1" :
                L"ZenCrop Selection Translate")
            : (S::IsChinese() ? L"ZenCrop \u7ffb\u8bd1" : L"ZenCrop Translate"),
        // A frameless popup needs these styles for standard taskbar
        // click-to-minimize behavior.
        WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, initialSize.cx, initialSize.cy,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (window_) {
        SetLayoutDpi(NativeWindowDpi(window_));
        ApplyDarkWindowChrome();
        CreateControls(request);
        const auto persistPreviewZoomFactor = [this](bool sourcePreview,
                                                       double zoomFactor) {
            if (!std::isfinite(zoomFactor)) return;
            zoomFactor = (std::clamp)(zoomFactor,
                kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);
            double& currentZoomFactor = sourcePreview
                ? sourcePreviewZoomFactor_
                : translationPreviewZoomFactor_;
            if (std::abs(currentZoomFactor - zoomFactor) < 0.001) return;
            currentZoomFactor = zoomFactor;
            TranslationSettings settings = LoadTranslationSettings();
            if (!settings.schemaSupported) return;
            if (sourcePreview) {
                settings.sourcePreviewZoomFactor = zoomFactor;
            } else {
                settings.translationPreviewZoomFactor = zoomFactor;
            }
            SaveTranslationSettings(settings);
        };
        const auto handlePreviewAccelerator = [this](UINT virtualKey, bool ctrlDown) {
            if (!ctrlDown || virtualKey != L'W' || !window_ || !IsWindow(window_)) {
                return false;
            }
            SendMessageW(window_, WM_CLOSE, 0, 0);
            return true;
        };
        // Both OCR and selected-text launches use the same source preview.
        // Selected text still keeps the editable Source mode so users can
        // correct the text before translating again.
        {
            sourcePreview_ = std::make_unique<OcrMarkdownPreviewHost>();
            OcrMarkdownPreviewHost::Callbacks sourcePreviewCallbacks;
            sourcePreviewCallbacks.onAcceleratorKey = handlePreviewAccelerator;
            sourcePreviewCallbacks.onZoomFactorChanged =
                [persistPreviewZoomFactor](double zoomFactor) {
                    persistPreviewZoomFactor(true, zoomFactor);
                };
            sourcePreviewCallbacks.onPreviewEditorState = [this](
                const OcrMarkdownPreviewHost::PreviewEditorState& state) {
                sourcePreviewEditorActive_ = state.active;
                sourcePreviewEditorContentUnits_ = state.contentUtf16Units;
                sourcePreviewEditorHasText_ = state.hasNonWhitespace;
                UpdateSourceModeButton();
                UpdateSourceEditorFooterActions();
                UpdateActionAvailability();
                if (!state.active) CompleteTextEntryAfterEditorClose();
            };
            sourcePreviewCallbacks.onReady = [this]() {
                sourcePreviewFailed_ = false;
                if (sourcePreview_) {
                    sourcePreview_->SetBounds(sourceContentRect_);
                    sourcePreview_->RenderMarkdown(-1, sourceMarkdownText_, true);
                }
                UpdateSourceModeButton();
                UpdateSourcePreviewVisibility();
            };
            sourcePreviewCallbacks.onContentMetrics = [this](
                const OcrMarkdownPreviewHost::PreviewContentMetrics& metrics) {
                if (!showSourceText_) return;
                sourcePreviewMetricsValid_ = true;
                sourcePreviewContentHeight_ = metrics.scrollHeight;
                if (!sourcePreviewRenderReady_) {
                    sourcePreviewRenderReady_ = true;
                    UpdateSourcePreviewVisibility();
                    StartPendingTextEntry();
                    if (!sourceMarkdownText_.empty()) ResizeToAutomaticWindowSize();
                }
            };
            sourcePreviewCallbacks.onUnavailable = [this](const std::wstring&) {
                sourcePreviewFailed_ = true;
                sourcePreviewMetricsValid_ = false;
                sourcePreviewRenderReady_ = false;
                sourceDisplayMode_ = SourceDisplayMode::Source;
                manualEntryPending_ = false;
                UpdateSourceModeButton();
                UpdateSourcePreviewVisibility();
                if (manualEntryMode_ && sourceEdit_) SetFocus(sourceEdit_);
                if (!sourceMarkdownText_.empty()) ResizeToAutomaticWindowSize();
            };
            sourcePreviewCallbacks.onRenderError = [this](int, const std::wstring&) {
                sourcePreviewFailed_ = true;
                sourcePreviewMetricsValid_ = false;
                sourcePreviewRenderReady_ = false;
                sourceDisplayMode_ = SourceDisplayMode::Source;
                manualEntryPending_ = false;
                UpdateSourceModeButton();
                UpdateSourcePreviewVisibility();
                if (!sourceMarkdownText_.empty()) ResizeToAutomaticWindowSize();
            };
            sourcePreviewCallbacks.onProcessFailed = [this]() {
                sourcePreviewFailed_ = true;
                sourcePreviewMetricsValid_ = false;
                sourcePreviewRenderReady_ = false;
                sourceDisplayMode_ = SourceDisplayMode::Source;
                manualEntryPending_ = false;
                UpdateSourceModeButton();
                UpdateSourcePreviewVisibility();
                if (!sourceMarkdownText_.empty()) ResizeToAutomaticWindowSize();
            };
            sourcePreviewCallbacks.onPreviewDocumentEdit = [this](bool sourceRequired) {
                if (busy_) return;
                if (manualEntryMode_ && SourceText().empty()) {
                    sourcePreview_->StartDocumentEditing(false);
                } else if (sourceRequired) {
                    SetSourceDisplayMode(SourceDisplayMode::Source, true);
                } else if (sourcePreview_ && sourceDisplayMode_ == SourceDisplayMode::Preview) {
                    sourcePreview_->StartDocumentEditing();
                }
            };
            sourcePreviewCallbacks.onPreviewDocumentSave = [this](
                const std::wstring& content,
                const std::wstring& renderToken) {
                if (!sourcePreview_) return;
                if (busy_) {
                    switchToSourceAfterDocumentSave_ = false;
                    sourcePreview_->PostPreviewDocumentSaveResult(renderToken, false, L"busy");
                    return;
                }
                const std::wstring normalized = NormalizeCardTextForWrap(content);
                if (manualEntryMode_ &&
                    !selection::HasNonWhitespace(normalized)) {
                    sourcePreview_->PostPreviewDocumentSaveResult(
                        renderToken, false, L"empty_text");
                    return;
                }
                if (manualEntryMode_ &&
                    (normalized.size() > selection::kMaxSelectionTextUnits ||
                     !selection::IsValidSelectionUtf16(normalized))) {
                    sourcePreview_->PostPreviewDocumentSaveResult(
                        renderToken, false, L"too_large");
                    return;
                }
                const bool translateAfterSave =
                    manualEntryMode_ && !switchToSourceAfterDocumentSave_;
                if (translateAfterSave) {
                    translateAfterEditorClose_ = true;
                    translateEntryGeneration_ = manualEntryGeneration_;
                }
                sourceMarkdownText_ = normalized;
                suppressCommands_ = true;
                SetControlText(sourceEdit_, normalized);
                suppressCommands_ = false;
                SetControlText(sourceCountLabel_, CharacterCountText(normalized));
                UpdateActionAvailability();
                MarkDirty();
                sourcePreview_->PostPreviewDocumentSaveResult(renderToken, true);

                const bool switchMode = switchToSourceAfterDocumentSave_;
                switchToSourceAfterDocumentSave_ = false;
                if (switchMode) {
                    resolvingDocumentEditorSwitch_ = true;
                    SetSourceDisplayMode(SourceDisplayMode::Source, true);
                    resolvingDocumentEditorSwitch_ = false;
                } else if (!translateAfterSave) {
                    sourcePreviewMetricsValid_ = false;
                    sourcePreviewRenderReady_ = false;
                    sourcePreview_->RenderMarkdown(-1, sourceMarkdownText_, true);
                }
                ResizeToAutomaticWindowSize();
            };
            sourcePreviewCallbacks.onPreviewDocumentCancel = [this]() {
                switchToSourceAfterDocumentSave_ = false;
                if (cancelManualEditorRequested_) {
                    cancelManualEditorRequested_ = false;
                    translateAfterEditorClose_ = false;
                    if (manualEntryMode_ && !SourceText().empty()) EndTextEntry();
                }
                UpdateSourceEditorFooterActions();
            };
            sourcePreviewCallbacks.onPreviewSelectionState =
                [this](bool hasSelection, uint64_t generation) {
                    UpdatePreviewSelectionState(
                        PreviewSelectionHost::Source,
                        hasSelection, generation);
                };
            sourcePreviewCallbacks.onStructuredSelectionPrepared =
                [this](const std::wstring& token, uint64_t generation,
                       bool success, const std::wstring& planJson,
                       const std::wstring& errorCode) {
                    HandleStructuredSelectionPrepared(
                        PreviewSelectionHost::Source, token, generation,
                        success, planJson, errorCode);
                };
            if (!sourcePreview_->Create(window_, sourceContentRect_,
                std::move(sourcePreviewCallbacks))) {
                sourcePreviewFailed_ = true;
                sourceDisplayMode_ = SourceDisplayMode::Source;
                sourcePreview_.reset();
            } else {
                sourcePreview_->SetZoomFactor(sourcePreviewZoomFactor_);
                sourcePreview_->SetTextFontSize(sourceFontSize_);
                sourcePreview_->Show(false);
            }
        }
        UpdateSourceModeButton();
        translationPreview_ = std::make_unique<OcrMarkdownPreviewHost>();
        OcrMarkdownPreviewHost::Callbacks previewCallbacks;
        previewCallbacks.onAcceleratorKey = handlePreviewAccelerator;
        previewCallbacks.onZoomFactorChanged =
            [persistPreviewZoomFactor](double zoomFactor) {
                persistPreviewZoomFactor(false, zoomFactor);
            };
        previewCallbacks.onReady = [this]() {
            translationPreviewFailed_ = false;
            if (translationPreview_) {
                translationPreview_->SetBounds(translationContentRect_);
                translationPreview_->RenderMarkdown(-1, translationMarkdownText_, true);
                UpdateTranslationPreviewVisibility();
            }
        };
        previewCallbacks.onContentMetrics = [this](
            const OcrMarkdownPreviewHost::PreviewContentMetrics& metrics) {
            translationPreviewMetricsValid_ = true;
            translationPreviewContentHeight_ = metrics.scrollHeight;
            if (!translationPreviewRenderReady_) {
                translationPreviewRenderReady_ = true;
                UpdateTranslationPreviewVisibility();
                if (!translationMarkdownText_.empty()) ResizeToAutomaticWindowSize();
            }
        };
        previewCallbacks.onUnavailable = [this](const std::wstring&) {
            translationPreviewFailed_ = true;
            translationPreviewMetricsValid_ = false;
            translationPreviewRenderReady_ = false;
            UpdateTranslationPreviewVisibility();
            if (!translationMarkdownText_.empty()) ResizeToAutomaticWindowSize();
        };
        previewCallbacks.onRenderError = [this](int, const std::wstring&) {
            translationPreviewFailed_ = true;
            translationPreviewMetricsValid_ = false;
            translationPreviewRenderReady_ = false;
            UpdateTranslationPreviewVisibility();
            if (!translationMarkdownText_.empty()) ResizeToAutomaticWindowSize();
        };
        previewCallbacks.onProcessFailed = [this]() {
            translationPreviewFailed_ = true;
            translationPreviewMetricsValid_ = false;
            translationPreviewRenderReady_ = false;
            UpdateTranslationPreviewVisibility();
            if (!translationMarkdownText_.empty()) ResizeToAutomaticWindowSize();
        };
        previewCallbacks.onPreviewSelectionState =
            [this](bool hasSelection, uint64_t generation) {
                UpdatePreviewSelectionState(
                    PreviewSelectionHost::Translation,
                    hasSelection, generation);
            };
        previewCallbacks.onStructuredSelectionPrepared =
            [this](const std::wstring& token, uint64_t generation,
                   bool success, const std::wstring& planJson,
                   const std::wstring& errorCode) {
                HandleStructuredSelectionPrepared(
                    PreviewSelectionHost::Translation, token, generation,
                    success, planJson, errorCode);
            };
        if (!translationPreview_->Create(window_, translationContentRect_,
            std::move(previewCallbacks))) {
            translationPreviewFailed_ = true;
            translationPreview_.reset();
        } else {
            translationPreview_->SetZoomFactor(translationPreviewZoomFactor_);
            translationPreview_->Show(false);
        }
        const UINT dpi = LayoutDpi();
        const SIZE layoutSize = CalculateInitialTranslationWindowSize(
            sourceRect_, dpi, MinimumWindowWidth(dpi));
        RECT currentRect = {};
        if (GetWindowRect(window_, &currentRect) &&
            (currentRect.right - currentRect.left != layoutSize.cx ||
             currentRect.bottom - currentRect.top != layoutSize.cy)) {
            SetWindowPos(window_, nullptr, 0, 0, layoutSize.cx, layoutSize.cy,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

bool TranslationResultWindow::PostAsyncError(
    HWND window, uint64_t workflowGeneration,
    const std::wstring& message, bool retryOcr) {
    if (!window || !IsWindow(window)) return false;
    AsyncErrorPayload* payload = nullptr;
    try {
        payload = new AsyncErrorPayload{workflowGeneration, message, retryOcr};
    } catch (...) {
        return false;
    }
    if (!PostMessageW(window, kAsyncErrorMessage, 0,
                      reinterpret_cast<LPARAM>(payload))) {
        delete payload;
        return false;
    }
    return true;
}

UINT TranslationResultWindow::LayoutDpi() const {
    return layoutDpi_ ? layoutDpi_ : kTranslationDesignDpi;
}

void TranslationResultWindow::SetLayoutDpi(UINT dpi) {
    if (dpi != 0) layoutDpi_ = dpi;
}

TranslationResultWindow::~TranslationResultWindow() {
    callback_ = {};
    const HWND windowHandle = window_;
    if (windowHandle && IsWindow(windowHandle)) {
        KillTimer(windowHandle, kResizeAnimationTimer);
    }
    const auto drainAsyncErrors = [](HWND filter) {
        MSG message = {};
        while (PeekMessageW(&message, filter,
            kAsyncErrorMessage, kAsyncErrorMessage, PM_REMOVE)) {
            delete reinterpret_cast<AsyncErrorPayload*>(message.lParam);
        }
    };
    // A coordinator failure can enqueue an async error immediately before the
    // result window is closed. Remove those heap payloads on the UI thread so
    // DestroyWindow cannot leave an undelivered lParam behind.
    {
        // Prefer an HWND-filtered drain. After WM_NCDESTROY the handle is no
        // longer usable, but this message id is private to result windows and
        // its payload type is uniform, so a thread-queue drain still closes
        // the ownership gap without touching unrelated application messages.
        const HWND filter = (windowHandle && IsWindow(windowHandle))
            ? windowHandle : nullptr;
        drainAsyncErrors(filter);
    }
    if (translationPreview_) {
        translationPreview_->Destroy();
        translationPreview_.reset();
    }
    if (sourcePreview_) {
        sourcePreview_->Destroy();
        sourcePreview_.reset();
    }
    if (pinToolTip_ && IsWindow(pinToolTip_)) DestroyWindow(pinToolTip_);
    pinToolTip_ = nullptr;
    if (windowHandle && IsWindow(windowHandle)) DestroyWindow(windowHandle);
    // A callback can race the first drain while DestroyWindow is dispatching
    // WM_DESTROY/WM_NCDESTROY. Once the handle is gone, private result-window
    // messages have no receiver; drain them from this UI thread as the final
    // ownership boundary.
    drainAsyncErrors(nullptr);
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (compactFont_) {
        DeleteObject(compactFont_);
        compactFont_ = nullptr;
    }
    if (titleFont_) {
        DeleteObject(titleFont_);
        titleFont_ = nullptr;
    }
    if (textFont_) {
        DeleteObject(textFont_);
        textFont_ = nullptr;
    }
    if (sourceTextFont_) {
        DeleteObject(sourceTextFont_);
        sourceTextFont_ = nullptr;
    }
}

void TranslationResultWindow::ApplyDarkWindowChrome() {
    if (!window_) return;
    const BOOL dark = TRUE;
    const COLORREF borderColor = showWindowBorder_ ? kCardBorder : kWindowBackground;
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark, sizeof(dark));
    DwmSetWindowAttribute(window_, kDwmWindowCornerPreference,
        &kDwmRoundCornerPreference, sizeof(kDwmRoundCornerPreference));
    DwmSetWindowAttribute(window_, kDwmBorderColor,
        &borderColor, sizeof(borderColor));
}

void TranslationResultWindow::CreateControls(const TranslationRequest& request) {
    font_ = DefaultFont(LayoutDpi());
    compactFont_ = CompactDefaultFont(LayoutDpi());
    titleFont_ = TitleFont(LayoutDpi());
    textFontSize_ = (std::clamp)(LoadOcrSettings().ocrFontSize, 8, 32);
    textFont_ = TextFont(textFontSize_, LayoutDpi());
    sourceTextFont_ = TextFont(sourceEditFontSize_, LayoutDpi());
    auto create = [&](DWORD exStyle, const wchar_t* cls, const wchar_t* text,
                      DWORD style, int id) {
        HWND control = CreateWindowExW(exStyle, cls, text, style,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (control && font_) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        return control;
    };
    const DWORD staticStyle = WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_NOPREFIX;
    const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;

    stageLabel_ = create(0, L"STATIC",
        sourceMode_ == TranslationSourceMode::SelectedText
            ? (S::IsChinese() ? L"\u6b63\u5728\u7ffb\u8bd1\u2026" : L"Translating...")
            : (S::IsChinese() ? L"\u6b63\u5728\u8bc6\u522b\u6587\u5b57\u2026" :
                L"Recognizing text..."),
        staticStyle, kStageLabel);
    if (sourceMode_ == TranslationSourceMode::OcrImage) {
        engineLabel_ = create(0, L"BUTTON", L"", buttonStyle, kEngineLabel);
    }
    targetLabel_ = create(0, L"STATIC", L"\u2192", staticStyle | SS_CENTER,
        kTargetLabel);
    showSourceToggle_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u539f\u6587" : L"Source",
        buttonStyle, kShowSource);

    sourceCombo_ = create(0, L"BUTTON", L"", buttonStyle, kSourceCombo);
    targetCombo_ = create(0, L"BUTTON", L"", buttonStyle, kTargetCombo);
    providerCombo_ = create(0, L"BUTTON", L"", buttonStyle, kProviderCombo);

    for (HWND control : {stageLabel_, engineLabel_, targetLabel_,
                         showSourceToggle_, sourceCombo_, targetCombo_,
                         providerCombo_}) {
        if (control && compactFont_) {
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(compactFont_), TRUE);
        }
    }

    sourceCountLabel_ = create(0, L"STATIC", CharacterCountText(L"").c_str(),
        staticStyle, kSourceCount);
    translationCountLabel_ = create(0, L"STATIC", CharacterCountText(L"").c_str(),
        staticStyle, kTranslationCount);
    translationElapsedLabel_ = create(0, L"STATIC", L"", staticStyle | SS_RIGHT,
        kTranslationElapsed);
    sourceEdit_ = create(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL, kSourceEdit);
    translationEdit_ = create(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_WANTRETURN | ES_NOHIDESEL,
        kTranslationEdit);
    if (textFont_) {
        if (sourceTextFont_) {
            SendMessageW(sourceEdit_, WM_SETFONT,
                reinterpret_cast<WPARAM>(sourceTextFont_), TRUE);
        }
        SendMessageW(translationEdit_, WM_SETFONT,
            reinterpret_cast<WPARAM>(textFont_), TRUE);
    }
    ShowWindow(translationElapsedLabel_, SW_HIDE);

    copySourceButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u590d\u5236" : L"Copy", buttonStyle, kCopySource);
    copyTranslationButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u590d\u5236" : L"Copy", buttonStyle, kCopyTranslation);
    sourceEditorCancelButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u53d6\u6d88" : L"Cancel", buttonStyle, kSourceEditorCancel);
    sourceEditorSaveButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u4fdd\u5b58" : L"Save", buttonStyle, kSourceEditorSave);
    ShowWindow(sourceEditorCancelButton_, SW_HIDE);
    ShowWindow(sourceEditorSaveButton_, SW_HIDE);
    if (sourceMode_ == TranslationSourceMode::OcrImage) {
        recognizeButton_ = create(0, L"BUTTON",
            S::IsChinese() ? L"\u91cd\u65b0\u8bc6\u522b" : L"Recognize again",
            buttonStyle, kRecognizeAgain);
    }
    retranslateButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u91cd\u65b0\u7ffb\u8bd1" : L"Translate again",
        buttonStyle, kRetranslate);
    cancelButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u53d6\u6d88" : L"Cancel", buttonStyle, kCancel);
    pinButton_ = create(0, L"BUTTON", L"", buttonStyle, kPin);
    sourceModeButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"\u539f\u6587" : L"Source", buttonStyle,
        kSourceMode);
    minimizeButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"最小化" : L"Minimize", buttonStyle, kMinimize);
    closeButton_ = create(0, L"BUTTON",
        S::IsChinese() ? L"关闭" : L"Close", buttonStyle, kClose);

    for (HWND control : {sourceEdit_, translationEdit_, engineLabel_, showSourceToggle_, sourceCombo_,
                          targetCombo_, providerCombo_, copySourceButton_, copyTranslationButton_,
                          sourceEditorCancelButton_, sourceEditorSaveButton_, recognizeButton_,
                          retranslateButton_, cancelButton_, pinButton_,
                         sourceModeButton_, minimizeButton_, closeButton_}) {
        if (control) {
            SetWindowSubclass(control, TranslationChildKeyboardProc,
                kTranslationChildKeyboardSubclass, 0);
        }
    }
    if (sourceModeButton_ && compactFont_) {
        SendMessageW(sourceModeButton_, WM_SETFONT,
            reinterpret_cast<WPARAM>(compactFont_), TRUE);
    }
    UpdatePinAccessibleState();
    pinToolTip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (pinToolTip_ && pinButton_) {
        TOOLINFOW tool = { sizeof(tool) };
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = window_;
        tool.uId = reinterpret_cast<UINT_PTR>(pinButton_);
        tool.lpszText = const_cast<wchar_t*>(pinToolTipText_.c_str());
        SendMessageW(pinToolTip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    if (pinToolTip_ && recognizeButton_) {
        TOOLINFOW tool = { sizeof(tool) };
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = window_;
        tool.uId = reinterpret_cast<UINT_PTR>(recognizeButton_);
        tool.lpszText = const_cast<wchar_t*>(S::IsChinese()
            ? L"\u91cd\u65b0\u8bc6\u522b" : L"Recognize again");
        SendMessageW(pinToolTip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    for (HWND control : { sourceEdit_, translationEdit_ }) {
        if (control) SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
    const int editMargin = ScaleForDpi(kTranslationTextEditMargin, LayoutDpi());
    SendMessageW(sourceEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(editMargin, editMargin));
    SendMessageW(translationEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(editMargin, editMargin));

    AddLanguage(sourceLanguages_, S::IsChinese() ? L"\u81ea\u52a8\u68c0\u6d4b" : L"Auto detect", L"auto");
    AddLanguage(sourceLanguages_, S::IsChinese() ? L"\u7b80\u4f53\u4e2d\u6587" : L"Simplified Chinese", L"zh-Hans");
    AddLanguage(sourceLanguages_, L"English", L"en");
    AddLanguage(sourceLanguages_, S::IsChinese() ? L"\u7e41\u9ad4\u4e2d\u6587" : L"Traditional Chinese", L"zh-Hant");
    AddLanguage(sourceLanguages_, L"\u65e5\u672c\u8a9e", L"ja");
    AddLanguage(sourceLanguages_, L"\ud55c\uad6d\uc5b4", L"ko");
    AddLanguage(targetLanguages_, S::IsChinese()
        ? L"\u81ea\u52a8\uff08\u4e2d\u82f1\u4e92\u8bd1\uff09" : L"Auto (CN \u2194 EN)", L"auto");
    AddLanguage(targetLanguages_, S::IsChinese() ? L"\u7b80\u4f53\u4e2d\u6587" : L"Simplified Chinese", L"zh-Hans");
    AddLanguage(targetLanguages_, L"English", L"en");
    AddLanguage(targetLanguages_, S::IsChinese() ? L"\u7e41\u9ad4\u4e2d\u6587" : L"Traditional Chinese", L"zh-Hant");
    AddLanguage(targetLanguages_, L"\u65e5\u672c\u8a9e", L"ja");
    AddLanguage(targetLanguages_, L"\ud55c\uad6d\uc5b4", L"ko");

    if (sourceMode_ == TranslationSourceMode::OcrImage) {
        // Mirrors Settings > OCR "Mode" (same wording, same order). The local
        // PaddleOCR entry is the PaddleOCR-VL 1.6 document-parsing route; there
        // is no separate image entry, so the window cannot silently drop the
        // user into plain image OCR.
        AddOcrRoute(S::IsChinese() ? L"\u5f53\u524d\u8bbe\u7f6e" :
            L"Current settings", L"current");
        AddOcrRoute(S::IsChinese() ? L"\u672c\u5730\uff08Windows OCR\uff09" :
            L"Local (Windows OCR)", L"local");
        AddOcrRoute(S::IsChinese() ? L"PaddleOCR \u4e91\u7aef" :
            L"PaddleOCR Cloud", L"paddle_cloud");
        AddOcrRoute(S::IsChinese() ? L"PaddleOCR-VL 1.6 \u672c\u5730" :
            L"PaddleOCR-VL 1.6 Local", L"paddle_local_doc");
        AddOcrRoute(S::IsChinese() ? L"PP-OCRv6 \u672c\u5730" :
            L"PP-OCRv6 Local", L"ppocrv6_onnx");
    }

    const TranslationSettings providerSettings = LoadTranslationSettings();
    for (const auto& profile : providerSettings.providerProfiles) {
        if (!profile.enabled) continue;
        AddProviderOption(profile.displayName.c_str(), profile.id.c_str());
    }

    const auto select = [](const std::vector<LanguageOption>& languages,
                           const std::wstring& value) {
        for (size_t index = 0; index < languages.size(); ++index) {
            if (languages[index].value == value) return static_cast<int>(index);
        }
        return languages.empty() ? -1 : 0;
    };
    sourceLanguageIndex_ = select(sourceLanguages_, request.sourceLanguage);
    targetLanguageIndex_ = select(targetLanguages_, request.targetLanguage);
    if (sourceLanguageIndex_ >= 0) {
        SetWindowTextW(sourceCombo_, sourceLanguages_[sourceLanguageIndex_].label.c_str());
    }
    if (targetLanguageIndex_ >= 0) {
        SetWindowTextW(targetCombo_, targetLanguages_[targetLanguageIndex_].label.c_str());
    }
    ocrRouteIndex_ = 0;
    providerIndex_ = select(providerOptions_, providerSettings.activeProviderId);
    if (providerIndex_ >= 0) {
        SetWindowTextW(providerCombo_,
            ProviderButtonLabel(providerOptions_[providerIndex_].label).c_str());
    }
    SetSourceText(L"");
    SetTranslationText(L"");
    LayoutControls();
}

void TranslationResultWindow::AddLanguage(std::vector<LanguageOption>& languages,
                                          const wchar_t* label, const wchar_t* value) {
    languages.push_back({ label, value });
}

void TranslationResultWindow::ShowLanguageMenu(HWND control, bool sourceLanguage) {
    const auto& languages = sourceLanguage ? sourceLanguages_ : targetLanguages_;
    if (!control || languages.empty()) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    ConfigureCompactPopupMenu(menu);
    const int base = sourceLanguage ? kSourceLanguageMenuBase : kTargetLanguageMenuBase;
    for (size_t index = 0; index < languages.size(); ++index) {
        AppendCompactPopupItem(menu, base + static_cast<int>(index),
            languages[index].label);
    }
    RECT rect = {};
    GetWindowRect(control, &rect);
    popupMenuAnchorWidth_ = static_cast<int>(rect.right - rect.left);
    popupMenuAnchorEngineLabel_ = false;
    const int result = TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        rect.left, rect.bottom, window_, nullptr);
    popupMenuAnchorWidth_ = 0;
    popupMenuAnchorEngineLabel_ = false;
    DestroyMenu(menu);
    const int index = result - base;
    if (result < base || index < 0 || index >= static_cast<int>(languages.size())) return;
    if (sourceLanguage) {
        sourceLanguageIndex_ = index;
    } else {
        targetLanguageIndex_ = index;
    }
    SetWindowTextW(control, languages[index].label.c_str());
    InvalidateRect(control, nullptr, TRUE);
    MarkDirty();
}

void TranslationResultWindow::AddOcrRoute(const wchar_t* label, const wchar_t* value) {
    ocrRoutes_.push_back({ label, value });
}

void TranslationResultWindow::ShowOcrRouteMenu() {
    if (sourceMode_ != TranslationSourceMode::OcrImage ||
        !engineLabel_ || ocrRoutes_.empty() || busy_) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    ConfigureCompactPopupMenu(menu);
    for (size_t index = 0; index < ocrRoutes_.size(); ++index) {
        AppendCompactPopupItem(menu, kOcrRouteMenuBase + static_cast<int>(index),
            ocrRoutes_[index].label);
    }
    RECT rect = {};
    GetWindowRect(engineLabel_, &rect);
    popupMenuAnchorWidth_ = static_cast<int>(rect.right - rect.left);
    popupMenuAnchorEngineLabel_ = true;
    const int result = TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        rect.left, rect.bottom, window_, nullptr);
    popupMenuAnchorWidth_ = 0;
    popupMenuAnchorEngineLabel_ = false;
    DestroyMenu(menu);
    const int index = result - kOcrRouteMenuBase;
    if (result < kOcrRouteMenuBase || index < 0 ||
        index >= static_cast<int>(ocrRoutes_.size())) return;
    if (index == ocrRouteIndex_) return;
    ocrRouteIndex_ = index;
    const std::wstring label = OcrRouteButtonLabel(ocrRoutes_[ocrRouteIndex_].value);
    SetWindowTextW(engineLabel_, label.c_str());
    InvalidateRect(engineLabel_, nullptr, TRUE);
    SetStage(S::IsChinese()
        ? L"OCR 已更改，请点击重新识别"
        : L"OCR changed; click Recognize again");
    InvokeCommandSafely(Command::OcrRouteChanged);
}

void TranslationResultWindow::AddProviderOption(const wchar_t* label, const wchar_t* value) {
    providerOptions_.push_back({ label, value });
}

void TranslationResultWindow::ShowProviderMenu() {
    if (!providerCombo_ || providerOptions_.empty() || busy_) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    ConfigureCompactPopupMenu(menu);
    for (size_t index = 0; index < providerOptions_.size(); ++index) {
        AppendCompactPopupItem(menu, kProviderMenuBase + static_cast<int>(index),
            providerOptions_[index].label);
    }
    RECT rect = {};
    GetWindowRect(providerCombo_, &rect);
    popupMenuAnchorWidth_ = static_cast<int>(rect.right - rect.left);
    popupMenuAnchorEngineLabel_ = false;
    const int result = TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        rect.left, rect.bottom, window_, nullptr);
    popupMenuAnchorWidth_ = 0;
    popupMenuAnchorEngineLabel_ = false;
    DestroyMenu(menu);
    const int index = result - kProviderMenuBase;
    if (result < kProviderMenuBase || index < 0 ||
        index >= static_cast<int>(providerOptions_.size())) return;
    if (index == providerIndex_) return;
    providerIndex_ = index;
    SetWindowTextW(providerCombo_,
        ProviderButtonLabel(providerOptions_[providerIndex_].label).c_str());
    InvalidateRect(providerCombo_, nullptr, TRUE);
    SetStage(S::IsChinese()
        ? L"Provider 已更改，点击重新翻译以应用"
        : L"Provider changed; translate again to apply");
    InvokeCommandSafely(Command::ProviderChanged);
}

void TranslationResultWindow::SetProviderSelection(const std::wstring& providerId) {
    for (size_t index = 0; index < providerOptions_.size(); ++index) {
        if (providerOptions_[index].value != providerId) continue;
        providerIndex_ = static_cast<int>(index);
        if (providerCombo_) {
            SetWindowTextW(providerCombo_,
                ProviderButtonLabel(providerOptions_[index].label).c_str());
            InvalidateRect(providerCombo_, nullptr, TRUE);
        }
        return;
    }
}

std::wstring TranslationResultWindow::SelectedProvider() const {
    if (providerIndex_ < 0 ||
        providerIndex_ >= static_cast<int>(providerOptions_.size())) {
        return L"";
    }
    return providerOptions_[providerIndex_].value;
}

void TranslationResultWindow::Show(
    HWND owner, const POINT* retainedPosition) {
    if (!window_) return;
    if (owner && IsWindow(owner)) SetWindowLongPtrW(window_, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(owner));
    autoPositionNearSource_ = retainedPosition == nullptr;
    if (retainedPosition) {
        SetWindowPos(window_, nullptr,
            retainedPosition->x, retainedPosition->y, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        PositionNearSourceRect();
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
}

void TranslationResultWindow::Activate() {
    if (!window_ || !IsWindow(window_)) return;
    if (IsIconic(window_)) ShowWindow(window_, SW_RESTORE);
    else ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    if (manualEntryMode_) {
        if (sourcePreview_ && sourcePreview_->HasActiveEditor()) {
            sourcePreview_->StartDocumentEditing(manualEntrySelectAll_);
        } else if (sourceDisplayMode_ == SourceDisplayMode::Source && sourceEdit_) {
            SetFocus(sourceEdit_);
        }
    }
}

void TranslationResultWindow::PrepareForReuse(const RECT& sourceRect) {
    StopAutomaticResizeAnimation(false);
    CancelPendingStructuredSelection(L"superseded");
    sourceRect_ = sourceRect;
}

void TranslationResultWindow::PositionNearSourceRect() {
    if (!window_ || sourceRect_.right <= sourceRect_.left ||
        sourceRect_.bottom <= sourceRect_.top) {
        return;
    }
    const HMONITOR monitor = MonitorFromRect(&sourceRect_, MONITOR_DEFAULTTONEAREST);
    FitToMonitorWorkArea(monitor, MonitorDpi(monitor));
    RECT windowRect = {};
    if (!GetWindowRect(window_, &windowRect)) return;
    const POINT position = CalculateWindowPositionNearSource(
        sourceRect_, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top);
    SetWindowPos(window_, nullptr, position.x, position.y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void TranslationResultWindow::FitToMonitorWorkArea(HMONITOR monitor, UINT targetDpi) {
    if (!window_ || !IsWindow(window_) || !monitor) return;
    MONITORINFO info = { sizeof(info) };
    if (!GetMonitorInfoW(monitor, &info)) return;
    RECT windowRect = {};
    if (!GetWindowRect(window_, &windowRect)) return;

    const UINT dpi = targetDpi ? targetDpi : LayoutDpi();
    const int gap = ScaleForDpi(10, dpi);
    const int minimumWidth = MinimumWindowWidth(dpi, monitor);
    const int minimumHeight = ScaleForDpi(420, dpi);
    const int workWidth = static_cast<int>(info.rcWork.right - info.rcWork.left);
    const int workHeight = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
    const int availableWidth = (std::max)(0, workWidth - gap * 2);
    const int availableHeight = (std::max)(0, workHeight - gap * 2);
    const int currentWidth = windowRect.right - windowRect.left;
    const int currentHeight = windowRect.bottom - windowRect.top;
    const int width = availableWidth >= minimumWidth
        ? (std::min)(currentWidth, availableWidth)
        : currentWidth;
    const int height = availableHeight >= minimumHeight
        ? (std::min)(currentHeight, availableHeight)
        : currentHeight;
    if (width == currentWidth && height == currentHeight) return;
    SetWindowPos(window_, nullptr, 0, 0, width, height,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void TranslationResultWindow::ClampToCurrentMonitorWorkArea() {
    if (!window_ || !IsWindow(window_)) return;
    const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    const UINT dpi = LayoutDpi();
    FitToMonitorWorkArea(monitor, dpi);
    RECT windowRect = {};
    if (!GetWindowRect(window_, &windowRect)) return;
    MONITORINFO info = { sizeof(info) };
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const int gap = ScaleForDpi(10, dpi);
    const int x = ClampWindowCoordinate(windowRect.left, width,
        info.rcWork.left, info.rcWork.right, gap);
    const int y = ClampWindowCoordinate(windowRect.top, height,
        info.rcWork.top, info.rcWork.bottom, gap);
    if (x == windowRect.left && y == windowRect.top) return;
    SetWindowPos(window_, nullptr, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

SIZE TranslationResultWindow::CalculateAutomaticWindowSize() const {
    const UINT dpi = LayoutDpi();
    HMONITOR monitor = MonitorFromRect(&sourceRect_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return CalculateInitialTranslationWindowSize(
            sourceRect_, dpi, MinimumWindowWidth(dpi));
    }

    const int minWidth = MinimumWindowWidth(dpi, monitor);
    const int minHeight = ScaleForDpi(420, dpi);
    const int maxWidth = (std::max)(minWidth,
        (std::min)(static_cast<int>(monitorInfo.rcWork.right - monitorInfo.rcWork.left) -
                       ScaleForDpi(40, dpi),
                   ScaleForDpi(1100, dpi)));
    const int maxHeight = (std::max)(minHeight,
        static_cast<int>((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) * 3 / 4));
    const int margin = ScaleForDpi(4, dpi);
    const int cardPadding = ScaleForDpi(8, dpi);
    const int textInset = ScaleForDpi(kTranslationTextEditMargin, dpi);
    const int cardFooterHeight = ScaleForDpi(kTranslationCardFooterHeight, dpi);
    const int cardFooterGap = ScaleForDpi(kTranslationCardFooterGap, dpi);
    const int controlRowHeight = ScaleForDpi(24, dpi);
    const int controlRowGap = ScaleForDpi(kTranslationControlRowGap, dpi);
    const bool selectorsInHeader = SelectorsInCompactHeader();
    const int headerHeight = ScaleForDpi(showWindowBorder_ ? 58 : 30, dpi);
    const int chromeHeight = headerHeight + ScaleForDpi(3, dpi) + margin +
        (selectorsInHeader
            ? (showSourceText_ ? controlRowGap : 0)
            : controlRowHeight + controlRowGap * 2);
    const int minimumBodyHeight = (std::max)(0, minHeight - chromeHeight);
    const int maximumBodyHeight = (std::max)(minimumBodyHeight, maxHeight - chromeHeight);
    const int cropWidth = (std::max)(0,
        static_cast<int>(sourceRect_.right - sourceRect_.left));
    const int cropHeight = (std::max)(0,
        static_cast<int>(sourceRect_.bottom - sourceRect_.top));
    const int cropWidthHint = (std::min)(MulDiv(cropWidth, 8, 10),
        ScaleForDpi(720, dpi));
    const int cropHeightHint = (std::min)(MulDiv(cropHeight, 8, 10),
        ScaleForDpi(520, dpi));
    const int maxMeasuredTextWidth = (std::max)(ScaleForDpi(48, dpi),
        maxWidth - ScaleForDpi(56, dpi));

    HDC measureDc = GetDC(window_);
    const int sourceTextWidth = MeasureUsefulTextWidth(
        measureDc, sourceTextFont_, SourceText(), maxMeasuredTextWidth);
    const int translationTextWidth = MeasureUsefulTextWidth(
        measureDc, textFont_, translationMarkdownText_, maxMeasuredTextWidth);
    const int preferredTextWidth = (std::max)(sourceTextWidth, translationTextWidth);
    if (measureDc) ReleaseDC(window_, measureDc);

    const int preferredWidth = (std::max)({ minWidth, cropWidthHint,
        preferredTextWidth + ScaleForDpi(56, dpi) });
    const int width = (std::clamp)(preferredWidth, minWidth, maxWidth);
    const int measureWidth = (std::max)(ScaleForDpi(48, dpi),
        width - margin * 2 - cardPadding * 2 - textInset * 2);
    HDC heightDc = GetDC(window_);
    const int sourceTextHeight = MeasureWrappedTextHeight(
        heightDc, sourceTextFont_, SourceText(), measureWidth);
    const int translationTextHeight = MeasureWrappedTextHeight(
        heightDc, textFont_, translationMarkdownText_, measureWidth);
    if (heightDc) ReleaseDC(window_, heightDc);

    const int previewAllowance = ScaleForDpi(16, dpi);
    const int previewMetricSafety = ScaleForDpi(kTranslationPreviewMetricSafety, dpi);
    const int sourcePreviewHeight = sourcePreviewMetricsValid_ &&
        sourceDisplayMode_ == SourceDisplayMode::Preview
        ? sourcePreviewContentHeight_ + previewMetricSafety : 0;
    const int translationPreviewHeight = translationPreviewMetricsValid_
        ? translationPreviewContentHeight_ + previewMetricSafety : 0;
    const int sourceContentHeight = (std::max)(sourceTextHeight + previewAllowance,
        sourcePreviewHeight);
    const int translationContentHeight = (std::max)(translationTextHeight + previewAllowance,
        translationPreviewHeight);
    const int sourceDesiredHeight = cardPadding + sourceContentHeight + cardFooterGap +
        cardFooterHeight;
    const int translationDesiredHeight = cardPadding + translationContentHeight +
        cardFooterGap + cardFooterHeight;
    const int minimumSourceHeight = ScaleForDpi(kTranslationSourceMinHeight, dpi);
    const int minimumTranslationHeight = ScaleForDpi(
        kTranslationTranslationMinHeight, dpi);
    int desiredBodyHeight = (std::max)(minimumBodyHeight, cropHeightHint);
    if (showSourceText_) {
        desiredBodyHeight = (std::max)(desiredBodyHeight,
            sourceDesiredHeight + minimumTranslationHeight);
    } else {
        desiredBodyHeight = (std::max)(desiredBodyHeight, translationDesiredHeight);
    }
    if (!translationMarkdownText_.empty() && showSourceText_) {
        desiredBodyHeight = (std::max)(desiredBodyHeight,
            (std::max)(minimumSourceHeight, sourceDesiredHeight) +
                translationDesiredHeight);
    }
    const int bodyHeight = (std::clamp)(desiredBodyHeight,
        minimumBodyHeight, maximumBodyHeight);
    return { width, bodyHeight + chromeHeight };
}

void TranslationResultWindow::ResizeToAutomaticWindowSize() {
    if (!window_ || !IsWindow(window_)) return;
    if (windowSizeManuallyAdjusted_) {
        LayoutControls();
        return;
    }
    const SIZE desired = CalculateAutomaticWindowSize();
    RECT current = {};
    if (!GetWindowRect(window_, &current)) return;
    if (current.right - current.left == desired.cx &&
        current.bottom - current.top == desired.cy) {
        LayoutControls();
        return;
    }
    if (!IsWindowVisible(window_) || IsIconic(window_) || IsZoomed(window_) ||
        windowSizeMoveActive_) {
        SetWindowPos(window_, nullptr, 0, 0, desired.cx, desired.cy,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (autoPositionNearSource_) PositionNearSourceRect();
        return;
    }
    BeginAutomaticResizeAnimation(desired);
}

void TranslationResultWindow::BeginAutomaticResizeAnimation(const SIZE& desired) {
    if (!window_ || !GetWindowRect(window_, &resizeAnimationStartRect_)) return;

    POINT targetPosition = {
        resizeAnimationStartRect_.left,
        resizeAnimationStartRect_.top,
    };
    if (autoPositionNearSource_ && sourceRect_.right > sourceRect_.left &&
        sourceRect_.bottom > sourceRect_.top) {
        targetPosition = CalculateWindowPositionNearSource(
            sourceRect_, desired.cx, desired.cy);
    }
    resizeAnimationTargetRect_ = {
        targetPosition.x,
        targetPosition.y,
        targetPosition.x + desired.cx,
        targetPosition.y + desired.cy,
    };
    resizeAnimationStartedTick_ = GetTickCount64();
    resizeAnimationActive_ = true;
    if (!SetTimer(window_, kResizeAnimationTimer,
            kTranslationResizeAnimationFrameMs, nullptr)) {
        resizeAnimationActive_ = false;
        SetWindowPos(window_, nullptr,
            resizeAnimationTargetRect_.left, resizeAnimationTargetRect_.top,
            desired.cx, desired.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void TranslationResultWindow::UpdateAutomaticResizeAnimation() {
    if (!resizeAnimationActive_ || !window_ || !IsWindow(window_)) return;

    const ULONGLONG elapsed = GetTickCount64() - resizeAnimationStartedTick_;
    const double progress = (std::min)(1.0,
        static_cast<double>(elapsed) /
            static_cast<double>(kTranslationResizeAnimationDurationMs));
    const double remaining = 1.0 - progress;
    const double eased = 1.0 - remaining * remaining * remaining;
    const auto interpolate = [eased](LONG start, LONG target) {
        return static_cast<LONG>(std::lround(
            static_cast<double>(start) +
            static_cast<double>(target - start) * eased));
    };
    RECT frame = {
        interpolate(resizeAnimationStartRect_.left, resizeAnimationTargetRect_.left),
        interpolate(resizeAnimationStartRect_.top, resizeAnimationTargetRect_.top),
        interpolate(resizeAnimationStartRect_.right, resizeAnimationTargetRect_.right),
        interpolate(resizeAnimationStartRect_.bottom, resizeAnimationTargetRect_.bottom),
    };
    SetWindowPos(window_, nullptr, frame.left, frame.top,
        frame.right - frame.left, frame.bottom - frame.top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);

    if (progress >= 1.0) {
        KillTimer(window_, kResizeAnimationTimer);
        resizeAnimationActive_ = false;
        LayoutControls();
    }
}

void TranslationResultWindow::StopAutomaticResizeAnimation(bool finish) {
    if (!resizeAnimationActive_) return;
    KillTimer(window_, kResizeAnimationTimer);
    if (finish && window_ && IsWindow(window_)) {
        SetWindowPos(window_, nullptr,
            resizeAnimationTargetRect_.left, resizeAnimationTargetRect_.top,
            resizeAnimationTargetRect_.right - resizeAnimationTargetRect_.left,
            resizeAnimationTargetRect_.bottom - resizeAnimationTargetRect_.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
    resizeAnimationActive_ = false;
    if (finish) LayoutControls();
}

void TranslationResultWindow::SetStage(const std::wstring& stage) {
    if (!stageLabel_) return;
    // Remember the pure stage text: the translation elapsed timer derives its
    // rolling label from this so seconds never accumulate on themselves.
    lastStageText_ = stage;
    SetWindowTextW(stageLabel_, stage.c_str());
    const bool showStage = !IsIdleStageText(stage);
    SetControlVisible(stageLabel_, showStage);
    if (showStage) {
        SetControlVisible(translationElapsedLabel_, false);
    } else if (showTranslationElapsed_ && window_ && IsWindow(window_)) {
        // Let the existing layout pass place the elapsed label before showing
        // it. In the bordered variant it may not have received geometry yet.
        LayoutControls();
    }
}

void TranslationResultWindow::BeginOcrElapsed() {
    if (sourceMode_ != TranslationSourceMode::OcrImage ||
        !window_ || !IsWindow(window_)) return;
    showOcrElapsed_ = true;
    ocrStartedTick_ = GetTickCount64();
    SetTimer(window_, kOcrElapsedTimer, 250, nullptr);
    UpdateOcrElapsedStage();
}

void TranslationResultWindow::EndOcrElapsed() {
    if (sourceMode_ != TranslationSourceMode::OcrImage) return;
    showOcrElapsed_ = false;
    ocrStartedTick_ = 0;
    if (window_ && IsWindow(window_)) KillTimer(window_, kOcrElapsedTimer);
}

void TranslationResultWindow::UpdateOcrElapsedStage() {
    if (!showOcrElapsed_ || !stageLabel_) return;
    const double elapsed = static_cast<double>(GetTickCount64() - ocrStartedTick_) / 1000.0;
    // WideFormatSeconds1 already carries the "s" unit, so appending another one
    // rendered "3.2ss". The spacing also matches the translation-phase label.
    const std::wstring prefix = S::IsChinese()
        ? L"\u6b63\u5728\u8bc6\u522b\u6587\u5b57\u2026" : L"Recognizing text...";
    SetWindowTextW(stageLabel_,
        (prefix + L" " + WideFormatSeconds1(elapsed)).c_str());
}

void TranslationResultWindow::BeginTranslationElapsed() {
    if (!window_ || !IsWindow(window_)) return;
    translationElapsedRunning_ = true;
    translationElapsedTick_ = GetTickCount64();
    SetTimer(window_, kTranslationElapsedTimer, 250, nullptr);
    UpdateTranslationElapsedStage();
}

void TranslationResultWindow::EndTranslationElapsed() {
    // Idempotent: every finish path may call this, and KillTimer is harmless
    // when no timer is pending.
    translationElapsedRunning_ = false;
    translationElapsedTick_ = 0;
    if (window_ && IsWindow(window_)) {
        KillTimer(window_, kTranslationElapsedTimer);
    }
}

void TranslationResultWindow::UpdateTranslationElapsedStage() {
    if (!translationElapsedRunning_ || !stageLabel_) return;
    // Never decorate a terminal stage. KillTimer does not remove WM_TIMER
    // messages that are already posted, so a tick can still be dispatched after
    // the counter was stopped and would otherwise turn "Ready" into
    // "Ready 0.3s".
    if (IsIdleStageText(lastStageText_)) return;
    // Appends the running seconds to the current stage text, so a retry can
    // change the wording ("请求超时，正在重试…") while the counter keeps
    // counting up instead of restarting. WideFormatSeconds1 already carries the
    // "s" unit.
    const double elapsed =
        static_cast<double>(GetTickCount64() - translationElapsedTick_) / 1000.0;
    SetWindowTextW(stageLabel_,
        (lastStageText_ + L" " + WideFormatSeconds1(elapsed)).c_str());
}

void TranslationResultWindow::SetOcrEngineLabel(const std::wstring& label) {
    if (sourceMode_ != TranslationSourceMode::OcrImage) return;
    // A pending route change temporarily shows the selected route here;
    // recognition restores the actual engine label supplied by the coordinator.
    if (!engineLabel_) return;
    wchar_t current[128] = {};
    GetWindowTextW(engineLabel_, current, static_cast<int>(std::size(current)));
    if (std::wstring(current) == label) return;
    SetWindowTextW(engineLabel_, label.c_str());
    // The route combo is measured from this text, so a longer badge has to widen
    // the shared combo width now rather than at the next unrelated layout pass.
    LayoutControls();
}

void TranslationResultWindow::SetOcrRouteSelection(const std::wstring& route) {
    if (sourceMode_ != TranslationSourceMode::OcrImage) return;
    for (size_t index = 0; index < ocrRoutes_.size(); ++index) {
        if (ocrRoutes_[index].value == route) {
            ocrRouteIndex_ = static_cast<int>(index);
            return;
        }
    }
    ocrRouteIndex_ = 0;
}

void TranslationResultWindow::SetSourceText(const std::wstring& text) {
    EndTextEntry();
    const std::wstring displayText = NormalizeCardTextForWrap(text);
    if (displayText == sourceMarkdownText_ && !sourcePreviewFailed_) {
        ResizeToAutomaticWindowSize();
        return;
    }
    const bool waitForPreview = sourcePreview_ &&
        (sourcePreview_->IsReady() || sourcePreview_->IsCreating());
    sourceMarkdownText_ = displayText;
    sourceDisplayMode_ = waitForPreview
        ? SourceDisplayMode::Preview : SourceDisplayMode::Source;
    sourcePreviewFailed_ = !waitForPreview;
    sourcePreviewMetricsValid_ = false;
    sourcePreviewRenderReady_ = false;
    sourcePreviewContentHeight_ = 0;
    suppressCommands_ = true;
    SetControlText(sourceEdit_, displayText);
    suppressCommands_ = false;
    if (waitForPreview) {
        sourcePreview_->RenderMarkdown(-1, displayText, true);
        UpdateSourcePreviewVisibility();
    }
    SetControlText(sourceCountLabel_, CharacterCountText(displayText));
    if (displayText.empty()) {
        UpdateSourcePreviewVisibility();
    } else if (!waitForPreview) {
        ResizeToAutomaticWindowSize();
    }
}

void TranslationResultWindow::SetTranslationText(const std::wstring& text) {
    const std::wstring displayText = NormalizeCardTextForWrap(text);
    if (displayText == translationMarkdownText_ && !translationPreviewFailed_) {
        ResizeToAutomaticWindowSize();
        return;
    }
    const bool waitForPreview = translationPreview_ &&
        (translationPreview_->IsReady() || translationPreview_->IsCreating());
    translationMarkdownText_ = displayText;
    // A previous render can fail while the WebView remains alive. A new
    // translation is a fresh render attempt; keep the native edit as a
    // fallback only if this attempt reports another error.
    translationPreviewFailed_ = !waitForPreview;
    translationPreviewMetricsValid_ = false;
    translationPreviewRenderReady_ = false;
    translationPreviewContentHeight_ = 0;
    SetControlText(translationEdit_, displayText);
    if (waitForPreview) {
        if (busy_ && translationPreview_->IsReady()) {
            translationPreview_->RenderTransientMarkdown(-1, displayText, true);
        } else {
            translationPreview_->RenderMarkdown(-1, displayText, true);
        }
        UpdateTranslationPreviewVisibility();
    }
    SetControlText(translationCountLabel_, CharacterCountText(displayText));
    if (displayText.empty()) {
        UpdateTranslationPreviewVisibility();
    } else if (!waitForPreview) {
        ResizeToAutomaticWindowSize();
    }
}

void TranslationResultWindow::SetTranslationElapsed(DWORD elapsedMs) {
    showTranslationElapsed_ = true;
    if (translationElapsedLabel_) {
        const std::wstring text = WideFormatSeconds1(elapsedMs / 1000.0);
        SetWindowTextW(translationElapsedLabel_, text.c_str());
    }
}

void TranslationResultWindow::ClearTranslationElapsed() {
    showTranslationElapsed_ = false;
    SetControlText(translationElapsedLabel_, L"");
    SetControlVisible(translationElapsedLabel_, false);
}

void TranslationResultWindow::SetBusy(bool busy) {
    if (!busy) {
        // Stop both counters before the state-equality early return. A busy flag
        // that is already clear must not leave a timer running: it would keep
        // repainting the stage label of an already-finished workflow. The
        // translation counter also has to stop before any terminal stage is
        // shown, so "Ready"/error text is never overwritten by a later tick.
        EndOcrElapsed();
        EndTranslationElapsed();
    }
    if (busy_ == busy) return;
    busy_ = busy;
    if (!busy) {
        if (translationPreview_ && translationPreview_->IsReady() &&
            !translationPreviewFailed_ && !translationMarkdownText_.empty()) {
            translationPreviewMetricsValid_ = false;
            translationPreviewRenderReady_ = false;
            translationPreviewContentHeight_ = 0;
            translationPreview_->RenderMarkdown(-1, translationMarkdownText_, true);
        }
    }
    EnableWindow(engineLabel_, !busy);
    EnableWindow(recognizeButton_, !busy);
    EnableWindow(sourceCombo_, !busy);
    EnableWindow(targetCombo_, !busy);
    EnableWindow(sourceEdit_, !busy);
    if (showSourceToggle_) EnableWindow(showSourceToggle_, !busy && !manualEntryMode_);
    UpdateSourcePreviewVisibility();
    UpdateTranslationPreviewVisibility();
    UpdateActionAvailability();
    SetControlVisible(retranslateButton_, !busy_ && !retryOcrMode_);
    SetControlVisible(cancelButton_, busy_);
}

void TranslationResultWindow::SetSourceDisplayMode(
    SourceDisplayMode mode, bool focusEdit) {
    if (mode == SourceDisplayMode::Preview &&
        (!sourcePreview_ || sourcePreviewFailed_)) {
        mode = SourceDisplayMode::Source;
    }
    if (mode == SourceDisplayMode::Source &&
        sourceDisplayMode_ == SourceDisplayMode::Preview &&
        !resolvingDocumentEditorSwitch_ &&
        !ResolveDocumentEditorBeforeSourceMode()) {
        return;
    }
    sourceDisplayMode_ = mode;
    if (mode == SourceDisplayMode::Preview) {
        sourceMarkdownText_ = SourceText();
        sourcePreviewRenderReady_ = false;
        if (sourcePreview_) {
            sourcePreview_->RenderMarkdown(-1, sourceMarkdownText_, true);
        }
    }
    UpdateSourceModeButton();
    UpdateSourcePreviewVisibility();
    if (focusEdit && sourceEdit_ && showSourceText_) {
        ShowWindow(sourceEdit_, SW_SHOW);
        SetFocus(sourceEdit_);
    }
}

void TranslationResultWindow::UpdateSourceModeButton() {
    if (!sourceModeButton_) return;
    const bool textChanged = SetControlText(sourceModeButton_,
        sourceDisplayMode_ == SourceDisplayMode::Preview
            ? (S::IsChinese() ? L"\u539f\u6587" : L"Source")
            : (S::IsChinese() ? L"\u9884\u89c8" : L"Preview"));
    const bool available = !busy_ && showSourceText_ && sourcePreview_ &&
        !sourcePreviewFailed_ && !sourcePreview_->IsEditorActionPending();
    const bool enabledChanged = (IsWindowEnabled(sourceModeButton_) != FALSE) != available;
    if (enabledChanged) EnableWindow(sourceModeButton_, available);
    if (textChanged || enabledChanged) InvalidateRect(sourceModeButton_, nullptr, TRUE);
}

bool TranslationResultWindow::ResolveDocumentEditorBeforeSourceMode() {
    if (!sourcePreview_ || !sourcePreview_->HasActiveEditor()) return true;
    if (sourcePreview_->IsEditorActionPending()) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    if (sourcePreview_->IsEditorComposing()) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    if (!sourcePreview_->HasDirtyEditor()) {
        sourcePreview_->CancelActiveEditor();
        return true;
    }

    const int choice = MessageBoxW(
        window_,
        S::IsChinese()
            ? L"Markdown 编辑尚未保存。\n\n是：保存并切换\n否：放弃修改并切换\n取消：继续编辑"
            : L"The Markdown edit has not been saved.\n\nYes: save and switch\nNo: discard and switch\nCancel: keep editing",
        S::IsChinese() ? L"ZenCrop 翻译" : L"ZenCrop Translate",
        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return false;
    if (choice == IDNO) {
        sourcePreview_->CancelActiveEditor();
        return true;
    }
    switchToSourceAfterDocumentSave_ = true;
    sourcePreview_->RequestActiveEditorSave();
    return false;
}

void TranslationResultWindow::UpdateSourcePreviewVisibility() {
    const bool previewReady = !busy_ && showSourceText_ &&
        sourceDisplayMode_ == SourceDisplayMode::Preview &&
        sourcePreview_ && sourcePreview_->IsReady() &&
        sourcePreviewRenderReady_ && !sourcePreviewFailed_ &&
        (!sourceMarkdownText_.empty() || manualEntryMode_);
    SetControlVisible(sourceEdit_, !previewReady && showSourceText_);
    if (sourcePreview_) {
        sourcePreview_->Show(previewReady);
        sourcePreview_->SetBounds(sourceContentRect_);
    }
    UpdateSourceModeButton();
    UpdateSourceEditorFooterActions();
}

void TranslationResultWindow::UpdateSourceEditorFooterActions() {
    const bool editing = showSourceText_ && !busy_ &&
        sourceDisplayMode_ == SourceDisplayMode::Preview && sourcePreview_ &&
        sourcePreview_->IsReady() && sourcePreview_->HasActiveEditor();
    const bool pending = editing && sourcePreview_->IsEditorActionPending();
    const bool validManualText = sourcePreviewEditorHasText_ &&
        sourcePreviewEditorContentUnits_ <= selection::kMaxSelectionTextUnits;
    const bool canSave = editing && !pending &&
        !sourcePreview_->IsEditorComposing() && sourcePreview_->CanSaveActiveEditor() &&
        (manualEntryMode_ ? validManualText : sourcePreview_->HasDirtyEditor());
    SetControlVisible(sourceEditorCancelButton_, editing);
    SetControlVisible(sourceEditorSaveButton_, editing);
    if (sourceEditorCancelButton_) EnableWindow(sourceEditorCancelButton_, editing && !pending);
    if (sourceEditorSaveButton_) EnableWindow(sourceEditorSaveButton_, canSave);
    if (copySourceButton_) EnableWindow(copySourceButton_, !editing);
}

void TranslationResultWindow::CancelSourceDocumentEditor() {
    if (!sourcePreview_ || !sourcePreview_->HasActiveEditor() ||
        sourcePreview_->IsEditorActionPending()) return;
    cancelManualEditorRequested_ = manualEntryMode_;
    sourcePreview_->CancelActiveEditor();
    UpdateSourceEditorFooterActions();
}

void TranslationResultWindow::UpdateTranslationPreviewVisibility() {
    const bool previewReady = translationPreview_ &&
        translationPreview_->IsReady() && !translationPreviewFailed_ &&
        translationPreviewRenderReady_ && !translationMarkdownText_.empty();
    SetControlVisible(translationEdit_, !previewReady);
    if (translationPreview_) {
        translationPreview_->Show(previewReady);
        translationPreview_->SetBounds(translationContentRect_);
    }
}

void TranslationResultWindow::SetAlwaysOnTop(bool alwaysOnTop) {
    if (!window_ || !IsWindow(window_)) return;
    if (alwaysOnTop_ == alwaysOnTop) return;

    // Screenshot translation is a native ZenCrop window, so use the same
    // manager as the existing crop/viewport windows.  Besides HWND_TOPMOST,
    // it owns the optional border and follows move/minimize/destroy events.
    auto& manager = AlwaysOnTopManager::Instance();
    if (alwaysOnTop) {
        manager.PinWindow(window_);
    } else {
        manager.UnpinWindow(window_);
    }
    alwaysOnTop_ = manager.IsPinned(window_);
    UpdatePinAccessibleState();
    if (pinButton_) InvalidateRect(pinButton_, nullptr, TRUE);
}

void TranslationResultWindow::SetShowWindowBorder(bool show) {
    if (showWindowBorder_ == show) return;
    showWindowBorder_ = show;
    ApplyDarkWindowChrome();
    ResizeToAutomaticWindowSize();
    if (window_) InvalidateRect(window_, nullptr, TRUE);
}

void TranslationResultWindow::SetShowSourceText(bool show) {
    if (showSourceText_ == show) return;
    if (!show && sourceSplitterDragging_) {
        sourceSplitterDragging_ = false;
        sourceSplitterHot_ = false;
        if (GetCapture() == window_) ReleaseCapture();
    }
    if (!show && (GetFocus() == sourceEdit_ || GetFocus() == copySourceButton_ ||
                  GetFocus() == sourceEditorCancelButton_ ||
                  GetFocus() == sourceEditorSaveButton_ || GetFocus() == sourceModeButton_)) {
        SetFocus(showSourceToggle_);
    }
    showSourceText_ = show;
    // The hidden preview has no usable layout bounds.  Do not let a height
    // measured for that state drive the first visible layout.
    sourcePreviewMetricsValid_ = false;
    sourcePreviewContentHeight_ = 0;
    SetControlVisible(copySourceButton_, show);
    SetControlVisible(sourceCountLabel_, show);
    UpdateSourceEditorFooterActions();
    ResizeToAutomaticWindowSize();
    if (showSourceToggle_) InvalidateRect(showSourceToggle_, nullptr, TRUE);
}

void TranslationResultWindow::SetRetryOcrMode(bool retryOcr) {
    const bool next = sourceMode_ == TranslationSourceMode::OcrImage && retryOcr;
    if (retryOcrMode_ == next) return;
    retryOcrMode_ = next;
    UpdateActionAvailability();
    SetControlVisible(retranslateButton_, !busy_ && !retryOcrMode_);
    SetControlVisible(cancelButton_, busy_);
}

std::wstring TranslationResultWindow::SourceText() const {
    const int length = GetWindowTextLengthW(sourceEdit_);
    if (length <= 0) return {};
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(sourceEdit_, value.data(), length + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<size_t>(copied));
    return value;
}

std::wstring TranslationResultWindow::SourceLanguage() const {
    if (sourceLanguageIndex_ < 0 || sourceLanguageIndex_ >= static_cast<int>(sourceLanguages_.size())) {
        return L"auto";
    }
    return sourceLanguages_[sourceLanguageIndex_].value;
}

void TranslationResultWindow::SetSourceLanguage(const std::wstring& value) {
    suppressCommands_ = true;
    for (size_t index = 0; index < sourceLanguages_.size(); ++index) {
        if (sourceLanguages_[index].value == value) {
            if (sourceLanguageIndex_ == static_cast<int>(index)) {
                suppressCommands_ = false;
                return;
            }
            sourceLanguageIndex_ = static_cast<int>(index);
            if (SetControlText(sourceCombo_, sourceLanguages_[index].label)) {
                InvalidateRect(sourceCombo_, nullptr, TRUE);
            }
            suppressCommands_ = false;
            return;
        }
    }
    suppressCommands_ = false;
}

void TranslationResultWindow::SetTargetLanguage(const std::wstring& value) {
    suppressCommands_ = true;
    for (size_t index = 0; index < targetLanguages_.size(); ++index) {
        if (targetLanguages_[index].value != value) continue;
        if (targetLanguageIndex_ != static_cast<int>(index)) {
            targetLanguageIndex_ = static_cast<int>(index);
            if (SetControlText(targetCombo_, targetLanguages_[index].label)) {
                InvalidateRect(targetCombo_, nullptr, TRUE);
            }
        }
        break;
    }
    suppressCommands_ = false;
}

std::wstring TranslationResultWindow::TargetLanguage() const {
    if (targetLanguageIndex_ < 0 || targetLanguageIndex_ >= static_cast<int>(targetLanguages_.size())) {
        return L"auto";
    }
    return targetLanguages_[targetLanguageIndex_].value;
}

std::wstring TranslationResultWindow::OcrRoute() const {
    if (ocrRouteIndex_ < 0 || ocrRouteIndex_ >= static_cast<int>(ocrRoutes_.size())) {
        return L"current";
    }
    return ocrRoutes_[ocrRouteIndex_].value;
}

void TranslationResultWindow::CopyControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        CopyTextToClipboard(window_, L"");
        return;
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    if (copied <= 0) return;
    text.resize(static_cast<size_t>(copied));
    if (!CopyTextToClipboard(window_, text)) {
        MessageBoxW(window_, S::IsChinese() ? L"\u590d\u5236\u5931\u8d25\u3002" : L"Copy failed.",
            S::IsChinese() ? L"ZenCrop \u7ffb\u8bd1" : L"ZenCrop Translate", MB_OK | MB_ICONERROR);
    }
}

void TranslationResultWindow::MarkDirty() {
    if (busy_ || suppressCommands_) return;
    SetStage(S::IsChinese()
        ? L"\u5df2\u4fee\u6539\uff0c\u70b9\u51fb\u91cd\u65b0\u7ffb\u8bd1\u4ee5\u66f4\u65b0"
        : L"Edited; translate again to update");
}

void TranslationResultWindow::UpdateActionAvailability() {
    const std::wstring source = SourceText();
    const bool withinLimit = sourceMode_ != TranslationSourceMode::SelectedText ||
        source.size() <= selection::kMaxSelectionTextUnits;
    const bool hasSource = selection::HasNonWhitespace(source) && withinLimit &&
        selection::IsValidSelectionUtf16(source) && !sourcePreviewEditorActive_;
    if (retranslateButton_) {
        EnableWindow(retranslateButton_, !busy_ && !retryOcrMode_ && hasSource);
    }
    if (cancelButton_) {
        EnableWindow(cancelButton_, busy_);
    }
}

void TranslationResultWindow::UpdatePinAccessibleState() {
    pinToolTipText_ = alwaysOnTop_
        ? (S::IsChinese() ? L"\u53d6\u6d88\u7ed3\u679c\u7a97\u53e3\u7f6e\u9876" : L"Unpin result window")
        : (S::IsChinese() ? L"\u7f6e\u9876\u7ed3\u679c\u7a97\u53e3" : L"Pin result window on top");
    if (pinButton_) SetWindowTextW(pinButton_, pinToolTipText_.c_str());
    if (!pinToolTip_ || !pinButton_) return;
    TOOLINFOW tool = { sizeof(tool) };
    tool.uFlags = TTF_IDISHWND;
    tool.hwnd = window_;
    tool.uId = reinterpret_cast<UINT_PTR>(pinButton_);
    tool.lpszText = const_cast<wchar_t*>(pinToolTipText_.c_str());
    SendMessageW(pinToolTip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void TranslationResultWindow::NotifyClose() {
    if (closeNotified_) return;
    closeNotified_ = true;
    InvokeCommandSafely(Command::Close);
}

void TranslationResultWindow::InvokeCommandSafely(Command command) noexcept {
    try {
        // Keep a local copy: owner callbacks are allowed to tear down the
        // coordinator (and therefore this window) while the command is being
        // dispatched. The local callable keeps its own captures alive and no
        // member is touched after the invocation returns.
        CommandCallback callback = callback_;
        if (callback) callback(command);
    } catch (const std::exception&) {
        OutputDebugStringW(L"[Translation] result-window command callback threw a standard exception.\n");
    } catch (...) {
        OutputDebugStringW(L"[Translation] result-window command callback threw an unknown exception.\n");
    }
}

void TranslationResultWindow::FocusRelative(HWND current, bool previous) {
    const std::array<HWND, 16> controls = {
        showSourceToggle_, providerCombo_, sourceCombo_, targetCombo_, sourceEdit_,
        copySourceButton_, sourceEditorCancelButton_, sourceEditorSaveButton_,
        translationEdit_, copyTranslationButton_, retranslateButton_, cancelButton_,
        sourceModeButton_, pinButton_, minimizeButton_, closeButton_,
    };
    std::vector<HWND> focusable;
    focusable.reserve(controls.size());
    for (HWND control : controls) {
        if (!control || !IsWindow(control) || !IsWindowVisible(control) ||
            !IsWindowEnabled(control) ||
            (GetWindowLongPtrW(control, GWL_STYLE) & WS_TABSTOP) == 0) {
            continue;
        }
        focusable.push_back(control);
    }
    if (focusable.empty()) return;
    const auto currentIt = std::find(focusable.begin(), focusable.end(), current);
    if (currentIt == focusable.end()) {
        SetFocus(previous ? focusable.back() : focusable.front());
        return;
    }
    const size_t index = static_cast<size_t>(currentIt - focusable.begin());
    const size_t next = previous
        ? (index + focusable.size() - 1) % focusable.size()
        : (index + 1) % focusable.size();
    SetFocus(focusable[next]);
}

void TranslationResultWindow::HandleEscape() {
    if (busy_) {
        InvokeCommandSafely(Command::Cancel);
        return;
    }
    NotifyClose();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
}

void TranslationResultWindow::HandleChildKey(HWND child, WPARAM key) {
    const WPARAM virtualKey = LOWORD(key);
    if (virtualKey == L'W' && (key & kTranslationChildKeyControl) != 0) {
        SendMessageW(window_, WM_CLOSE, 0, 0);
    } else if (virtualKey == VK_ESCAPE) {
        if (!busy_ && sourcePreview_ && sourcePreview_->HasActiveEditor()) {
            CancelSourceDocumentEditor();
        } else {
            HandleEscape();
        }
    } else if (virtualKey == VK_TAB) {
        FocusRelative(child, (key & kTranslationChildKeyShift) != 0);
    }
}

void TranslationResultWindow::RefreshFontForLayoutDpi() {
    HFONT replacement = DefaultFont(LayoutDpi());
    HFONT replacementCompact = CompactDefaultFont(LayoutDpi());
    HFONT replacementTitle = TitleFont(LayoutDpi());
    HFONT replacementText = TextFont(textFontSize_, LayoutDpi());
    HFONT replacementSourceText = TextFont(sourceEditFontSize_, LayoutDpi());
    if (!replacement || !replacementCompact || !replacementTitle || !replacementText ||
        !replacementSourceText) {
        if (replacement) DeleteObject(replacement);
        if (replacementCompact) DeleteObject(replacementCompact);
        if (replacementTitle) DeleteObject(replacementTitle);
        if (replacementText) DeleteObject(replacementText);
        if (replacementSourceText) DeleteObject(replacementSourceText);
        return;
    }
    HFONT previous = font_;
    HFONT previousCompact = compactFont_;
    HFONT previousTitle = titleFont_;
    HFONT previousText = textFont_;
    HFONT previousSourceText = sourceTextFont_;
    font_ = replacement;
    compactFont_ = replacementCompact;
    titleFont_ = replacementTitle;
    textFont_ = replacementText;
    sourceTextFont_ = replacementSourceText;
    EnumChildWindows(window_, [](HWND child, LPARAM parameter) {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(parameter), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font_));
    // Must stay in sync with the compact-font list in CreateControls(): a control
    // painted with compactFont_ but measured with font_ would be laid out too
    // narrow (the provider combo and the source-mode button were the ones missing
    // here).
    for (HWND control : {stageLabel_, engineLabel_, targetLabel_,
                         showSourceToggle_, sourceCombo_, targetCombo_,
                         providerCombo_, sourceModeButton_}) {
        if (control) {
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(compactFont_), TRUE);
        }
    }
    if (sourceEdit_) SendMessageW(sourceEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(sourceTextFont_), TRUE);
    if (translationEdit_) SendMessageW(translationEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(textFont_), TRUE);
    const int editMargin = ScaleForDpi(kTranslationTextEditMargin, LayoutDpi());
    if (sourceEdit_) {
        SendMessageW(sourceEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
            MAKELPARAM(editMargin, editMargin));
    }
    if (translationEdit_) {
        SendMessageW(translationEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
            MAKELPARAM(editMargin, editMargin));
    }
    if (previous) DeleteObject(previous);
    if (previousCompact) DeleteObject(previousCompact);
    if (previousTitle) DeleteObject(previousTitle);
    if (previousText) DeleteObject(previousText);
    if (previousSourceText) DeleteObject(previousSourceText);
}

void TranslationResultWindow::AdjustSourceEditFontSize(int step, bool reset) {
    const int next = reset
        ? sourceFontSize_
        : (std::clamp)(sourceEditFontSize_ + step,
            kTranslationSourceFontSizeMin, kTranslationSourceFontSizeMax);
    if (next == sourceEditFontSize_) return;
    HFONT replacement = TextFont(next, LayoutDpi());
    if (!replacement) return;
    HFONT previous = sourceTextFont_;
    sourceTextFont_ = replacement;
    sourceEditFontSize_ = next;
    if (sourceEdit_) {
        SendMessageW(sourceEdit_, WM_SETFONT,
            reinterpret_cast<WPARAM>(sourceTextFont_), TRUE);
    }
    if (previous) DeleteObject(previous);
    ResizeToAutomaticWindowSize();
}

int TranslationResultWindow::MinimumWindowWidth(UINT dpi, HMONITOR monitor) const {
    const bool compactOcr = !showWindowBorder_ &&
        sourceMode_ == TranslationSourceMode::OcrImage;
    const int designMinimum = ScaleForDpi(
        compactOcr ? kTranslationCompactOcrMinimumWidth
                   : kTranslationAutomaticMinimumWidth, dpi);
    if (!compactOcr) return designMinimum;
    // A small monitor at a high scale factor can offer less than the one-row
    // header wants. Cap the requirement by the work area (never below the shared
    // minimum) so the window still opens fully on screen; the shared combo width
    // then narrows as a set instead of the window running off screen.
    if (!monitor) {
        monitor = MonitorFromRect(&sourceRect_, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) return designMinimum;
    const int workWidthLimit = (std::max)(
        ScaleForDpi(kTranslationAutomaticMinimumWidth, dpi),
        static_cast<int>(monitorInfo.rcWork.right - monitorInfo.rcWork.left) -
            ScaleForDpi(40, dpi));
    return (std::min)(designMinimum, workWidthLimit);
}

void TranslationResultWindow::LayoutControls(bool redraw) {
    if (!window_) return;
    RECT rc = {};
    GetClientRect(window_, &rc);
    const int clientWidth = static_cast<int>(rc.right - rc.left);
    const int clientHeight = static_cast<int>(rc.bottom - rc.top);
    if (clientWidth <= 0 || clientHeight <= 0) return;

    const UINT dpi = LayoutDpi();
    const int margin = ScaleForDpi(4, dpi);
    const bool compactHeader = !showWindowBorder_;
    const int headerIconInset = ScaleForDpi(kTranslationHeaderIconInset, dpi);
    const int headerHeight = ScaleForDpi(compactHeader ? 30 : 58, dpi);
    const int metadataTop = ScaleForDpi(compactHeader ? 5 : 34, dpi);
    const int metadataHeight = ScaleForDpi(20, dpi);
    const int headerButtonWidth = ScaleForDpi(30, dpi);
    const int headerButtonTop = ScaleForDpi(compactHeader ? 0 : 8, dpi);
    const int headerButtonGap = ScaleForDpi(4, dpi);
    const int sourceModeWidth = ScaleForDpi(64, dpi);
    const int sourceModeHeight = ScaleForDpi(22, dpi);
    const int sourceModeTop = headerButtonTop +
        (headerButtonWidth - sourceModeHeight) / 2;
    // The compact header tightens its horizontal rhythm to 5 units to keep the
    // single control row narrow; the bordered layout keeps 6.
    const int rowGap = ScaleForDpi(compactHeader ? 5 : 6, dpi);
    const int controlRowHeight = ScaleForDpi(24, dpi);
    const int comboHeight = ScaleForDpi(24, dpi);
    // Checkbox + gap + the short "Source" / "原文" label.
    const int showSourceWidth = ScaleForDpi(76, dpi);
    const int controlRowGap = ScaleForDpi(kTranslationControlRowGap, dpi);
    const int cardPadding = ScaleForDpi(8, dpi);
    const int cardFooterHeight = ScaleForDpi(kTranslationCardFooterHeight, dpi);
    const int actionHeight = ScaleForDpi(24, dpi);
    const int copyWidth = ScaleForDpi(58, dpi);
    const int countWidth = ScaleForDpi(122, dpi);
    const int elapsedWidth = ScaleForDpi(58, dpi);
    const int retranslateWidth = ScaleForDpi(132, dpi);
    const int contentWidth = (std::max)(ScaleForDpi(120, dpi), clientWidth - margin * 2);
    const int toggleX = margin + ScaleForDpi(8, dpi);
    const auto move = [](HWND control, int x, int y, int width, int height) {
        if (!control) return;
        SetWindowPos(control, nullptr, x, y, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW);
    };

    const int closeX = clientWidth - headerIconInset - headerButtonWidth;
    const int minimizeX = closeX - headerButtonGap - headerButtonWidth;
    const int pinX = compactHeader
        ? closeX - headerButtonGap - headerButtonWidth
        : minimizeX - headerButtonGap - headerButtonWidth;
    move(pinButton_, pinX, headerButtonTop,
        headerButtonWidth, headerButtonWidth);
    if (redraw) SetControlVisible(minimizeButton_, !compactHeader);
    if (!compactHeader) {
        move(minimizeButton_, minimizeX, headerButtonTop,
            headerButtonWidth, headerButtonWidth);
    }
    move(closeButton_, closeX, headerButtonTop,
        headerButtonWidth, headerButtonWidth);

    const bool showOcrControls =
        sourceMode_ == TranslationSourceMode::OcrImage;
    const bool compactOcrHeader = compactHeader && showOcrControls;
    const bool selectorsInHeader = SelectorsInCompactHeader();
    const bool showSourceInHeader = compactHeader;
    const bool showSourceMode = sourceModeButton_ && showSourceText_;
    const bool sourceModeInHeader = !compactHeader && showSourceMode;
    const bool stageInHeader = !compactHeader;
    if (sourceModeButton_) {
        if (redraw) SetControlVisible(sourceModeButton_, showSourceMode);
    }
    const int metadataLeft = ScaleForDpi(kTranslationMetadataTextInset, dpi);
    const int stageMinimumWidth = stageInHeader ? ScaleForDpi(48, dpi) : 0;
    // Every control width is budgeted in a single pass once the label text has
    // been measured, because the compact header row holds Show source, the
    // selectors and (OCR only) the route combo plus the recognize button at
    // once. Placement happens after that budget, so it stays below.
    const int minimumRecognizeWidth = showOcrControls
        ? (compactOcrHeader ? headerButtonWidth : ScaleForDpi(132, dpi)) : 0;
    int recognizeWidth = showOcrControls
        ? (compactOcrHeader ? headerButtonWidth : ScaleForDpi(150, dpi)) : 0;
    const int recognizeHeight = compactOcrHeader ? headerButtonWidth : actionHeight;
    const int recognizeTop = headerButtonTop +
        (headerButtonWidth - recognizeHeight) / 2;
    const int engineMaximumWidth = ScaleForDpi(compactOcrHeader ? 240 : 232, dpi);
    const int engineMinimumWidth = showOcrControls
        ? ScaleForDpi(compactOcrHeader ? 150 : 128, dpi) : 0;

    const int bodyTop = headerHeight + ScaleForDpi(3, dpi);
    const int arrowWidth = ScaleForDpi(16, dpi);
    const int actionWidth = (std::min)(retranslateWidth,
        (std::max)(ScaleForDpi(128, dpi), contentWidth / 5));
    // "Translate" is longer than the existing "Save" label. Keep both
    // source-editor actions readable at the default window size and DPI.
    const int sourceEditorActionWidth = ScaleForDpi(96, dpi);
    const int showSourceToComboGap = ScaleForDpi(24, dpi);
    const int minimumComboWidth = ScaleForDpi(68, dpi);
    const int minimumTargetComboWidth = compactHeader
        ? ScaleForDpi(120, dpi) : minimumComboWidth;
    const int minimumProviderWidth = ScaleForDpi(96, dpi);
    const int providerMaximumWidth = ScaleForDpi(200, dpi);
    const int selectorClusterGaps = rowGap * 3;
    const int minimumSelectorWidth = minimumProviderWidth + minimumComboWidth +
        minimumTargetComboWidth + arrowWidth + selectorClusterGaps;
    // Combo boxes are sized by the label they currently show, not by their
    // longest option: the popup menus measure their own width, so this keeps the
    // shared header row as narrow as the current selection needs and stops an
    // unused long name (provider profile, language, route) from starving the
    // other controls of the one-row compact header.
    int sourceLanguageTextWidth = 0;
    int targetLanguageTextWidth = 0;
    int providerTextWidth = 0;
    int engineTextWidth = 0;
    HDC languageMeasureDc = GetDC(window_);
    if (languageMeasureDc && compactFont_) {
        HGDIOBJ previousFont = SelectObject(languageMeasureDc, compactFont_);
        const auto measureLabel = [&](const std::wstring& text) {
            if (text.empty()) return 0;
            SIZE textSize = {};
            GetTextExtentPoint32W(languageMeasureDc, text.c_str(),
                static_cast<int>(text.size()), &textSize);
            return static_cast<int>(textSize.cx);
        };
        const auto selectedLanguageLabel =
            [](const std::vector<LanguageOption>& languages, int index) {
                if (index < 0 || index >= static_cast<int>(languages.size())) {
                    return std::wstring();
                }
                return languages[static_cast<size_t>(index)].label;
            };
        sourceLanguageTextWidth = measureLabel(
            selectedLanguageLabel(sourceLanguages_, sourceLanguageIndex_));
        targetLanguageTextWidth = measureLabel(
            selectedLanguageLabel(targetLanguages_, targetLanguageIndex_));
        if (providerIndex_ >= 0 &&
            providerIndex_ < static_cast<int>(providerOptions_.size())) {
            // Must match the painted label: the compact header drops the
            // "Provider：" prefix, so its button is sized for the bare name.
            const std::wstring& name =
                providerOptions_[static_cast<size_t>(providerIndex_)].label;
            providerTextWidth = measureLabel(
                compactHeader ? name : ProviderButtonLabel(name));
        }
        if (ocrRouteIndex_ >= 0 &&
            ocrRouteIndex_ < static_cast<int>(ocrRoutes_.size())) {
            // Same rule as the provider button: a compact header paints the
            // route label without the "OCR：" prefix.
            const std::wstring& value =
                ocrRoutes_[static_cast<size_t>(ocrRouteIndex_)].value;
            engineTextWidth = measureLabel(compactOcrHeader
                ? CompactOcrRouteLabel(value) : OcrRouteButtonLabel(value));
        }
        // The route button also shows the resolved engine badge, which the
        // coordinator sets independently of the menu selection ("Current
        // settings" resolves to a concrete engine, so the two usually differ).
        if (compactOcrHeader && engineLabel_) {
            wchar_t badge[128] = {};
            if (GetWindowTextW(engineLabel_, badge,
                    static_cast<int>(std::size(badge))) > 0) {
                std::wstring badgeText(badge);
                const size_t separator = badgeText.find_first_of(L":\uff1a");
                if (separator != std::wstring::npos) {
                    badgeText.erase(0, separator + 1);
                    while (!badgeText.empty() && badgeText.front() == L' ') {
                        badgeText.erase(0, 1);
                    }
                }
                engineTextWidth = (std::max)(engineTextWidth, measureLabel(badgeText));
            }
        }
        SelectObject(languageMeasureDc, previousFont);
    }
    if (languageMeasureDc) ReleaseDC(window_, languageMeasureDc);

    const int comboTextExtra = ScaleForDpi(24, dpi);
    const auto preferredComboWidth = [&](int textWidth, int minimumWidth) {
        const int fallbackWidth = ScaleForDpi(112, dpi);
        return (std::min)(ScaleForDpi(240, dpi),
            (std::max)(minimumWidth,
                textWidth > 0 ? textWidth + comboTextExtra : fallbackWidth));
    };
    // Every combo in the compact header shares one width (see
    // kTranslationCompactComboMinWidth): it follows the longest label currently
    // shown so a longer selection widens the whole set together, and it keeps a
    // comfortable floor so the provider never collapses to its own minimum.
    const int sharedComboWidth = compactHeader
        ? (std::clamp)(
              (std::max)((std::max)(providerTextWidth, sourceLanguageTextWidth),
                         (std::max)(targetLanguageTextWidth, engineTextWidth)) +
                  comboTextExtra,
              ScaleForDpi(kTranslationCompactComboMinWidth, dpi),
              ScaleForDpi(kTranslationCompactComboMaxWidth, dpi))
        : 0;
    int providerWidth = compactHeader
        ? sharedComboWidth
        : (std::min)(providerMaximumWidth,
            (std::max)(minimumProviderWidth,
                providerTextWidth > 0 ? providerTextWidth + comboTextExtra
                                      : ScaleForDpi(128, dpi)));
    int sourceComboWidth = compactHeader
        ? sharedComboWidth
        : preferredComboWidth(sourceLanguageTextWidth, minimumComboWidth);
    int targetComboWidth = compactHeader
        ? sharedComboWidth
        : preferredComboWidth(targetLanguageTextWidth, minimumTargetComboWidth);
    int engineWidth = 0;
    if (showOcrControls) {
        // The compact route combo takes the shared width; the bordered window
        // keeps sizing it by a fraction of the window instead.
        engineWidth = compactOcrHeader
            ? sharedComboWidth
            : (std::min)(engineMaximumWidth,
                (std::max)(engineMinimumWidth, contentWidth / 3));
    }

    if (compactHeader) {
        // One row: Source | selectors | [OCR route | recognize]. The combos share
        // one width, so relief comes out of that shared width first (the whole set
        // narrows together and the row keeps its rhythm); only if the shared floor
        // is not enough do the individual controls give way, cheapest-loss-first
        // (route label, then provider name).
        const int rowLeft = toggleX + showSourceWidth + rowGap;
        const int rowRight = pinX - rowGap;
        // The content that must fit is the selector cluster plus, for OCR, the
        // trailing route/recognize pair. The gap before the pin button is already
        // enforced by the right-anchored placement, so charging it here would make
        // the combos give way earlier than the geometry requires.
        int shortfall = providerWidth + sourceComboWidth + targetComboWidth +
            arrowWidth + selectorClusterGaps +
            (showOcrControls ? rowGap + engineWidth + rowGap + recognizeWidth : 0) -
            (std::max)(0, rowRight - rowLeft);
        if (shortfall > 0) {
            // providerWidth is the shared width in this branch (all combos were
            // set to sharedComboWidth above), and unlike engineWidth it is always
            // present -- selected-text translation has no OCR route combo.
            const int comboCount = showOcrControls ? 4 : 3;
            const int sharedFloor = ScaleForDpi(kTranslationCompactComboMinWidth, dpi);
            const int reduction = (std::min)(shortfall,
                (std::max)(0, providerWidth - sharedFloor) * comboCount);
            if (reduction > 0) {
                const int perCombo = reduction / comboCount;
                if (perCombo > 0) {
                    providerWidth -= perCombo;
                    sourceComboWidth -= perCombo;
                    targetComboWidth -= perCombo;
                    if (showOcrControls) engineWidth -= perCombo;
                    shortfall -= perCombo * comboCount;
                }
            }
        }
        if (shortfall > 0) {
            const int reduction = (std::min)(shortfall,
                engineWidth - engineMinimumWidth);
            engineWidth -= reduction;
            shortfall -= reduction;
        }
        if (shortfall > 0) {
            const int reduction = (std::min)(shortfall,
                providerWidth - minimumProviderWidth);
            providerWidth -= reduction;
            shortfall -= reduction;
        }
        if (shortfall > 0) {
            const int reduction = (std::min)(shortfall,
                sourceComboWidth - minimumComboWidth);
            sourceComboWidth -= reduction;
            shortfall -= reduction;
        }
        if (shortfall > 0) {
            const int reduction = (std::min)(shortfall,
                targetComboWidth - minimumTargetComboWidth);
            targetComboWidth -= reduction;
        }
    } else {
        // Bordered window: the header row keeps the stage label and the OCR
        // controls; the selectors keep their own row below it.
        const int topGapCount = (showOcrControls ? 3 : 1) +
            (sourceModeInHeader ? 1 : 0);
        int topShortfall = stageMinimumWidth + rowGap * topGapCount + engineWidth +
            (sourceModeInHeader ? sourceModeWidth : 0) + recognizeWidth -
            (pinX - metadataLeft);
        if (topShortfall > 0) {
            const int reduction = (std::min)(topShortfall,
                recognizeWidth - minimumRecognizeWidth);
            recognizeWidth -= reduction;
            topShortfall -= reduction;
        }
        if (topShortfall > 0) {
            const int reduction = (std::min)(topShortfall,
                engineWidth - engineMinimumWidth);
            engineWidth -= reduction;
        }
        const int selectorStart = margin + showSourceWidth + rowGap +
            showSourceToComboGap;
        const int selectorAvailable = (std::max)(minimumSelectorWidth,
            (clientWidth - margin) - selectorStart);
        int selectorShortfall = providerWidth + sourceComboWidth + targetComboWidth +
            arrowWidth + selectorClusterGaps - selectorAvailable;
        if (selectorShortfall > 0) {
            const int providerReduction = (std::min)(selectorShortfall,
                providerWidth - minimumProviderWidth);
            providerWidth -= providerReduction;
            selectorShortfall -= providerReduction;
        }
        if (selectorShortfall > 0) {
            const int sourceReduction = (std::min)(selectorShortfall,
                sourceComboWidth - minimumComboWidth);
            sourceComboWidth -= sourceReduction;
            selectorShortfall -= sourceReduction;
        }
        if (selectorShortfall > 0) {
            const int targetReduction = (std::min)(selectorShortfall,
                targetComboWidth - minimumTargetComboWidth);
            targetComboWidth -= targetReduction;
        }
    }

    const int recognizeX = showOcrControls
        ? pinX - rowGap - recognizeWidth : pinX;
    const int engineX = showOcrControls
        ? recognizeX - rowGap - engineWidth : pinX;
    const int sourceModeX = (showOcrControls ? engineX : pinX) -
        rowGap - sourceModeWidth;
    const int engineAnchor = sourceModeInHeader
        ? sourceModeX : (showOcrControls ? engineX : pinX);
    // A compact OCR header right-aligns the selectors against the route combo;
    // every other case right-aligns them against the window (or the card edge).
    const int controlRowRight = compactHeader
        ? (showOcrControls ? engineX - rowGap : pinX - rowGap)
        : clientWidth - margin;
    const int clusterRight = compactHeader
        ? controlRowRight
        : (std::max)(controlRowRight, margin + showSourceWidth +
            showSourceToComboGap + rowGap + minimumSelectorWidth);

    if (sourceModeInHeader) {
        move(sourceModeButton_, sourceModeX, sourceModeTop,
            sourceModeWidth, sourceModeHeight);
    }
    if (showSourceInHeader) {
        move(showSourceToggle_, toggleX,
            (headerHeight - controlRowHeight) / 2,
            showSourceWidth, controlRowHeight);
    }
    if (showOcrControls) {
        move(recognizeButton_, recognizeX, recognizeTop,
            recognizeWidth, recognizeHeight);
    }
    if (stageInHeader) {
        move(stageLabel_, metadataLeft, metadataTop,
            (std::max)(stageMinimumWidth, engineAnchor - metadataLeft - rowGap),
            metadataHeight);
    }
    if (showOcrControls) {
        move(engineLabel_, engineX,
            compactOcrHeader ? (headerHeight - actionHeight) / 2 : metadataTop,
            engineWidth, compactOcrHeader ? actionHeight : metadataHeight);
    }

    int controlTop = selectorsInHeader
        ? (headerHeight - controlRowHeight) / 2
        : bodyTop;
    const int cardsTop = bodyTop;
    const int betweenCardsHeight = selectorsInHeader
        ? controlRowGap
        : controlRowHeight + controlRowGap * 2;
    if (showSourceText_) {
        const int cardSpace = (std::max)(ScaleForDpi(200, dpi),
            clientHeight - cardsTop - betweenCardsHeight - margin);
        const int minSourceHeight = ScaleForDpi(kTranslationSourceMinHeight, dpi);
        const int minTranslationHeight = ScaleForDpi(kTranslationTranslationMinHeight, dpi);
        const int measureWidth = (std::max)(ScaleForDpi(48, dpi),
            contentWidth - cardPadding * 2 -
            ScaleForDpi(kTranslationTextEditMargin * 2, dpi));
        const std::wstring sourceText = SourceText();
        HDC measureDc = GetDC(window_);
        const int sourceTextHeight = MeasureWrappedTextHeight(
            measureDc, sourceTextFont_, sourceText, measureWidth);
        if (measureDc) ReleaseDC(window_, measureDc);
        // Preview has its own vertical content padding and Markdown block
        // margins. Reserve a small allowance so switching modes does not
        // immediately introduce a scrollbar for otherwise fitting text.
        const int previewVerticalAllowance = ScaleForDpi(16, dpi);
        const int previewMetricSafety = ScaleForDpi(kTranslationPreviewMetricSafety, dpi);
        const int sourcePreviewHeight = sourcePreviewMetricsValid_ &&
            sourceDisplayMode_ == SourceDisplayMode::Preview
            ? sourcePreviewContentHeight_ + previewMetricSafety : 0;
        const int sourceContentHeight = (std::max)(
            sourceTextHeight + previewVerticalAllowance, sourcePreviewHeight);
        const int desiredSourceHeight = cardPadding + sourceContentHeight +
            ScaleForDpi(kTranslationCardFooterGap, dpi) +
            cardFooterHeight;
        const int manualSourceMaxHeight = (std::max)(minSourceHeight,
            cardSpace - minTranslationHeight);
        int sourceHeight = 0;
        if (sourceSplitPermille_ >= 0) {
            sourceHeight = MulDiv(cardSpace, sourceSplitPermille_, 1000);
            sourceHeight = (std::clamp)(sourceHeight,
                minSourceHeight, manualSourceMaxHeight);
        } else {
            const int automaticSourceMaxHeight = (std::max)(minSourceHeight,
                (std::min)(manualSourceMaxHeight,
                    cardSpace * kTranslationSourceMaxPercent / 100));
            const int automaticSourceBaselineHeight = (std::min)(
                automaticSourceMaxHeight,
                sourceText.empty()
                    ? (std::max)(minSourceHeight,
                        cardSpace * kTranslationSourceDefaultPercent / 100)
                    : minSourceHeight);
            sourceHeight = (std::clamp)(desiredSourceHeight,
                automaticSourceBaselineHeight, automaticSourceMaxHeight);
        }
        sourceCardRect_ = { margin, cardsTop, clientWidth - margin, cardsTop + sourceHeight };
        sourceSplitterRect_ = { margin, sourceCardRect_.bottom,
            clientWidth - margin, sourceCardRect_.bottom + controlRowGap };
        const int translationTop = sourceCardRect_.bottom + controlRowGap +
            (selectorsInHeader ? 0 : controlRowHeight + controlRowGap);
        translationCardRect_ = { margin, translationTop, clientWidth - margin,
            translationTop + cardSpace - sourceHeight };
        if (!selectorsInHeader) {
            controlTop = sourceCardRect_.bottom + controlRowGap;
        }
    } else {
        sourceCardRect_ = {};
        sourceContentRect_ = {};
        sourceSplitterRect_ = {};
        translationCardRect_ = { margin, cardsTop +
            (selectorsInHeader ? 0 : controlRowHeight + controlRowGap),
            clientWidth - margin, clientHeight - margin };
    }

    const int selectorWidth = providerWidth + sourceComboWidth +
        targetComboWidth + arrowWidth + rowGap * 3;
    const int providerX = clusterRight - selectorWidth;
    const int sourceComboX = providerX + providerWidth + rowGap;
    const int arrowX = sourceComboX + sourceComboWidth + rowGap;
    const int targetComboX = arrowX + arrowWidth + rowGap;
    if (!showSourceInHeader) {
        move(showSourceToggle_, toggleX, controlTop,
            showSourceWidth, controlRowHeight);
    }
    move(providerCombo_, providerX, controlTop, providerWidth, comboHeight);
    move(sourceCombo_, sourceComboX, controlTop, sourceComboWidth, comboHeight);
    move(targetLabel_, arrowX, controlTop, arrowWidth, controlRowHeight);
    move(targetCombo_, targetComboX, controlTop, targetComboWidth, comboHeight);

    const auto layoutCard = [&](const RECT& card, HWND edit, HWND count, HWND copy) {
        const int footerTop = card.bottom - cardFooterHeight;
        const int editTop = card.top + cardPadding;
        const int editHeight = (std::max)(ScaleForDpi(30, dpi),
            footerTop - editTop - ScaleForDpi(kTranslationCardFooterGap, dpi));
        const int footerButtonTop = footerTop + (cardFooterHeight - actionHeight) / 2;
        move(edit, card.left + cardPadding, editTop,
            (std::max)(ScaleForDpi(48, dpi), contentWidth - cardPadding * 2), editHeight);
        if (edit == sourceEdit_) {
            sourceContentRect_ = {
                card.left + cardPadding,
                editTop,
                card.left + cardPadding + (std::max)(ScaleForDpi(48, dpi),
                    contentWidth - cardPadding * 2),
                editTop + editHeight,
            };
        } else if (edit == translationEdit_) {
            translationContentRect_ = {
                card.left + cardPadding,
                editTop,
                card.left + cardPadding + (std::max)(ScaleForDpi(48, dpi),
                    contentWidth - cardPadding * 2),
                editTop + editHeight,
            };
        }
        move(copy, card.left + cardPadding, footerButtonTop, copyWidth, actionHeight);
        int countX = card.left + cardPadding + copyWidth + rowGap;
        if (edit == sourceEdit_ && compactHeader && showSourceMode) {
            move(sourceModeButton_, countX, footerButtonTop,
                sourceModeWidth, actionHeight);
            countX += sourceModeWidth + rowGap;
        }
        move(count, countX, footerTop, countWidth, cardFooterHeight);
        if (edit == sourceEdit_) {
            const int sourceSaveX = card.right - cardPadding - sourceEditorActionWidth;
            const int sourceCancelX = sourceSaveX - rowGap - sourceEditorActionWidth;
            move(sourceEditorCancelButton_, sourceCancelX, footerButtonTop,
                sourceEditorActionWidth, actionHeight);
            move(sourceEditorSaveButton_, sourceSaveX, footerButtonTop,
                sourceEditorActionWidth, actionHeight);
        }
        if (edit == translationEdit_) {
            // Translate again / Cancel share one slot at the footer's right
            // edge. The row already exists, so the action adds no height.
            const int footerActionX = card.right - cardPadding - actionWidth;
            move(retranslateButton_, footerActionX, footerButtonTop,
                actionWidth, actionHeight);
            move(cancelButton_, footerActionX, footerButtonTop,
                actionWidth, actionHeight);
            if (redraw) {
                SetControlVisible(retranslateButton_, !busy_ && !retryOcrMode_);
                SetControlVisible(cancelButton_, busy_);
            }
            const int elapsedX = card.left + cardPadding + copyWidth + rowGap +
                countWidth + rowGap;
            const int elapsedAvailable = footerActionX - rowGap - elapsedX;
            const int minElapsedWidth = ScaleForDpi(40, dpi);
            if (compactHeader) {
                const bool showStage = StageLabelNeedsPresentation(stageLabel_);
                const bool hasStatusRoom = elapsedAvailable >= minElapsedWidth;
                move(stageLabel_, elapsedX, footerTop,
                    (std::max)(0, elapsedAvailable), cardFooterHeight);
                move(translationElapsedLabel_, elapsedX, footerTop,
                    (std::min)(elapsedWidth, (std::max)(0, elapsedAvailable)),
                    cardFooterHeight);
                if (redraw) {
                    SetControlVisible(stageLabel_, showStage && hasStatusRoom);
                    SetControlVisible(translationElapsedLabel_, !showStage &&
                        showTranslationElapsed_ && hasStatusRoom);
                }
            } else if (showTranslationElapsed_ && elapsedAvailable >= minElapsedWidth) {
                move(translationElapsedLabel_, elapsedX, footerTop,
                    (std::min)(elapsedWidth, elapsedAvailable), cardFooterHeight);
                if (redraw) SetControlVisible(translationElapsedLabel_, true);
            } else {
                if (redraw) SetControlVisible(translationElapsedLabel_, false);
            }
        }
    };

    if (showSourceText_) {
        layoutCard(sourceCardRect_, sourceEdit_, sourceCountLabel_, copySourceButton_);
    }
    layoutCard(translationCardRect_, translationEdit_, translationCountLabel_,
        copyTranslationButton_);
    if (redraw) {
        if (translationPreview_) {
            translationPreview_->SetBounds(translationContentRect_);
        }
        if (sourcePreview_) {
            sourcePreview_->SetBounds(sourceContentRect_);
        }
        UpdateSourcePreviewVisibility();
        UpdateTranslationPreviewVisibility();
        RedrawWindow(window_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

void TranslationResultWindow::Paint() {
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(window_, &ps);
    RECT client = {};
    GetClientRect(window_, &client);
    const UINT dpi = LayoutDpi();
    const bool compactHeader = !showWindowBorder_;
    const int headerIconInset = ScaleForDpi(kTranslationHeaderIconInset, dpi);
    const int titleTop = ScaleForDpi(8, dpi);
    const int titleHeight = ScaleForDpi(24, dpi);

    HBRUSH background = CreateSolidBrush(kWindowBackground);
    FillRect(hdc, &client, background);
    DeleteObject(background);

    if (showSourceText_ && sourceCardRect_.right > sourceCardRect_.left) {
        FillRoundedRect(hdc, sourceCardRect_, ScaleForDpi(14, dpi),
            kCardBackground, kCardBorder);
    }
    if (translationCardRect_.right > translationCardRect_.left) {
        FillRoundedRect(hdc, translationCardRect_, ScaleForDpi(14, dpi),
            kCardBackground, kCardBorder);
    }
    if (showSourceText_ && sourceSplitterRect_.right > sourceSplitterRect_.left) {
        const int lineY = sourceSplitterRect_.top +
            (sourceSplitterRect_.bottom - sourceSplitterRect_.top) / 2;
        const int lineInset = ScaleForDpi(8, dpi);
        HPEN splitterPen = CreatePen(PS_SOLID, 1,
            sourceSplitterDragging_ || sourceSplitterHot_ ? kAccent : kCardBorder);
        HGDIOBJ previousPen = SelectObject(hdc, splitterPen);
        MoveToEx(hdc, sourceSplitterRect_.left + lineInset, lineY, nullptr);
        LineTo(hdc, sourceSplitterRect_.right - lineInset, lineY);
        SelectObject(hdc, previousPen);
        DeleteObject(splitterPen);
    }

    if (showWindowBorder_) {
        HBRUSH border = CreateSolidBrush(kCardBorder);
        FrameRect(hdc, &client, border);
        DeleteObject(border);
    }

    if (!compactHeader) {
        HFONT oldFont = titleFont_ ? static_cast<HFONT>(SelectObject(hdc, titleFont_)) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kAccent);
        RECT logoRect = { headerIconInset, titleTop,
            headerIconInset + ScaleForDpi(24, dpi), titleTop + titleHeight };
        DrawTextW(hdc, L"Z", -1, &logoRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, kTextPrimary);
        RECT titleRect = { logoRect.right + ScaleForDpi(8, dpi), titleTop,
            client.right - headerIconInset - ScaleForDpi(76, dpi), titleTop + titleHeight };
        const wchar_t* title = sourceMode_ == TranslationSourceMode::SelectedText
            ? (S::IsChinese() ? L"ZenCrop \u5212\u8bcd\u7ffb\u8bd1" :
                L"ZenCrop Selection Translate")
            : (S::IsChinese() ? L"ZenCrop \u7ffb\u8bd1" : L"ZenCrop Translate");
        DrawTextW(hdc, title, -1,
            &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (oldFont) SelectObject(hdc, oldFont);
    }
    EndPaint(window_, &ps);
}

bool TranslationResultWindow::IsPointInSourceSplitter(POINT point) const {
    return showSourceText_ && PtInRect(&sourceSplitterRect_, point) != FALSE;
}

void TranslationResultWindow::UpdateSourceSplitFromPoint(int clientY) {
    if (!showSourceText_) return;
    const int sourceHeight = sourceCardRect_.bottom - sourceCardRect_.top;
    const int translationHeight = translationCardRect_.bottom - translationCardRect_.top;
    const int cardSpace = sourceHeight + translationHeight;
    if (cardSpace <= 0) return;

    const int minSourceHeight = ScaleForDpi(kTranslationSourceMinHeight, LayoutDpi());
    const int minTranslationHeight = ScaleForDpi(
        kTranslationTranslationMinHeight, LayoutDpi());
    const int maxSourceHeight = (std::max)(minSourceHeight,
        cardSpace - minTranslationHeight);
    const int requestedHeight = clientY - sourceSplitterDragOffset_ - sourceCardRect_.top;
    const int clampedHeight = (std::clamp)(requestedHeight,
        minSourceHeight, maxSourceHeight);
    sourceSplitPermille_ = (std::clamp)(MulDiv(clampedHeight, 1000, cardSpace),
        0, 1000);
    LayoutControls();
}

void TranslationResultWindow::ResetSourceSplit() {
    sourceSplitPermille_ = -1;
    LayoutControls();
}

void TranslationResultWindow::DrawOwnerDrawControl(const DRAWITEMSTRUCT& draw) {
    const int id = static_cast<int>(draw.CtlID);
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool hot = (draw.itemState & ODS_HOTLIGHT) != 0;
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const UINT dpi = LayoutDpi();
    const int radius = ScaleForDpi(7, dpi);

    if (id == kShowSource) {
        HBRUSH backgroundBrush = CreateSolidBrush(kWindowBackground);
        FillRect(draw.hDC, &draw.rcItem, backgroundBrush);
        DeleteObject(backgroundBrush);
        const int box = ScaleForDpi(16, dpi);
        const int left = draw.rcItem.left;
        const int top = draw.rcItem.top +
            ((draw.rcItem.bottom - draw.rcItem.top) - box) / 2;
        RECT check = { left, top, left + box, top + box };
        FillRoundedRect(draw.hDC, check, ScaleForDpi(3, dpi),
            showSourceText_ ? kAccent : kControlBackground,
            showSourceText_ ? kAccent : kTextMuted);
        SetBkMode(draw.hDC, TRANSPARENT);
        SetTextColor(draw.hDC, showSourceText_ ? kTextPrimary : kTextMuted);
        if (showSourceText_) {
            HFONT old = font_ ? static_cast<HFONT>(SelectObject(draw.hDC, font_)) : nullptr;
            DrawTextW(draw.hDC, L"\u2713", -1, &check, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (old) SelectObject(draw.hDC, old);
        }
        RECT text = { check.right + ScaleForDpi(6, dpi), draw.rcItem.top,
            draw.rcItem.right, draw.rcItem.bottom };
        wchar_t label[64] = {};
        GetWindowTextW(draw.hwndItem, label, static_cast<int>(std::size(label)));
        SetTextColor(draw.hDC, disabled ? kTextMuted : kTextPrimary);
        DrawTextW(draw.hDC, label, -1, &text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if ((draw.itemState & ODS_FOCUS) != 0) {
            RECT focus = draw.rcItem;
            InflateRect(&focus, -ScaleForDpi(2, dpi), -ScaleForDpi(2, dpi));
            DrawFocusRect(draw.hDC, &focus);
        }
        return;
    }

    if (id == kEngineLabel || id == kSourceCombo || id == kTargetCombo ||
        id == kProviderCombo) {
        const COLORREF background = pressed ? kControlPressed : hot ? kControlHover : kControlBackground;
        const COLORREF border = disabled ? kControlBackground :
            (pressed ? kControlBorderPressed : hot ? kControlBorderHover : kControlBorder);
        HBRUSH underlay = CreateSolidBrush(kWindowBackground);
        FillRect(draw.hDC, &draw.rcItem, underlay);
        DeleteObject(underlay);
        FillRoundedRect(draw.hDC, draw.rcItem, radius, background, border);
        wchar_t label[128] = {};
        GetWindowTextW(draw.hwndItem, label, static_cast<int>(std::size(label)));
        const wchar_t* visualLabel = label;
        // Every compact (borderless) header paints the bare profile name for the
        // provider control, not just the OCR variant. At the default compact
        // window size the "Provider：" prefix alone eats most of the button's
        // text width, so the name -- the only part that identifies the model
        // actually in use -- was the piece that got ellipsized.
        const bool compactHeader = !showWindowBorder_;
        const bool compactOcr = compactHeader &&
            sourceMode_ == TranslationSourceMode::OcrImage;
        if (compactOcr && id == kEngineLabel) {
            const wchar_t* separator = wcschr(label, L':');
            if (!separator) separator = wcschr(label, L'\uff1a');
            if (separator) {
                visualLabel = separator + 1;
                while (*visualLabel == L' ') ++visualLabel;
            }
        } else if (compactHeader && id == kProviderCombo &&
                   providerIndex_ >= 0 &&
                   providerIndex_ < static_cast<int>(providerOptions_.size())) {
            visualLabel = providerOptions_[providerIndex_].label.c_str();
        }
        // Padding 6 + caret column 16 = 22, matching comboTextExtra (24) with a
        // two unit slack so rounding can never ellipsize a fitting label.
        const int textPadding = ScaleForDpi(6, dpi);
        const int arrowWidth = ScaleForDpi(16, dpi);
        RECT text = { draw.rcItem.left + textPadding, draw.rcItem.top,
            draw.rcItem.right - arrowWidth, draw.rcItem.bottom };
        SetBkMode(draw.hDC, TRANSPARENT);
        SetTextColor(draw.hDC, disabled ? kTextMuted : kTextPrimary);
        DrawTextW(draw.hDC, visualLabel, -1, &text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const int midX = draw.rcItem.right - arrowWidth / 2 - ScaleForDpi(4, dpi);
        const int midY = (draw.rcItem.top + draw.rcItem.bottom) / 2;
        const int half = ScaleForDpi(4, dpi);
        POINT triangle[] = {
            { midX - half, midY - half / 2 },
            { midX + half, midY - half / 2 },
            { midX, midY + half / 2 + 1 },
        };
        HBRUSH arrow = CreateSolidBrush(disabled || !hot ? kTextMuted : kTextPrimary);
        HGDIOBJ oldBrush = SelectObject(draw.hDC, arrow);
        HGDIOBJ oldPen = SelectObject(draw.hDC, GetStockObject(NULL_PEN));
        Polygon(draw.hDC, triangle, static_cast<int>(std::size(triangle)));
        SelectObject(draw.hDC, oldPen);
        SelectObject(draw.hDC, oldBrush);
        DeleteObject(arrow);
        if ((draw.itemState & ODS_FOCUS) != 0) {
            RECT focus = draw.rcItem;
            InflateRect(&focus, -ScaleForDpi(2, dpi), -ScaleForDpi(2, dpi));
            DrawFocusRect(draw.hDC, &focus);
        }
        return;
    }

    if (id == kPin) {
        HBRUSH background = CreateSolidBrush(kWindowBackground);
        FillRect(draw.hDC, &draw.rcItem, background);
        DeleteObject(background);
        const COLORREF pinColor = disabled ? kTextMuted :
            (alwaysOnTop_ ? (pressed ? kAccentPressed : hot ? kAccentHover : kAccent) : kTextMuted);
        const int centerX = (draw.rcItem.left + draw.rcItem.right) / 2;
        const int centerY = (draw.rcItem.top + draw.rcItem.bottom) / 2;
        constexpr double kInvSqrt2 = 0.7071067811865476;
        const auto rotate = [&](int designX, int designY) {
            const int x = MulDiv(designX, static_cast<int>(dpi),
                static_cast<int>(kTranslationDesignDpi));
            const int y = MulDiv(designY, static_cast<int>(dpi),
                static_cast<int>(kTranslationDesignDpi));
            return POINT{
                centerX + static_cast<int>(std::lround((x - y) * kInvSqrt2)),
                centerY + static_cast<int>(std::lround((x + y) * kInvSqrt2)),
            };
        };

        // Use a thin, tilted thumbtack outline rather than a filled toolbar
        // glyph. Keep the hit target roomy but render the
        // actual icon at the same compact visual weight.
        POINT outline[] = {
            rotate(-5, -7), rotate(5, -7), rotate(5, -3), rotate(7, -1),
            rotate(7, 1), rotate(-7, 1), rotate(-7, -1), rotate(-5, -3),
        };
        HPEN pen = CreatePen(PS_SOLID, 1, pinColor);
        HGDIOBJ oldPen = SelectObject(draw.hDC, pen);
        HGDIOBJ oldBrush = SelectObject(draw.hDC, GetStockObject(NULL_BRUSH));
        Polygon(draw.hDC, outline, static_cast<int>(std::size(outline)));
        const POINT needleStart = rotate(0, 1);
        const POINT needleEnd = rotate(0, 8);
        MoveToEx(draw.hDC, needleStart.x, needleStart.y, nullptr);
        LineTo(draw.hDC, needleEnd.x, needleEnd.y);
        SelectObject(draw.hDC, oldPen);
        SelectObject(draw.hDC, oldBrush);
        DeleteObject(pen);
        if ((draw.itemState & ODS_FOCUS) != 0) {
            RECT focus = draw.rcItem;
            InflateRect(&focus, -ScaleForDpi(2, dpi), -ScaleForDpi(2, dpi));
            DrawFocusRect(draw.hDC, &focus);
        }
        return;
    }

    const bool isHeaderButton = id == kMinimize || id == kClose;
    const bool usesHeaderUnderlay = id == kSourceMode ||
        (id == kRecognizeAgain && !showWindowBorder_);
    const bool isCopyButton = id == kCopySource || id == kCopyTranslation;
    const bool isPrimary = id == kRetranslate || id == kSourceEditorSave;
    COLORREF background = isHeaderButton ? kWindowBackground :
        (isPrimary ? (pressed ? kAccentPressed : hot ? kAccentHover : kAccent)
                   : (pressed ? kControlPressed : hot ? kControlHover : kControlBackground));
    COLORREF border = isPrimary ? background :
        (pressed ? kControlBorderPressed : hot ? kControlBorderHover : kControlBorder);
    if (id == kClose && hot) {
        background = kCloseHover;
        border = kCloseHover;
    }
    if (isCopyButton) {
        background = hot ? kControlHover : kCardBackground;
        border = hot ? kControlBorderHover : kCardBackground;
    }
    if (disabled) {
        background = isPrimary ? RGB(58, 77, 104) :
            (isCopyButton ? kCardBackground : kControlBackground);
        border = isPrimary ? background :
            (isCopyButton ? kCardBackground : kControlBackground);
    }

    if (isHeaderButton) {
        HBRUSH brush = CreateSolidBrush(background);
        FillRect(draw.hDC, &draw.rcItem, brush);
        DeleteObject(brush);
    } else if (isCopyButton) {
        HBRUSH underlay = CreateSolidBrush(kCardBackground);
        FillRect(draw.hDC, &draw.rcItem, underlay);
        DeleteObject(underlay);
        FillRoundedRect(draw.hDC, draw.rcItem, ScaleForDpi(5, dpi),
            background, border);
    } else {
        HBRUSH underlay = CreateSolidBrush(
            usesHeaderUnderlay ? kWindowBackground : kCardBackground);
        FillRect(draw.hDC, &draw.rcItem, underlay);
        DeleteObject(underlay);
        FillRoundedRect(draw.hDC, draw.rcItem, radius, background, border);
    }

    wchar_t label[96] = {};
    GetWindowTextW(draw.hwndItem, label, static_cast<int>(std::size(label)));
    const wchar_t* visualLabel = id == kMinimize ? L"\u2212" :
        (id == kClose ? L"\u00d7" :
        (id == kRecognizeAgain && !showWindowBorder_ ? L"\u21bb" : label));
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, disabled ? kTextMuted :
        (isPrimary ? RGB(255, 255, 255) : (isCopyButton ? kTextMuted : kTextPrimary)));
    RECT textRect = draw.rcItem;
    DrawTextW(draw.hDC, visualLabel, -1, &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if ((draw.itemState & ODS_FOCUS) != 0) {
        RECT focus = draw.rcItem;
        InflateRect(&focus, -ScaleForDpi(2, dpi), -ScaleForDpi(2, dpi));
        DrawFocusRect(draw.hDC, &focus);
    }
}

LRESULT CALLBACK TranslationResultWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TranslationResultWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<TranslationResultWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = hwnd;
    } else {
        self = reinterpret_cast<TranslationResultWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT TranslationResultWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case kAsyncErrorMessage: {
        std::unique_ptr<AsyncErrorPayload> payload(
            reinterpret_cast<AsyncErrorPayload*>(lParam));
        if (payload) {
            if (payload->workflowGeneration != 0 &&
                payload->workflowGeneration != workflowGeneration_.load(
                    std::memory_order_acquire)) {
                return 0;
            }
            translationPreviewFailed_ = true;
            if (translationPreview_) translationPreview_->Show(false);
            SetBusy(false);
            SetRetryOcrMode(payload->retryOcr);
            SetStage(payload->message);
            UpdateTranslationPreviewVisibility();
        }
        return 0;
    }
    case kTranslationChildKeyboardMessage:
        HandleChildKey(reinterpret_cast<HWND>(lParam), wParam);
        return 0;
    case kTranslationChildZoomMessage:
        if (reinterpret_cast<HWND>(lParam) == sourceEdit_) {
            const auto signedStep = static_cast<INT_PTR>(wParam);
            AdjustSourceEditFontSize(
                signedStep > 0 ? 1 : signedStep < 0 ? -1 : 0,
                signedStep == 0);
        }
        return 0;
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCHITTEST: {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &point);
        RECT client = {};
        GetClientRect(hwnd, &client);
        const int border = (std::max)(ScaleForDpi(6, LayoutDpi()), 5);
        const bool left = point.x < client.left + border;
        const bool right = point.x >= client.right - border;
        const bool top = point.y < client.top + border;
        const bool bottom = point.y >= client.bottom - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        // The compact header row is packed with controls, so the 3px strip
        // between it and the first card counts as drag area as well.
        const int headerHitHeight = ScaleForDpi(showWindowBorder_ ? 58 : 33, LayoutDpi());
        if (point.y < headerHitHeight &&
            !PointInChild(hwnd, showSourceToggle_, point) &&
            !PointInChild(hwnd, providerCombo_, point) &&
            !PointInChild(hwnd, sourceCombo_, point) &&
            !PointInChild(hwnd, targetLabel_, point) &&
            !PointInChild(hwnd, targetCombo_, point) &&
            !PointInChild(hwnd, engineLabel_, point) &&
            !PointInChild(hwnd, recognizeButton_, point) &&
            !PointInChild(hwnd, sourceModeButton_, point) &&
            !PointInChild(hwnd, pinButton_, point) &&
            !PointInChild(hwnd, minimizeButton_, point) &&
            !PointInChild(hwnd, closeButton_, point)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_SETCURSOR: {
        POINT point = {};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        if (sourceSplitterDragging_ || IsPointInSourceSplitter(point)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (IsPointInSourceSplitter(point)) {
            sourceSplitterDragging_ = true;
            sourceSplitterHot_ = true;
            sourceSplitterDragOffset_ = point.y - sourceCardRect_.bottom;
            SetCapture(hwnd);
            InvalidateRect(hwnd, &sourceSplitterRect_, FALSE);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (sourceSplitterDragging_) {
            UpdateSourceSplitFromPoint(point.y);
            return 0;
        }
        const bool hot = IsPointInSourceSplitter(point);
        if (hot != sourceSplitterHot_) {
            sourceSplitterHot_ = hot;
            InvalidateRect(hwnd, &sourceSplitterRect_, FALSE);
            if (hot) {
                TRACKMOUSEEVENT tracking = { sizeof(tracking), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tracking);
            }
        }
        break;
    }
    case WM_MOUSELEAVE:
        if (!sourceSplitterDragging_ && sourceSplitterHot_) {
            sourceSplitterHot_ = false;
            InvalidateRect(hwnd, &sourceSplitterRect_, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (sourceSplitterDragging_) {
            sourceSplitterDragging_ = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            sourceSplitterHot_ = IsPointInSourceSplitter(point);
            InvalidateRect(hwnd, &sourceSplitterRect_, FALSE);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (sourceSplitterDragging_) {
            sourceSplitterDragging_ = false;
            sourceSplitterHot_ = false;
            InvalidateRect(hwnd, &sourceSplitterRect_, FALSE);
        }
        return 0;
    case WM_LBUTTONDBLCLK: {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (IsPointInSourceSplitter(point)) {
            if (GetCapture() == hwnd) ReleaseCapture();
            sourceSplitterDragging_ = false;
            sourceSplitterHot_ = true;
            ResetSourceSplit();
            return 0;
        }
        break;
    }
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        const bool onCard = control == sourceCountLabel_ || control == translationCountLabel_ ||
            control == translationElapsedLabel_ || control == translationEdit_ ||
            (control == stageLabel_ && !showWindowBorder_);
        SetBkColor(hdc, onCard ? kCardBackground : kWindowBackground);
        SetTextColor(hdc, control == targetLabel_ ||
            control == translationEdit_
            ? kTextPrimary : kTextMuted);
        static HBRUSH windowBrush = CreateSolidBrush(kWindowBackground);
        static HBRUSH cardBrush = CreateSolidBrush(kCardBackground);
        return reinterpret_cast<LRESULT>(onCard ? cardBrush : windowBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kCardBackground);
        SetTextColor(hdc, kTextPrimary);
        static HBRUSH cardBrush = CreateSolidBrush(kCardBackground);
        return reinterpret_cast<LRESULT>(cardBrush);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kCardBackground);
        SetTextColor(hdc, kTextPrimary);
        static HBRUSH cardBrush = CreateSolidBrush(kCardBackground);
        return reinterpret_cast<LRESULT>(cardBrush);
    }
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure && measure->CtlType == ODT_MENU) {
            MeasureCompactPopupItem(*measure, compactFont_, LayoutDpi(),
                popupMenuAnchorWidth_, popupMenuAnchorEngineLabel_);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!draw) break;
        if (draw->CtlType == ODT_MENU) {
            DrawCompactPopupItem(*draw, compactFont_, LayoutDpi());
            return TRUE;
        }
        if (draw->CtlType == ODT_BUTTON) {
            DrawOwnerDrawControl(*draw);
            return TRUE;
        }
        break;
    }
    case WM_SIZE:
        LayoutControls(!resizeAnimationActive_);
        return 0;
    case WM_ENTERSIZEMOVE:
        StopAutomaticResizeAnimation(true);
        windowSizeMoveActive_ = true;
        GetWindowRect(hwnd, &windowSizeMoveStartRect_);
        return 0;
    case WM_EXITSIZEMOVE:
        if (windowSizeMoveActive_) {
            RECT current = {};
            GetWindowRect(hwnd, &current);
            if (current.left != windowSizeMoveStartRect_.left ||
                current.top != windowSizeMoveStartRect_.top) {
                autoPositionNearSource_ = false;
            }
            if (current.right - current.left !=
                    windowSizeMoveStartRect_.right - windowSizeMoveStartRect_.left ||
                current.bottom - current.top !=
                    windowSizeMoveStartRect_.bottom - windowSizeMoveStartRect_.top) {
                windowSizeManuallyAdjusted_ = true;
            }
            windowSizeMoveActive_ = false;
        }
        return 0;
    case WM_TIMER:
        if (wParam == kResizeAnimationTimer) {
            UpdateAutomaticResizeAnimation();
            return 0;
        }
        if (wParam == kOcrElapsedTimer) {
            UpdateOcrElapsedStage();
            return 0;
        }
        if (wParam == kTranslationElapsedTimer) {
            UpdateTranslationElapsedStage();
            return 0;
        }
        if (wParam == kStructuredSelectionTimer) {
            CancelPendingStructuredSelection(L"conversion_timeout");
            return 0;
        }
        break;
    case WM_DPICHANGED: {
        StopAutomaticResizeAnimation(false);
        SetLayoutDpi(LOWORD(wParam));
        RefreshFontForLayoutDpi();
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ApplyDarkWindowChrome();
        ResizeToAutomaticWindowSize();
        if (autoPositionNearSource_) {
            ClampToCurrentMonitorWorkArea();
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = LayoutDpi();
        info->ptMinTrackSize.x = MinimumWindowWidth(dpi);
        info->ptMinTrackSize.y = ScaleForDpi(420, dpi);
        return 0;
    }
    case WM_COMMAND:
        if (suppressCommands_) return 0;
        if (LOWORD(wParam) == kShowSource && HIWORD(wParam) == BN_CLICKED) {
            if (manualEntryMode_) return 0;
            SetShowSourceText(!showSourceText_);
            InvokeCommandSafely(Command::ToggleShowSource);
            return 0;
        }
        if (LOWORD(wParam) == kSourceMode && HIWORD(wParam) == BN_CLICKED) {
            SetSourceDisplayMode(
                sourceDisplayMode_ == SourceDisplayMode::Preview
                    ? SourceDisplayMode::Source
                    : SourceDisplayMode::Preview,
                sourceDisplayMode_ == SourceDisplayMode::Preview);
            return 0;
        }
        if (LOWORD(wParam) == kSourceCombo && HIWORD(wParam) == BN_CLICKED) {
            ShowLanguageMenu(sourceCombo_, true);
            return 0;
        }
        if (LOWORD(wParam) == kTargetCombo && HIWORD(wParam) == BN_CLICKED) {
            ShowLanguageMenu(targetCombo_, false);
            return 0;
        }
        if (sourceMode_ == TranslationSourceMode::OcrImage &&
            LOWORD(wParam) == kEngineLabel && HIWORD(wParam) == BN_CLICKED) {
            ShowOcrRouteMenu();
            return 0;
        }
        if (LOWORD(wParam) == kProviderCombo && HIWORD(wParam) == BN_CLICKED) {
            ShowProviderMenu();
            return 0;
        }
        if (LOWORD(wParam) == kCopySource && HIWORD(wParam) == BN_CLICKED) {
            CopyControlText(sourceEdit_);
            return 0;
        }
        if (LOWORD(wParam) == kSourceEditorCancel && HIWORD(wParam) == BN_CLICKED) {
            CancelSourceDocumentEditor();
            return 0;
        }
        if (LOWORD(wParam) == kSourceEditorSave && HIWORD(wParam) == BN_CLICKED) {
            if (sourcePreview_) {
                if (manualEntryMode_) {
                    translateAfterEditorClose_ = true;
                    translateEntryGeneration_ = manualEntryGeneration_;
                }
                if (manualEntryMode_ && !sourcePreview_->HasDirtyEditor()) {
                    cancelManualEditorRequested_ = false;
                    sourcePreview_->CancelActiveEditor();
                } else {
                    sourcePreview_->RequestActiveEditorSave();
                }
            }
            return 0;
        }
        if (LOWORD(wParam) == kCopyTranslation && HIWORD(wParam) == BN_CLICKED) {
            CopyControlText(translationEdit_);
            return 0;
        }
        if (sourceMode_ == TranslationSourceMode::OcrImage &&
            LOWORD(wParam) == kRecognizeAgain && HIWORD(wParam) == BN_CLICKED) {
            InvokeCommandSafely(Command::RecognizeAgain);
            return 0;
        }
        if (LOWORD(wParam) == kRetranslate && HIWORD(wParam) == BN_CLICKED) {
            InvokeCommandSafely(Command::Retranslate);
            return 0;
        }
        if (LOWORD(wParam) == kCancel && HIWORD(wParam) == BN_CLICKED) {
            InvokeCommandSafely(Command::Cancel);
            return 0;
        }
        if (LOWORD(wParam) == kPin && HIWORD(wParam) == BN_CLICKED) {
            InvokeCommandSafely(Command::ToggleAlwaysOnTop);
            return 0;
        }
        if (LOWORD(wParam) == kMinimize && HIWORD(wParam) == BN_CLICKED) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        if (LOWORD(wParam) == kClose && HIWORD(wParam) == BN_CLICKED) {
            NotifyClose();
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kSourceEdit && HIWORD(wParam) == EN_CHANGE) {
            if (sourceCountLabel_) {
                const std::wstring count = CharacterCountText(SourceText());
                SetWindowTextW(sourceCountLabel_, count.c_str());
            }
            ResizeToAutomaticWindowSize();
            MarkDirty();
            UpdateActionAvailability();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == L'W' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            if (!busy_ && sourcePreview_ && sourcePreview_->HasActiveEditor()) {
                CancelSourceDocumentEditor();
            } else {
                HandleEscape();
            }
            return 0;
        }
        if (wParam == VK_TAB) {
            FocusRelative(GetFocus(), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        NotifyClose();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kResizeAnimationTimer);
        KillTimer(hwnd, kStructuredSelectionTimer);
        pendingStructuredSelectionCallback_ = {};
        pendingStructuredSelectionHost_ = PreviewSelectionHost::None;
        pendingStructuredSelectionToken_.clear();
        pendingStructuredSelectionGeneration_ = 0;
        resizeAnimationActive_ = false;
        NotifyClose();
        // Do not leave a stale entry or a border window behind if the user
        // closes the result window while it is pinned.
        if (AlwaysOnTopManager::Instance().IsPinned(hwnd)) {
            AlwaysOnTopManager::Instance().UnpinWindow(hwnd);
        }
        alwaysOnTop_ = false;
        return 0;
    case WM_NCDESTROY:
        window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace translation
