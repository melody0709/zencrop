#pragma once

#include <windows.h>
#include <string>

// Non-modal confirmation for screenshot OCR text copied to the clipboard (Shift+C).
// Uses short success timing and a non-activating popup window.
// Now also used by OCR-mode Shift+C (no result window / history).
class OcrCopyToastWindow {
public:
    static OcrCopyToastWindow& Instance();

    static constexpr UINT AutoCloseMs = 3000;
    static constexpr UINT FadeDurationMs = 300;

    void Show();

private:
    enum class Phase { Hidden, FadeIn, Visible, FadeOut };

    OcrCopyToastWindow() = default;
    ~OcrCopyToastWindow();
    OcrCopyToastWindow(const OcrCopyToastWindow&) = delete;
    OcrCopyToastWindow& operator=(const OcrCopyToastWindow&) = delete;

    HWND m_hwnd = nullptr;
    std::wstring m_text;
    RECT m_closeRect = {};
    DWORD m_phaseStartTick = 0;
    DWORD m_shownTick = 0;
    UINT_PTR m_timerId = 0;
    Phase m_phase = Phase::Hidden;
    bool m_closeHovered = false;

    static const wchar_t* ClassName;
    static constexpr UINT_PTR TimerId = 10043;

    static void RegisterWindowClass();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateWindowInternal();
    void PositionAndSize();
    void Paint(HWND hwnd);
    void ApplyOpacity(BYTE alpha) const;
    void BeginFadeOut();
};
