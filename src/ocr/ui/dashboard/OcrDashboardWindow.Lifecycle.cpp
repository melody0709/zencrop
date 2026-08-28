#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardOleDropTarget.h"
#include "translation/TranslationCoordinator.h"
#include "AppMessages.h"
#include "Settings.h"
#include "Strings.h"
#include "core/WideStringUtils.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windows.h>
#include <windowsx.h>

// D-I-3: real TU (was Lifecycle.inl).

OcrDashboardWindow::OcrDashboardWindow() {
    // D-I-3: canvas view/hover Window aliases deleted — sole store is DashboardState.
    // D-C-9: m_historyItems alias removed — items sole store is m_history.model.items.
    // D-F-3/4: PDF tree keys + batch selection Window aliases removed — DashboardState sole store.
    m_asyncDispatchState = std::make_shared<DashboardAsyncDispatchState>();
    RegisterWindowClasses();
}

OcrDashboardWindow::~OcrDashboardWindow() {
    if (m_dashboardTranslation) {
        m_dashboardTranslation->Shutdown();
        m_dashboardTranslation.reset();
    }
    RevokeOleDropTargets();
    if (m_previewHost) {
        m_previewHost->Destroy();
        m_previewHost.reset();
    }
    if (m_translationPreviewHost) {
        m_translationPreviewHost->Destroy();
        m_translationPreviewHost.reset();
    }
    ReleaseSourceRailBackbuffer();
    if (m_hUiFont) {
        DeleteObject(m_hUiFont);
    }
    if (m_hSourceTitleFont) {
        DeleteObject(m_hSourceTitleFont);
    }
    if (m_hSourceMetaFont) {
        DeleteObject(m_hSourceMetaFont);
    }
    if (m_hEditFont) {
        DeleteObject(m_hEditFont);
    }
    if (m_gdiplusImage || m_gdiplusImageFull) {
        ReleaseGdiplusImages();
    }
    s_instance = nullptr;
}

void OcrDashboardWindow::RegisterWindowClasses() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(Theme::bgPrimary);
    wcex.lpszClassName = ClassName;
    wcex.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    RegisterClassExW(&wcex);

    wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = ImageAreaWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_HAND);
    wcex.hbrBackground = CreateSolidBrush(Theme::bgSecondary);
    wcex.lpszClassName = ImageAreaClassName;
    RegisterClassExW(&wcex);

    wcex = { sizeof(wcex) };
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = SourceRailWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = SourceRailClassName;
    RegisterClassExW(&wcex);

    wcex = { sizeof(wcex) };
    wcex.style = 0;
    wcex.lpfnWndProc = SplitterTrackerWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_SIZEWE);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = SplitterTrackerClassName;
    RegisterClassExW(&wcex);

    wcex = { sizeof(wcex) };
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = SplitterHitWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_SIZEWE);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = SplitterHitClassName;
    RegisterClassExW(&wcex);

    registered = true;
}

