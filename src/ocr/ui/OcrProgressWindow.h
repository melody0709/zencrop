#pragma once

// OcrProgressWindow：OCR 进行中的轻量进度浮层
//
// 设计目的：在快捷键截图 OCR / 截图工具栏 CopyOcrText 期间显示持续的 liveness 反馈，
// 解决"OCR 期间画面空白，用户以为程序卡住"的问题。
//
// 视觉：全自绘深色浮层，显示 "OCR 中 · 00:12 · {引擎名}" + indeterminate motion bar。
// 复用 OcrResultWindow 的配色（BgColor / TextColor / StatusColor）。
//
// 硬约束：
// - H1：定位用 anchorRect，不依赖 owner（owner 可能是隐藏主窗口或马上销毁的 Overlay）。
// - H2：Show() 返回 progressId，Close(progressId) 只关闭当前任务的浮层，避免并发覆盖。
//       progressId 由全局 NextOcrProgressId() 生成（P1.2 修复），与 OcrDashboardWindow 共享单一 id 空间。
// - H3：全自绘，不用 child static 控件，省掉透明背景和 DPI 布局麻烦。
// - WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE，避免抢焦点。

#include <windows.h>
#include <string>
#include <cstdint>

class OcrProgressWindow {
public:
    static OcrProgressWindow& Instance();

    // 显示进度浮层。返回 progressId（H2 硬约束）。
    // - owner：父窗口（仅用于关联，不用于定位）；可为 nullptr。
    // - anchorRect：浮层定位锚点（H1 硬约束）；nullptr 时 fallback 到屏幕中央。
    // - label：显示的引擎名（H5 硬约束，反映 fallback 后的真实引擎）。
    // - fontSize：字体大小（与 OcrResultWindow 一致，来自 OcrSettings.ocrFontSize）；
    //             默认 -1 表示沿用上次创建的字体（首次默认 18）。
    uint64_t Show(HWND owner, const RECT* anchorRect, const std::wstring& label, int fontSize = -1);

    // 按 progressId 关闭浮层（H2 硬约束）。id 不匹配时忽略，避免并发覆盖。
    void Close(uint64_t progressId);

    bool IsVisible() const;
    bool IsCancelled(uint64_t progressId) const;  // P1: 取消按钮支持

private:
    OcrProgressWindow() = default;
    ~OcrProgressWindow();
    OcrProgressWindow(const OcrProgressWindow&) = delete;
    OcrProgressWindow& operator=(const OcrProgressWindow&) = delete;

    HWND m_hwnd = nullptr;
    HFONT m_hUiFont = nullptr;     // 粗体 UI 字体
    HFONT m_hMonoFont = nullptr;   // 等宽字体（elapsed time）
    int m_fontSize = 18;           // 当前字体大小（与 OcrResultWindow 一致，来自 OcrSettings.ocrFontSize）
    int m_lastFontSize = 0;        // 上次创建字体时的大小，用于检测变化
    DWORD m_startTick = 0;
    std::wstring m_label;
    UINT_PTR m_timerId = 0;
    uint64_t m_currentId = 0;      // 当前浮层对应的 operation token（来自 NextOcrProgressId()）
    uint64_t m_cancelledId = 0;    // P1: 已取消的 progressId

    static const wchar_t* ClassName;
    static const COLORREF BgColor;
    static const COLORREF TextColor;
    static const COLORREF StatusColor;
    static const COLORREF AccentColor;
    static const int WindowW = 280;
    static const int WindowH = 80;
    static const UINT_PTR TimerId = 10042;  // 500ms elapsed 刷新

    static void RegisterWindowClass();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT MessageHandler(HWND, UINT, WPARAM, LPARAM);

    void CreateWindowInternal(HWND owner, const RECT* anchorRect);
    void Reposition(const RECT* anchorRect);
    POINT CalcAnchorPosition(const RECT* anchorRect, int winW, int winH);
    void EnsureFonts();
    void PaintContent(HWND hwnd);
    std::wstring FormatElapsed() const;
};
