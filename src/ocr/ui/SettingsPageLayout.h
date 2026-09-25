#pragma once
#include "SettingsDialogInternal.h"
#include <algorithm>
#include <commctrl.h>

namespace settings_ui {
class SettingsPageLayout {
public:
    SettingsPageLayout(HWND hPage, UINT dpi)
        : m_page(hPage), m_dpi(dpi) {
        RECT rc;
        GetClientRect(hPage, &rc);
        m_clientW = rc.right - rc.left;
        if (m_clientW < 100) m_clientW = Scale(520);

        m_padX = Scale(18);
        m_padY = Scale(18);
        m_labelW = Scale(110);
        m_ctrlX = m_padX + m_labelW + Scale(8);
        // The row column takes everything left of the right margin so the page uses the
        // full window width; the floor keeps narrow (minimum-size) windows usable.
        m_ctrlW = m_clientW - m_ctrlX - m_padX;
        if (m_ctrlW < Scale(240)) m_ctrlW = Scale(240);
        m_rowH = Scale(22);
        m_rowGap = Scale(14);
        m_rowStep = m_rowH + m_rowGap;
        m_y = m_padY;
    }

    int Scale(int val) const { return MulDiv(val, m_dpi, 96); }

    int PadX() const { return m_padX; }
    int LabelW() const { return m_labelW; }
    int CtrlX() const { return m_ctrlX; }
    int CtrlW() const { return m_ctrlW; }
    int RowH() const { return m_rowH; }
    int CurrentY() const { return m_y; }
    void AdvanceRow() { m_y += m_rowStep; }
    void AddSpace(int px = 8) { m_y += Scale(px); }

    void AddRow(int labelId, int ctrlId, int width = 0) {
        HWND hLabel = GetDlgItem(m_page, labelId);
        HWND hCtrl = GetDlgItem(m_page, ctrlId);
        int w = width > 0 ? Scale(width) : m_ctrlW;
        if (hLabel) MoveWindow(hLabel, m_padX, m_y + Scale(2), m_labelW, m_rowH, TRUE);
        if (hCtrl) MoveWindow(hCtrl, m_ctrlX, m_y, w, m_rowH, TRUE);
        m_y += m_rowStep;
    }

    void AddRowWithButton(int labelId, int ctrlId, int btnId, int btnWidth = 72) {
        HWND hLabel = GetDlgItem(m_page, labelId);
        HWND hCtrl = GetDlgItem(m_page, ctrlId);
        HWND hBtn = GetDlgItem(m_page, btnId);
        int bW = Scale(btnWidth);
        int cW = m_ctrlW - bW - Scale(6);
        if (hLabel) MoveWindow(hLabel, m_padX, m_y + Scale(2), m_labelW, m_rowH, TRUE);
        if (hCtrl) MoveWindow(hCtrl, m_ctrlX, m_y, cW, m_rowH, TRUE);
        if (hBtn) MoveWindow(hBtn, m_ctrlX + cW + Scale(6), m_y, bW, m_rowH, TRUE);
        m_y += m_rowStep;
    }

    void AddHotkeyRow(int labelId, int editId, int clearId) {
        HWND hLabel = GetDlgItem(m_page, labelId);
        HWND hEdit = GetDlgItem(m_page, editId);
        HWND hClear = GetDlgItem(m_page, clearId);
        int clearW = Scale(24);
        int editW = m_ctrlW - clearW - Scale(6);
        if (hLabel) MoveWindow(hLabel, m_padX, m_y + Scale(2), m_labelW, m_rowH, TRUE);
        if (hEdit) {
            HFONT pageFont = (HFONT)SendMessageW(m_page, WM_GETFONT, 0, 0);
            if (pageFont) SendMessageW(hEdit, WM_SETFONT, (WPARAM)pageFont, 0);
            MoveWindow(hEdit, m_ctrlX, m_y, editW, m_rowH, TRUE);
        }
        if (hClear) MoveWindow(hClear, m_ctrlX + editW + Scale(6), m_y, clearW, m_rowH, TRUE);
        m_y += m_rowStep;
    }