bool OcrDashboardWindow::Create(HWND parent) {
    // D-E-1: ocrGeneration sole on DashboardState.
    const uint64_t nextGeneration = DashboardNextHostGeneration();
    DashboardStateSetOcrGeneration(m_dashboardState, nextGeneration);
    m_asyncDispatchState->generation.store(nextGeneration);

    // Center on current screen
    POINT pt = { 0, 0 };
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UpdateDpi(DashboardGetMonitorEffectiveDpi(hMon));

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);
    SIZE initialSize = FitWindowSizeToWorkArea(m_metrics.windowW, m_metrics.windowH, mi.rcWork);
    int winW = initialSize.cx;
    int winH = initialSize.cy;
    // D-D-5/6: legacy Window splitter fields removed — seed pure state only.
    const int initialSplitterX = winW * 55 / 100;
    DashboardStateSyncSplitterGeometry(
        m_dashboardState,
        0,
        0,
        initialSplitterX,
        0.55);
    m_layout.sourceWidth = m_metrics.sourceW;
    m_layout.resultWidth = m_metrics.resultW;
    m_layout.translationWidth = m_metrics.resultW;

    int startX = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - winW) / 2;
    int startY = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - winH) / 2;

    // D-D-1: showTitlebar sole authority is DashboardState.
    GeneralSettings genSettings = LoadGeneralSettings();
    DashboardStateSetShowTitlebar(m_dashboardState, genSettings.showTitlebar);

    // Create window - frameless by default
    // A frameless WS_POPUP needs a system menu and minimize box for Windows to
    // honor the standard taskbar click-to-minimize behavior.
    DWORD dwStyle = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU |
        WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (DashboardStateShowTitlebar(m_dashboardState)) {
        dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    }
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        ClassName,
        S::IsChinese() ? L"ZenCrop OCR 工作台" : L"ZenCrop OCR Dashboard",
        dwStyle,
        startX, startY, winW, winH,
        parent && GetAncestor(parent, GA_PARENT) != HWND_MESSAGE ? parent : nullptr,
        nullptr, GetModuleHandleW(nullptr), this
    );

    if (!m_hwnd) return false;
    {
        std::lock_guard<std::mutex> lock(m_asyncDispatchState->mutex);
        m_asyncDispatchState->hwnd = m_hwnd;
        m_asyncDispatchState->accepting = true;
        m_asyncDispatchState->generation.store(DashboardStateOcrGeneration(m_dashboardState));
    }

    // Immersive Dark Mode
    BOOL darkValue = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkValue, sizeof(darkValue));

    // Windows 11 rounded corners (DWMWA_WINDOW_CORNER_PREFERENCE = 33)
    // Values: 0 = Default, 1 = Don't round, 2 = Round
    DWORD cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hwnd, 33, &cornerPreference, sizeof(cornerPreference));

    // Create ImageArea child
    m_imageArea = CreateWindowExW(
        0, ImageAreaClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, DashboardStateSplitterX(m_dashboardState), winH,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), this
    );

    // Create Edit control (modern flat style)
    m_edit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_READONLY,
        0, 0, 0, 0,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_edit, WM_SETFONT, (WPARAM)m_hEditFont, TRUE);
    // Set modern margins for Edit control (left padding)
    SendMessage(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(m_metrics.editMarginLeft, m_metrics.editMarginRight));

    // Subclass Edit control
    m_editOrigProc = (WNDPROC)SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    SetWindowLongPtrW(m_edit, GWLP_USERDATA, (LONG_PTR)m_editOrigProc);

    // Modern scrollbar style (dark mode)
    SetWindowTheme(m_edit, L"DarkMode_Explorer", nullptr);

    m_searchEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_SEARCH, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_searchEdit, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    SendMessage(m_searchEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(m_metrics.searchMarginX, m_metrics.searchMarginX));
    SendMessageW(m_searchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)(S::IsChinese() ? L"搜索来源..." : L"Search sources..."));
    SetWindowTheme(m_searchEdit, L"DarkMode_Explorer", nullptr);
    m_searchOrigProc = (WNDPROC)SetWindowLongPtrW(m_searchEdit, GWLP_WNDPROC, (LONG_PTR)SearchSubclassProc);
    SetWindowLongPtrW(m_searchEdit, GWLP_USERDATA, (LONG_PTR)m_searchOrigProc);

    // SS_ENDELLIPSIS keeps long activity text from spilling; width is constrained
    // in LayoutControls so this control never covers the Sort button.
    m_sourceHeaderText = CreateWindowExW(
        0, L"STATIC", L"Sources",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX,
        0, 0, 0, 0,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    SendMessageW(m_sourceHeaderText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_sourceSortBtn = CreateWindowExW(
        0, L"BUTTON", L"Sort v",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_SOURCE_SORT, GetModuleHandleW(nullptr), nullptr
    );
    SendMessageW(m_sourceSortBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_sourceList = CreateWindowExW(
        0, SourceRailClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_SOURCE_LIST, GetModuleHandleW(nullptr), this
    );
    SetWindowTheme(m_sourceList, L"DarkMode_Explorer", nullptr);

    m_splitterTracker = CreateWindowExW(
        0,
        SplitterTrackerClassName, L"",
        WS_CHILD,
        0, 0, 0, 0,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    for (HWND& hitTarget : m_splitterHitTargets) {
        hitTarget = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            SplitterHitClassName, L"",
            WS_CHILD,
            0, 0, 0, 0,
            m_hwnd, nullptr, GetModuleHandleW(nullptr), this
        );
        if (hitTarget) DragAcceptFiles(hitTarget, TRUE);
    }

    // Create Buttons
    m_sourcePanelToggleBtn = CreateWindowExW(
        0, L"BUTTON", L"Source panel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_SOURCE_PANEL_TOGGLE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_sourcePanelToggleBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_importBtn = CreateWindowExW(
        0, L"BUTTON", L"Import",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_IMPORT, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_importBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_outputFolderBtn = CreateWindowExW(
        0, L"BUTTON", L"Output: Compact",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_OUTPUT_FOLDER, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_outputFolderBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_dashboardOcrCombo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_OCR_MODE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_dashboardOcrCombo, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    SetWindowTheme(m_dashboardOcrCombo, L"DarkMode_CFD", nullptr);
    PopulateDashboardOcrModeCombo();

    m_copyBtn = CreateWindowExW(
        0, L"BUTTON", L"Copy",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_COPY, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_copyBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_clearBtn = CreateWindowExW(
        0, L"BUTTON", S::IsChinese() ? L"清理已结束" : L"Clear Finished",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_CLEAR, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_clearBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_retryFailedBtn = CreateWindowExW(
        0, L"BUTTON", L"Retry Failed",
        WS_CHILD | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_RETRY_FAILED, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_retryFailedBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    EnableWindow(m_retryFailedBtn, FALSE);

    m_pauseBatchBtn = CreateWindowExW(
        0, L"BUTTON", L"Pause",
        WS_CHILD | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_PAUSE_BATCH, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_pauseBatchBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_openOutputBtn = CreateWindowExW(
        0, L"BUTTON", L"Open Output",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_OPEN_OUTPUT, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_openOutputBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    EnableWindow(m_openOutputBtn, FALSE);

    m_minimizeBtn = CreateWindowExW(
        0, L"BUTTON", S::IsChinese() ? L"最小化" : L"Minimize",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_MINIMIZE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_minimizeBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_maximizeBtn = CreateWindowExW(
        0, L"BUTTON", S::IsChinese() ? L"最大化" : L"Maximize",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_MAXIMIZE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_maximizeBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_closeBtn = CreateWindowExW(
        0, L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_CLOSE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_closeBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    // P1.5: 语言切换按钮（中/EN toggle），位于 Close 左侧。
    m_langToggleBtn = CreateWindowExW(
        0, L"BUTTON", L"EN",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_LANG_TOGGLE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_langToggleBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_resultPanelToggleBtn = CreateWindowExW(
        0, L"BUTTON", L"Result panel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_RESULT_PANEL_TOGGLE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_resultPanelToggleBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_previewBtn = CreateWindowExW(
        0, L"BUTTON", L"Preview",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_PREVIEW, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_previewBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_sourceBtn = CreateWindowExW(
        0, L"BUTTON", L"Source",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_SOURCE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_sourceBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_textBtn = CreateWindowExW(
        0, L"BUTTON", L"Text",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_TEXT, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_textBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_jsonBtn = CreateWindowExW(
        0, L"BUTTON", L"JSON",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_JSON, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_jsonBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_translateBtn = CreateWindowExW(
        0, L"BUTTON", L"Translate",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_TRANSLATE, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_translateBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_translateAgainBtn = CreateWindowExW(
        0, L"BUTTON", L"Translate again",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_TRANSLATE_AGAIN, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_translateAgainBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_prevRecordBtn = CreateWindowExW(
        0, L"BUTTON", L"<",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_PREV_RECORD, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_prevRecordBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_nextRecordBtn = CreateWindowExW(
        0, L"BUTTON", L">",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        m_hwnd, (HMENU)ID_DASH_NEXT_RECORD, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_nextRecordBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_recordPosText = CreateWindowExW(
        0, L"STATIC", L"0/0",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 0, 0,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_recordPosText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    m_statusText = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    SendMessage(m_statusText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);

    // Tooltip 控件：为命令栏 owner-draw 按钮提供 hover 提示（含快捷键）
    // TTF_SUBCLASS 让 tooltip 自动子类化目标 HWND 并跟踪其矩形，无需手动 TTM_NEWTOOLRECT。
    m_tooltipHwnd = CreateWindowExW(
        0, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    if (m_tooltipHwnd) {
        SendMessageW(m_tooltipHwnd, TTM_SETMAXTIPWIDTH, 0, 400);
        SendMessageW(m_tooltipHwnd, TTM_SETDELAYTIME, TTDT_INITIAL, 300);
        SendMessageW(m_tooltipHwnd, TTM_SETDELAYTIME, TTDT_AUTOPOP, 8000);
        struct BtnTip { HWND btn; const wchar_t* zh; const wchar_t* en; };
        const BtnTip tips[] = {
            { m_sourceSortBtn,        L"按添加日期排序",                              L"Sort by date added" },
            { m_sourcePanelToggleBtn, L"显示或隐藏来源面板",                    L"Show or hide source panel" },
            { m_resultPanelToggleBtn, L"显示或隐藏结果面板",                    L"Show or hide result panel" },
            { m_importBtn,       L"导入图片 (Ctrl+O)",                      L"Import images (Ctrl+O)" },
            { m_outputFolderBtn, L"设置输出目录",                            L"Set output folder" },
            { m_copyBtn,         L"复制文本 (Ctrl+C)",                       L"Copy text (Ctrl+C)" },
            { m_clearBtn,        L"清理已完成、失败或取消的来源",              L"Clear completed, failed, or canceled Sources" },
            { m_retryFailedBtn,  L"重试失败任务",                            L"Retry failed tasks" },
            { m_pauseBatchBtn,   L"暂停 OCR 队列（当前识别/渲染/云端任务仍会继续）", L"Pause OCR queue (current OCR/render/Cloud keep running)" },
            { m_openOutputBtn,   L"打开输出目录",                            L"Open output folder" },
            { m_closeBtn,        L"关闭 (Esc)",                              L"Close (Esc)" },
            { m_langToggleBtn,   L"切换语言（中/英）",                       L"Toggle language (ZH/EN)" },
            { m_previewBtn,      L"预览模式（Markdown 渲染）",               L"Preview mode (Markdown render)" },
            { m_sourceBtn,       L"来源模式（原始文本）",                    L"Source mode (raw text)" },
            { m_textBtn,         L"文本模式（纯文本）",                      L"Text mode (plain text)" },
            { m_jsonBtn,         L"JSON 模式（结构化数据）",                 L"JSON mode (structured)" },
            { m_translateBtn,    L"翻译当前 OCR 结果",                       L"Translate the current OCR result" },
            { m_translateAgainBtn, L"跳过缓存并重新翻译",                    L"Ignore cache and translate again" },
            { m_prevRecordBtn,   L"上一条记录",                              L"Previous record" },
            { m_nextRecordBtn,   L"下一条记录",                              L"Next record" },
        };
        bool zh = S::IsChinese();
        for (const auto& t : tips) {
            if (!t.btn) continue;
            TOOLINFOW ti = { sizeof(ti) };
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = m_hwnd;
            ti.uId = (UINT_PTR)t.btn;
            ti.lpszText = (LPWSTR)(zh ? t.zh : t.en);
            SendMessageW(m_tooltipHwnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
        const struct { HWND btn; const wchar_t* zh; const wchar_t* en; } windowControlTips[] = {
            { m_minimizeBtn, L"最小化窗口", L"Minimize window" },
            { m_maximizeBtn, L"最大化/还原窗口", L"Maximize/restore window" },
        };
        for (const auto& t : windowControlTips) {
            if (!t.btn) continue;
            TOOLINFOW ti = { sizeof(ti) };
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = m_hwnd;
            ti.uId = (UINT_PTR)t.btn;
            ti.lpszText = const_cast<LPWSTR>(zh ? t.zh : t.en);
            SendMessageW(m_tooltipHwnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
    }

    // Accept files on every major pane so drag/drop works wherever the user aims.
    DragAcceptFiles(m_hwnd, TRUE);
    DragAcceptFiles(m_imageArea, TRUE);
    DragAcceptFiles(m_edit, TRUE);
    DragAcceptFiles(m_searchEdit, TRUE);
    DragAcceptFiles(m_sourceList, TRUE);
    RegisterOleDropTargets();

    // P1.5: 按当前语言设置命令栏按钮文案（创建时默认英文，这里同步为实际语言）
    RefreshAllTexts();

    // Load history files
    LoadHistory();

    // Restore window position before showing
    bool restoredMaximized = RestoreWindowPosition();
    LoadBatchSessionState();
    // Output manifests intentionally survive Source removal. Load the durable
    // Dashboard deny-list before any automatic snapshot discovery so deleted
    // Sources are not reconstructed from those retained files.
    DashboardHistorySessionLoadDismissed(m_history, m_dashboardState);
    // The Output control includes the restored profile name, so refresh it
    // after the session defaults have been read.
    RefreshAllTexts();

    // Auto layout - only set default splitter if not restored from saved position
    RECT clientRc;
    GetClientRect(m_hwnd, &clientRc);
    // D-D-5/6: clamp pure splitterX; Window fields removed.
    int splitterX = DashboardStateSplitterX(m_dashboardState);
    if (splitterX <= m_metrics.minLeftW / 2 ||
        splitterX >= clientRc.right - m_metrics.minRightW / 2) {
        splitterX = clientRc.right * 55 / 100; // Default: left pane 55%
        DashboardStateSyncSplitterGeometry(
            m_dashboardState,
            DashboardStateSourceSplitterX(m_dashboardState),
            DashboardStateResultSplitterX(m_dashboardState),
            splitterX,
            DashboardStateSplitterRatio(m_dashboardState));
    }
    LayoutControls();
    UpdatePreviewControls();
    AutoResumeLastBatchOutputRoot();

    // Durable History stores only a manifest link. Add every still-present
    // linked Output root so old records remain readable beyond the small
    // recent-root session list.
    std::vector<std::wstring> durableRoots;
    for (const auto& item : m_history.model.items) {
        if (item.recordKind != L"DurableOutputLink" || item.originManifestPath.empty()) continue;
        // OWN-96: pure double-parent from manifest path (WideStringUtils).
        const std::wstring outputDir = WideParentDirFromPath(item.originManifestPath);
        const std::wstring outputRoot = WideParentDirFromPath(outputDir);
        if (!outputDir.empty() && DashboardDirectoryExistsWide(outputRoot) &&
            std::find_if(durableRoots.begin(), durableRoots.end(),
                [&](const std::wstring& existing) {
                    return DashboardNormalizePathForCompare(existing) == DashboardNormalizePathForCompare(outputRoot);
                }) == durableRoots.end()) {
            durableRoots.push_back(outputRoot);
        }
    }
    bool appendedDurableRoot = !m_batch.batchTasks.empty() || !m_batch.activePdfJobs.empty();
    for (const auto& root : durableRoots) {
        // The recent-session restore may already have loaded this root.
        // Durable History is a fallback for roots outside that small list.
        if (IsBatchOutputRootInUse(root)) continue;
        if (LoadBatchOutputSnapshot(root, false, false, appendedDurableRoot, false)) {
            appendedDurableRoot = true;
        }
    }

    // Show window
    ShowWindow(m_hwnd, restoredMaximized ? SW_MAXIMIZE : SW_SHOWNORMAL);
    UpdateWindow(m_hwnd);

    // Select only a standalone History row. DurableOutputLink records already
    // represented by restored tasks must not reactivate their hidden
    // source.png backing after the Source projection has been rebuilt.
    if (DashboardStateHasVisibleHistory(m_dashboardState)) {
        SelectHistoryItem(DashboardStateLastVisibleHistoryIndex(m_dashboardState));
        int textLen = GetWindowTextLengthW(m_edit);
        SendMessage(m_edit, EM_SETSEL, textLen, textLen);
        SendMessage(m_edit, EM_SCROLLCARET, 0, 0);
    } else if (!m_batch.activePdfJobs.empty()) {
        ActivateSourceRailPdfItem(
            static_cast<int>(m_batch.activePdfJobs.size()) - 1, 0, true);
    } else if (!m_batch.batchTasks.empty()) {
        ActivateSourceRailImageTask(static_cast<int>(m_batch.batchTasks.size()) - 1);
    }
    // Apply restored/default preferred Result Inspector mode after selection is
    // ready. Missing ini key restores as Preview. Host unavailable falls back
    // only the effective mode; preferred + ini stay Preview.
    SetTextMode(DashboardStateTextModePreferred(m_dashboardState));

    return true;
}
