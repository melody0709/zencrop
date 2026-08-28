#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "dashboard/DashboardTheme.h"
#include "Strings.h"
#include "core/WideStringUtils.h"

#include <gdiplus.h>
#include <windows.h>

// D-I-1: real translation unit (was OcrDashboardWindow.HistoryPaint.inl).

void OcrDashboardWindow::DrawHistorySeparators(HDC hdc, const RECT& rc) {
    if (m_historyRanges.empty()) return;

    m_actionButtons.clear();

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Fonts for buttons
    HFONT hFont = (HFONT)SendMessage(m_edit, WM_GETFONT, 0, 0);
    HDC hdcMeasure = GetDC(m_edit);
    HFONT hOldFont = (HFONT)SelectObject(hdcMeasure, hFont);
    TEXTMETRIC tm;
    GetTextMetrics(hdcMeasure, &tm);
    SelectObject(hdcMeasure, hOldFont);
    ReleaseDC(m_edit, hdcMeasure);

    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font smallFont(&fontFamily, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    COLORREF oldTextColor = SetTextColor(hdc, Theme::textAccent);
    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    HFONT oldDrawFont = (HFONT)SelectObject(hdc, hFont);

    for (size_t i = 0; i < m_historyRanges.size(); i++) {
        DWORD pos = (DWORD)SendMessage(m_edit, EM_POSFROMCHAR, m_historyRanges[i].startChar, 0);
        if (pos == 0xFFFFFFFF) continue;
        int headerY = HIWORD(pos);
        if (headerY + tm.tmHeight < rc.top || headerY > rc.bottom) continue;

        const auto* itemPtr = m_history.model.itemAt(m_historyRanges[i].itemIndex);
        if (!itemPtr) continue;
        const auto& item = *itemPtr;
        std::wstring elapsedStr;
        if (item.elapsedMs > 0) {
            // OWN-114: pure elapsed paren seconds label (WideStringUtils).
            elapsedStr = WideFormatElapsedParenSeconds1(item.elapsedMs / 1000.0);
        }
        // OWN-127: pure pin header (WideStringUtils).
        std::wstring header = WideFormatHistoryPinHeader(m_historyRanges[i].itemIndex + 1) +
            item.timestamp + elapsedStr;
        RECT textRc = {
            m_metrics.historyLeftPad,
            headerY,
            rc.right - m_metrics.historyHeaderReserveW,
            headerY + tm.tmHeight + m_metrics.historyLinePadY
        };
        DrawTextW(hdc, header.c_str(), -1, &textRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    SelectObject(hdc, oldDrawFont);
    SetBkMode(hdc, oldBkMode);
    SetTextColor(hdc, oldTextColor);

    // Draw expand hints on the blank line after truncated previews.
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldHintFont = (HFONT)SelectObject(hdc, hFont);
    COLORREF oldHintColor = SetTextColor(hdc, Theme::textMuted);
    std::wstring expandHint = S::IsChinese() ? L"...  (双击展开)" : L"...  (double-click to expand)";
    for (const auto& info : m_previewInfos) {
        if (!info.truncated ||
            info.itemIndex == DashboardStateExpandedHistoryIndex(m_dashboardState)) {
            continue;
        }
        DWORD hintPos = (DWORD)SendMessage(m_edit, EM_POSFROMCHAR, info.hintChar, 0);
        if (hintPos == 0xFFFFFFFF) continue;
        int hintY = HIWORD(hintPos);
        if (hintY + tm.tmHeight < rc.top || hintY > rc.bottom) continue;

        RECT hintRc = {
            m_metrics.historyLeftPad,
            hintY,
            rc.right - m_metrics.historyLeftPad,
            hintY + tm.tmHeight + m_metrics.historyLinePadY
        };
        DrawTextW(hdc, expandHint.c_str(), -1, &hintRc,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    SetTextColor(hdc, oldHintColor);
    SelectObject(hdc, oldHintFont);
    SetBkMode(hdc, oldBkMode);

    // Draw separator lines between records
    Gdiplus::Pen sepPen(Gdiplus::Color(80, 80, 80), ScaleF(2.0f));
    for (size_t i = 1; i < m_historyRanges.size(); i++) {
        DWORD pos2 = (DWORD)SendMessage(m_edit, EM_POSFROMCHAR, m_historyRanges[i].startChar, 0);
        if (pos2 == 0xFFFFFFFF) continue;
        int y2 = HIWORD(pos2);

        int sepY = y2 - m_metrics.historySepOffsetY;

        if (sepY >= rc.top - m_metrics.historySepClipPad && sepY <= rc.bottom + m_metrics.historySepClipPad) {
            graphics.DrawLine(&sepPen, m_metrics.historyLeftPad, sepY, rc.right - m_metrics.historyRightPad, sepY);
        }
    }

    auto drawButton = [&](const HistoryActionButton& button, const std::wstring& label, int hoverIndex) {
        int radius = m_metrics.historyButtonRadius;
        bool isHovered = (DashboardStateHoveredActionBtn(m_dashboardState) == hoverIndex);

        Gdiplus::GraphicsPath path;
        int btnW = button.rc.right - button.rc.left;
        int btnH = button.rc.bottom - button.rc.top;
        int btnX = button.rc.left;
        int btnY = button.rc.top;
        path.AddArc(btnX, btnY, radius * 2, radius * 2, 180, 90);
        path.AddArc(btnX + btnW - radius * 2, btnY, radius * 2, radius * 2, 270, 90);
        path.AddArc(btnX + btnW - radius * 2, btnY + btnH - radius * 2, radius * 2, radius * 2, 0, 90);
        path.AddArc(btnX, btnY + btnH - radius * 2, radius * 2, radius * 2, 90, 90);
        path.CloseFigure();

        Gdiplus::SolidBrush bgBrush(isHovered ?
            Gdiplus::Color(70, 70, 73) : Gdiplus::Color(50, 50, 53));
        graphics.FillPath(&bgBrush, &path);

        Gdiplus::Pen borderPen(isHovered ?
            Gdiplus::Color(100, 100, 100) : Gdiplus::Color(70, 70, 70), 1.0f);
        graphics.DrawPath(&borderPen, &path);

        Gdiplus::SolidBrush textBrush(isHovered ?
            Gdiplus::Color(220, 220, 220) : Gdiplus::Color(160, 160, 160));
        graphics.DrawString(label.c_str(), -1, &smallFont,
            Gdiplus::RectF((float)btnX, (float)btnY, (float)btnW, (float)btnH),
            &centerFormat, &textBrush);
    };

    int btnW = S::IsChinese() ? m_metrics.historyButtonWZh : m_metrics.historyButtonW;
    int btnH = m_metrics.historyButtonH;
    int gap = m_metrics.historyButtonGap;
    std::wstring copyLabel = S::IsChinese() ? L"复制" : L"Copy";
    std::wstring deleteLabel = S::IsChinese() ? L"删除" : L"Del";
    for (size_t i = 0; i < m_historyRanges.size(); i++) {
        DWORD pos = (DWORD)SendMessage(m_edit, EM_POSFROMCHAR, m_historyRanges[i].startChar, 0);
        if (pos == 0xFFFFFFFF) continue;
        int headerY = HIWORD(pos);

        int deleteX = rc.right - m_metrics.historyRightPad - btnW;
        int copyX = deleteX - gap - btnW;
        int btnY = headerY + (tm.tmHeight - btnH) / 2;

        if (btnY + btnH < rc.top - m_metrics.historyButtonClipPad ||
            btnY > rc.bottom + m_metrics.historyButtonClipPad) continue;

        HistoryActionButton copyBtn = { { copyX, btnY, copyX + btnW, btnY + btnH }, m_historyRanges[i].itemIndex, HistoryAction::Copy };
        HistoryActionButton deleteBtn = { { deleteX, btnY, deleteX + btnW, btnY + btnH }, m_historyRanges[i].itemIndex, HistoryAction::Delete };
        int copyHoverIndex = (int)m_actionButtons.size();
        m_actionButtons.push_back(copyBtn);
        drawButton(copyBtn, copyLabel, copyHoverIndex);

        int deleteHoverIndex = (int)m_actionButtons.size();
        m_actionButtons.push_back(deleteBtn);
        drawButton(deleteBtn, deleteLabel, deleteHoverIndex);
    }
}
