#pragma once
#include <windows.h>
#include <string>

class OcrResultWindow {
public:
    OcrResultWindow(const std::wstring& text, RECT cropRect,
                    bool autoCopied = false, bool showTitlebar = false,
                    int fontSize = 14, DWORD elapsedMs = 0,
                    bool resultOnTop = false);
    ~OcrResultWindow();

    bool IsValid() const { return m_hwnd != nullptr; }
    HWND GetHwnd() const { return m_hwnd; }

private:
    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    HWND m_copyBtn = nullptr;
    HWND m_closeBtn = nullptr;
    HWND m_statusText = nullptr;
    HWND m_hintText = nullptr;
    HWND m_elapsedText = nullptr;
    std::wstring m_text;
    bool m_showTitlebar;
    bool m_autoCopied;
    bool m_copied = false;
    int m_fontSize;
    DWORD m_elapsedMs;
    bool m_resultOnTop;
    HFONT m_hUiFont = nullptr;
    WNDPROC m_editOrigProc = nullptr;

    static const int BorderWidth = 1;
    static const COLORREF BorderColor;
    static const COLORREF BgColor;
    static const COLORREF TextColor;
    static const COLORREF BtnNormalBg;
    static const COLORREF BtnHoverBg;
    static const COLORREF BtnPressedBg;
    static const COLORREF BtnTextColor;
    static const COLORREF StatusColor;

    static const int ID_COPY = 1001;
    static const int ID_CLOSE = 1002;

    void CopyToClipboard();
    void LayoutControls();
    POINT CalcWindowPosition(RECT cropRect, int winW, int winH);
    void PaintBorder(HWND hwnd);
    static LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM);

    static const wchar_t* ClassName;
    static void RegisterWindowClass();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT MessageHandler(HWND, UINT, WPARAM, LPARAM);
};