    void AddColorRow(int labelId, int previewId, int chooseBtnId) {
        HWND hLabel = GetDlgItem(m_page, labelId);
        HWND hPreview = GetDlgItem(m_page, previewId);
        HWND hBtn = GetDlgItem(m_page, chooseBtnId);
        int prevW = Scale(36);
        int btnW = Scale(72);
        if (hLabel) MoveWindow(hLabel, m_padX, m_y + Scale(2), m_labelW, m_rowH, TRUE);
        if (hPreview) MoveWindow(hPreview, m_ctrlX, m_y, prevW, m_rowH, TRUE);
        if (hBtn) MoveWindow(hBtn, m_ctrlX + prevW + Scale(8), m_y, btnW, m_rowH, TRUE);
        m_y += m_rowStep;
    }

    void AddSliderRow(int labelId, int sliderId, int valLabelId, int sliderWidth = 0) {
        HWND hLabel = GetDlgItem(m_page, labelId);
        HWND hSlider = GetDlgItem(m_page, sliderId);
        HWND hVal = GetDlgItem(m_page, valLabelId);
        int vW = Scale(44);
        // sliderWidth == 0 means "stretch": the track fills the column and the value
        // label sits right after it.
        int sW = sliderWidth > 0 ? Scale(sliderWidth) : (m_ctrlW - vW - Scale(8));
        if (sW < Scale(80)) sW = Scale(80);
        if (hLabel) MoveWindow(hLabel, m_padX, m_y + Scale(2), m_labelW, m_rowH, TRUE);
        if (hSlider) MoveWindow(hSlider, m_ctrlX, m_y, sW, m_rowH, TRUE);
        if (hVal) MoveWindow(hVal, m_ctrlX + sW + Scale(8), m_y + Scale(2), vW, m_rowH, TRUE);
        m_y += m_rowStep;
    }

    void AddCheckbox(int checkId) {
        HWND hCheck = GetDlgItem(m_page, checkId);
        if (hCheck) MoveWindow(hCheck, m_padX, m_y, m_clientW - m_padX * 2, m_rowH, TRUE);
        m_y += m_rowStep;
    }

    void AddCheckboxWithHint(int checkId, int hintId, int hintH = 16) {
        HWND hCheck = GetDlgItem(m_page, checkId);
        HWND hHint = GetDlgItem(m_page, hintId);
        if (hCheck) MoveWindow(hCheck, m_padX, m_y, m_clientW - m_padX * 2, m_rowH, TRUE);
        m_y += m_rowH + Scale(3);

        if (hHint) {
            SetPropW(hHint, L"ZenCropHint", reinterpret_cast<HANDLE>(1));
            auto* state = SettingsStateForPage(m_page);
            if (state && state->hHintFont.get()) {
                SendMessageW(hHint, WM_SETFONT, reinterpret_cast<WPARAM>(state->hHintFont.get()), TRUE);
            }
            int h = Scale(hintH);
            int indent = Scale(20);
            MoveWindow(hHint, m_padX + indent, m_y, m_clientW - m_padX * 2 - indent, h, TRUE);
            m_y += h + m_rowGap;
        } else {
            m_y += m_rowGap;
        }
    }

    void AddCheckboxPair(int check1Id, int check2Id) {
        HWND h1 = GetDlgItem(m_page, check1Id);
        HWND h2 = GetDlgItem(m_page, check2Id);
        // Split the full row evenly so long checkbox captions are not clipped.
        const int gap = Scale(12);
        const int total = m_clientW - m_padX * 2;
        int col1W = (total - gap) / 2;
        int col2W = total - gap - col1W;
        if (h1) MoveWindow(h1, m_padX, m_y, col1W, m_rowH, TRUE);
        if (h2) MoveWindow(h2, m_padX + col1W + gap, m_y, col2W, m_rowH, TRUE);
        m_y += m_rowStep;
    }

    int Finish() {
        int totalH = m_y + Scale(18);
        UpdatePageScroll(m_page, totalH);
        return totalH;
    }

private:
    HWND m_page;
    UINT m_dpi;
    int m_clientW;
    int m_padX;
    int m_padY;
    int m_labelW;
    int m_ctrlX;
    int m_ctrlW;
    int m_rowH;
    int m_rowGap;
    int m_rowStep;
    int m_y;
};

} // namespace settings_ui
