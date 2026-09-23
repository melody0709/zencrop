#pragma once

// 跨模块自定义消息与主窗口句柄访问
//
// 设计目的：让 ScreenshotSession / ScreenshotEditorWindow 等模块能够向主窗口
// 投递 OCR 相关完成消息，而不需要 extern 全局变量或 #include "main.cpp"。
//
// 实现位置：GetAppMainHwnd / SetAppMainHwnd / NextOcrProgressId 的实现放 main.cpp，不新增 .cpp。

#include <windows.h>
#include <cstdint>
#include <string>

// 自定义应用级消息（WM_APP 范围，避免与 WM_USER 控件子类消息冲突）
inline constexpr UINT WM_APP_OCR_RESULT          = WM_APP + 1; // 热键 OCR 完整路径（结果窗 / 历史）
inline constexpr UINT WM_APP_SCREENSHOT_OCR_DONE = WM_APP + 2; // 静默 Copy OCR 完成（截图工具栏 / OCR 模式 Shift+C）
inline constexpr UINT WM_APP_OVERLAY_RESET       = WM_APP + 3; // 延迟销毁 overlay（避免在 MessageHandler 栈上 reset 自身）
inline constexpr UINT WM_APP_SCREENSHOT_TRANSLATION_OCR_DONE = WM_APP + 4;
inline constexpr UINT WM_APP_SCREENSHOT_TRANSLATION_DONE     = WM_APP + 5;
inline constexpr UINT WM_APP_DASHBOARD_TRANSLATION_DONE      = WM_APP + 6;
// wParam: SelectionTranslationController acquisition generation.
// lParam: selection::SelectionAcquisitionResult*, controller deletes exactly once.
inline constexpr UINT WM_APP_SELECTION_TEXT_ACQUIRED         = WM_APP + 7;
// wParam: TranslationCoordinator workflow generation (independent of the
// acquisition generation). lParam: translation::TranslationResult*.
inline constexpr UINT WM_APP_SELECTION_TRANSLATION_DONE      = WM_APP + 8;

// wParam：OcrProgressWindow::Show() / OcrDashboardWindow::ShowExternalOcrProgress() 返回的 progressId（H2 硬约束）
// lParam：OcrOutput* heap 拷贝，接收方负责 delete

// 主窗口句柄访问（避免 extern 全局变量）
// 实现放 main.cpp：静态变量 + 这两个函数。
HWND GetAppMainHwnd();
void SetAppMainHwnd(HWND hwnd);

// 全局 OCR progressId 生成器（H2 硬约束 + P1.2 修复）
// 单一全局 id 空间，避免 OcrProgressWindow 和 OcrDashboardWindow 各自独立计数导致 id 撞车。
// 实现放 main.cpp，原子递增，全进程单调递增。
uint64_t NextOcrProgressId();

// Stage3 3-F: composition-root OCR progress facade.
// ScreenshotSession / other features must not include OcrDashboardWindow / OcrProgressWindow.
// Implementation lives in main.cpp (composition root; may include OCR UI headers).
// Returns progressId (0 if no UI shown / skipped).
uint64_t ShowAppOcrProgress(
    const std::wstring& engineLabel,
    const RECT* anchorRect,
    int ocrFontSize,
    const std::wstring& imagePath = L"",
    bool allowProgressUi = true);
void CloseAppOcrProgress(uint64_t progressId);
