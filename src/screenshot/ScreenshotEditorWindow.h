#pragma once

#include "OcrEngine.h"
#include <windows.h>
#include <functional>
#include <memory>

class ScreenshotEditorWindow {
public:
    using PinCallback = std::function<void(HBITMAP, RECT)>;

    ScreenshotEditorWindow(HBITMAP hBitmap, const RECT& sourceRect, PinCallback onPin);
    ~ScreenshotEditorWindow();

    bool IsValid() const { return m_window && IsWindow(m_window); }

private:
    HWND m_window = nullptr;
    HBITMAP m_bitmap = nullptr;
    RECT m_sourceRect = {};
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    bool m_ocrInFlight = false;
    DWORD m_ocrStartTick = 0;  // H7：编辑器 OCR 按钮 elapsed 计时起点
    std::shared_ptr<IOcrEngine> m_ocrEngine;
    PinCallback m_onPin;

    HWND m_btnCopy = nullptr;
    HWND m_btnSave = nullptr;
    HWND m_btnQuickSave = nullptr;
    HWND m_btnPin = nullptr;
    HWND m_btnOcr = nullptr;
    HWND m_btnClose = nullptr;

    static const wchar_t* ClassName;
    static void RegisterWindowClass();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void LayoutControls();
    void Paint(HDC hdc);
    void CopyImage();
    void SaveImageAs();
    void QuickSaveImage();
    void PinImage();
    void StartCopyOcrText();
    void CompleteCopyOcrText(OcrOutput* result);

    // H7 硬约束：编辑器 OCR 按钮 elapsed 刷新 timer ID
    static constexpr UINT_PTR TIMER_OCR_TICK = 1001;
};
