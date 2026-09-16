#pragma once

#include <windows.h>

#include <string>

namespace selection {

enum class SelectionToastKind {
    Info,
    Warning,
    Error,
};

// Short-lived, non-activating feedback for selection acquisition and hotkey
// failures. It never displays selected text.
class SelectionTranslationToastWindow {
public:
    SelectionTranslationToastWindow() = default;
    ~SelectionTranslationToastWindow();

    SelectionTranslationToastWindow(const SelectionTranslationToastWindow&) = delete;
    SelectionTranslationToastWindow& operator=(const SelectionTranslationToastWindow&) = delete;

    // `avoidRect` (optional, screen coordinates) is a window the toast must not
    // cover -- the freshly opened result window. The toast then moves to one of
    // its sides and falls back to the work-area corner, so an informational
    // message can never hide the content it belongs to.
    void Show(std::wstring message, POINT anchor,
              SelectionToastKind kind = SelectionToastKind::Warning,
              bool workAreaCorner = false,
              UINT visibleMilliseconds = 0,
              const RECT* avoidRect = nullptr);
    void Hide();

private:
    static constexpr UINT_PTR kTimerId = 0x534C;
    static constexpr UINT kVisibleMilliseconds = 4200;

    HWND window_ = nullptr;
    std::wstring message_;
    POINT anchor_ = {};
    SelectionToastKind kind_ = SelectionToastKind::Warning;
    bool workAreaCorner_ = false;
    RECT avoidRect_ = {};
    bool hasAvoidRect_ = false;

    static const wchar_t* ClassName();
    static void RegisterWindowClass();
    static LRESULT CALLBACK WindowProc(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    bool EnsureWindow();
    void PositionAndShow();
    void Paint();
};

} // namespace selection
