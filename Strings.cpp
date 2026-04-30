#include "Strings.h"
#include "Settings.h"
#include <windows.h>

static bool g_chinese = false;

static bool DetectChineseSystem() {
    LANGID langId = GetUserDefaultUILanguage();
    WORD priLang = (langId & 0xFF);
    return (priLang == LANG_CHINESE);
}

namespace S {

void InitLanguage() {
    GeneralSettings gs = LoadGeneralSettings();
    switch (gs.language.value) {
    case AppLanguage::English:
        g_chinese = false;
        break;
    case AppLanguage::Chinese:
        g_chinese = true;
        break;
    default:
        g_chinese = DetectChineseSystem();
        break;
    }
}

void SetLanguage(bool chinese) {
    g_chinese = chinese;
}

bool IsChinese() {
    return g_chinese;
}

const wchar_t* AppName() { return L"ZenCrop"; }
const wchar_t* TrayTip() { return g_chinese ? L"ZenCrop（右键退出）" : L"ZenCrop (Right click to exit)"; }
const wchar_t* AlreadyRunning() { return g_chinese ? L"ZenCrop 已在运行。" : L"ZenCrop is already running."; }

const wchar_t* MenuShowTitlebar() { return g_chinese ? L"显示标题栏" : L"Show Titlebar"; }
const wchar_t* MenuSettings() { return g_chinese ? L"设置" : L"Settings"; }
const wchar_t* MenuOpenRelease() { return g_chinese ? L"打开发布页" : L"Open Release Page"; }
const wchar_t* MenuExit() { return g_chinese ? L"退出 ZenCrop" : L"Exit ZenCrop"; }

const wchar_t* SettingsTitle() { return g_chinese ? L"设置" : L"Settings"; }
const wchar_t* GeneralCaption() { return g_chinese ? L"常规" : L"General"; }
const wchar_t* ZenCropCaption() { return L"ZenCrop"; }
const wchar_t* AotCaption() { return g_chinese ? L"置顶" : L"Always On Top"; }

const wchar_t* LanguageLabel() { return g_chinese ? L"语言：" : L"Language:"; }
const wchar_t* LangAuto() { return g_chinese ? L"自动（跟随系统）" : L"Auto (Follow System)"; }
const wchar_t* LangEnglish() { return L"English"; }
const wchar_t* LangChinese() { return L"\x4E2D\x6587"; }

const wchar_t* ColorLabel() { return g_chinese ? L"颜色：" : L"Color:"; }
const wchar_t* ChooseButton() { return g_chinese ? L"选择..." : L"Choose..."; }
const wchar_t* ThicknessLabel() { return g_chinese ? L"粗细 (px)：" : L"Thickness (px):"; }
const wchar_t* CropOnTop() { return g_chinese ? L"裁剪窗口置顶" : L"Enable crop on top"; }
const wchar_t* ReparentLabel() { return g_chinese ? L"Reparent：" : L"Reparent:"; }
const wchar_t* ThumbnailLabel() { return g_chinese ? L"Thumbnail：" : L"Thumbnail:"; }
const wchar_t* ViewportLabel() { return g_chinese ? L"Viewport：" : L"Viewport:"; }
const wchar_t* CloseAllLabel() { return g_chinese ? L"全部关闭：" : L"Close All:"; }
const wchar_t* HotkeyLabel() { return g_chinese ? L"快捷键：" : L"Hotkey:"; }

const wchar_t* AotShowBorder() { return g_chinese ? L"在置顶窗口周围显示边框" : L"Show a border around the pinned window"; }
const wchar_t* AotCustomColor() { return g_chinese ? L"自定义颜色" : L"Custom color"; }
const wchar_t* OpacityLabel() { return g_chinese ? L"不透明度 (%)：" : L"Opacity (%):"; }
const wchar_t* AotRounded() { return g_chinese ? L"启用圆角" : L"Enable rounded corners"; }

const wchar_t* HotkeyNone() { return g_chinese ? L"(无)" : L"(None)"; }
const wchar_t* HotkeyPrompt() { return g_chinese ? L"请按下快捷键..." : L"Press shortcut..."; }
const wchar_t* KeySpace() { return g_chinese ? L"空格" : L"Space"; }
const wchar_t* KeyEnter() { return g_chinese ? L"回车" : L"Enter"; }
const wchar_t* KeyBackspace() { return g_chinese ? L"退格" : L"Backspace"; }
const wchar_t* KeyPageUp() { return g_chinese ? L"上页" : L"Page Up"; }
const wchar_t* KeyPageDown() { return g_chinese ? L"下页" : L"Page Down"; }
const wchar_t* KeyLeft() { return g_chinese ? L"左" : L"Left"; }
const wchar_t* KeyUp() { return g_chinese ? L"上" : L"Up"; }
const wchar_t* KeyRight() { return g_chinese ? L"右" : L"Right"; }
const wchar_t* KeyDown() { return g_chinese ? L"下" : L"Down"; }

const wchar_t* HotkeyConflictMsg() { return g_chinese ? L"检测到重复快捷键！部分快捷键可能无法正常工作。" : L"Duplicate hotkeys detected! Some shortcuts may not work correctly."; }
const wchar_t* HotkeyConflictTitle() { return g_chinese ? L"ZenCrop - 快捷键冲突" : L"ZenCrop - Hotkey Conflict"; }

const wchar_t* CropLabelFormat() { return g_chinese ? L"%d, %d \x00B7 %d x %d 像素" : L"%d, %d \x00B7 %d x %d px"; }
const wchar_t* OverlayHoverHint() { return g_chinese ? L"点击选择窗口 \x00B7 拖拽选择区域 \x00B7 ESC 取消" : L"Click to select \x00B7 Drag to select area \x00B7 ESC to cancel"; }
const wchar_t* OverlayAdjustHint() { return g_chinese ? L"双击或按 Enter 确认 \x00B7 ESC 取消 \x00B7 方向键微调" : L"Double-click or Enter to confirm \x00B7 ESC to cancel \x00B7 Arrow keys to adjust"; }

const wchar_t* ThumbnailTitle() { return g_chinese ? L"缩略图" : L"Thumbnail"; }

}
