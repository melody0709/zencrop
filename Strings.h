#pragma once
#include <windows.h>

namespace S {

void InitLanguage();
void SetLanguage(bool chinese);
bool IsChinese();

const wchar_t* AppName();
const wchar_t* TrayTip();
const wchar_t* AlreadyRunning();

const wchar_t* MenuShowTitlebar();
const wchar_t* MenuSettings();
const wchar_t* MenuOpenRelease();
const wchar_t* MenuExit();

const wchar_t* SettingsTitle();
const wchar_t* GeneralCaption();
const wchar_t* ZenCropCaption();
const wchar_t* AotCaption();

const wchar_t* LanguageLabel();
const wchar_t* LangAuto();
const wchar_t* LangEnglish();
const wchar_t* LangChinese();

const wchar_t* ColorLabel();
const wchar_t* ChooseButton();
const wchar_t* ThicknessLabel();
const wchar_t* CropOnTop();
const wchar_t* ReparentLabel();
const wchar_t* ThumbnailLabel();
const wchar_t* ViewportLabel();
const wchar_t* CloseAllLabel();
const wchar_t* HotkeyLabel();

const wchar_t* AotShowBorder();
const wchar_t* AotCustomColor();
const wchar_t* OpacityLabel();
const wchar_t* AotRounded();

const wchar_t* HotkeyNone();
const wchar_t* HotkeyPrompt();
const wchar_t* KeySpace();
const wchar_t* KeyEnter();
const wchar_t* KeyBackspace();
const wchar_t* KeyPageUp();
const wchar_t* KeyPageDown();
const wchar_t* KeyLeft();
const wchar_t* KeyUp();
const wchar_t* KeyRight();
const wchar_t* KeyDown();

const wchar_t* HotkeyConflictMsg();
const wchar_t* HotkeyConflictTitle();

const wchar_t* CropLabelFormat();
const wchar_t* OverlayHoverHint();
const wchar_t* OverlayAdjustHint();

const wchar_t* ThumbnailTitle();

}
