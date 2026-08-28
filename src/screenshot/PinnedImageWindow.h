#pragma once

#include <windows.h>
#include <vector>

class PinnedImageWindow {
public:
    PinnedImageWindow(HBITMAP hBitmap, const RECT& sourceRect);
    ~PinnedImageWindow();

    bool IsValid() const { return m_window && IsWindow(m_window); }

private:
    HWND m_window = nullptr;
    HBITMAP m_bitmap = nullptr;
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    std::vector<DWORD> m_sourcePixels;

    static const wchar_t* ClassName;
    static void RegisterWindowClass();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool LoadSourcePixels();
    bool UpdateLayeredSurface();
    LRESULT HitTest(POINT ptScreen) const;
};
