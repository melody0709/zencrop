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

    void Show(std::wstring message, POINT anchor,
              SelectionToastKind kind = SelectionToastKind::Warning,
              bool workAreaCorner = false,
              UINT visibleMilliseconds = 0);
    void Hide();

private:
    static constexpr UINT_PTR kTimerId = 0x534C;
    static constexpr UINT kVisibleMilliseconds = 4200;

    HWND window_ = nullptr;
    std::wstring message_;
    POINT anchor_ = {};
    SelectionToastKind kind_ = SelectionToastKind::Warning;
    bool workAreaCorner_ = false;

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
