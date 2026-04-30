#pragma once
#include <windows.h>
#include <string>

#define IDD_AOT_SETTINGS        2010
#define IDC_AOT_SHOW_BORDER     2001
#define IDC_AOT_COLOR_MODE      2002
#define IDC_AOT_COLOR_PREVIEW   2003
#define IDC_AOT_CHOOSE_COLOR    2004
#define IDC_AOT_OPACITY_SLIDER  2005
#define IDC_AOT_OPACITY_LABEL   2006
#define IDC_AOT_THICK_SLIDER    2007
#define IDC_AOT_THICK_LABEL     2008
#define IDC_AOT_ROUNDED         2009

#define IDD_SETTINGS            2020
#define IDD_SETTINGS_ZENCROP    2021
#define IDD_SETTINGS_AOT        2022
#define IDC_SETTINGS_TAB        2030

#define IDC_ZC_COLOR_PREVIEW    2031
#define IDC_ZC_CHOOSE_COLOR     2032
#define IDC_ZC_THICK_SLIDER     2033
#define IDC_ZC_THICK_LABEL      2034
#define IDC_ZC_CROP_ON_TOP      2035

#define IDC_HK_REPARENT_EDIT    2040
#define IDC_HK_REPARENT_CLEAR   2041
#define IDC_HK_THUMBNAIL_EDIT   2042
#define IDC_HK_THUMBNAIL_CLEAR  2043
#define IDC_HK_VIEWPORT_EDIT    2048
#define IDC_HK_VIEWPORT_CLEAR   2049
#define IDC_HK_CLOSE_EDIT       2044
#define IDC_HK_CLOSE_CLEAR      2045
#define IDC_HK_AOT_EDIT         2046
#define IDC_HK_AOT_CLEAR        2047

#define IDD_SETTINGS_GENERAL   2023
#define IDC_GEN_LANGUAGE       2050

#define IDC_ZC_COLOR_LABEL     2060
#define IDC_ZC_THICK_LABEL2    2061
#define IDC_ZC_REPARENT_LABEL  2062
#define IDC_ZC_THUMBNAIL_LABEL 2063
#define IDC_ZC_VIEWPORT_LABEL  2064
#define IDC_ZC_CLOSE_LABEL     2065

#define IDC_AOT_COLOR_LABEL    2070
#define IDC_AOT_OPACITY_LABEL2 2071
#define IDC_AOT_THICK_LABEL2   2072
#define IDC_AOT_HOTKEY_LABEL   2073

struct AppLanguage {
    enum Value { Auto, English, Chinese };
    Value value = Auto;
};

struct GeneralSettings {
    AppLanguage language;
};

struct AotSettings {
    bool showBorder = true;
    bool customColor = true;
    COLORREF color = RGB(0, 120, 215);
    int opacity = 100;
    int thickness = 6;
    bool roundedCorners = true;
};

struct OverlaySettings {
    COLORREF color = RGB(255, 0, 0);
    int thickness = 3;
    bool cropOnTop = false;
};

struct HotkeyConfig {
    bool win = false;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    unsigned char key = 0;

    bool IsEmpty() const { return key == 0; }
    UINT Modifiers() const {
        UINT mod = 0;
        if (win) mod |= MOD_WIN;
        if (ctrl) mod |= MOD_CONTROL;
        if (shift) mod |= MOD_SHIFT;
        if (alt) mod |= MOD_ALT;
        mod |= MOD_NOREPEAT;
        return mod;
    }
    std::wstring ToString() const;
};

struct HotkeySettings {
    HotkeyConfig reparent;
    HotkeyConfig thumbnail;
    HotkeyConfig viewport;
    HotkeyConfig closeReparent;
    HotkeyConfig alwaysOnTop;
};

GeneralSettings LoadGeneralSettings();
void SaveGeneralSettings(const GeneralSettings& settings);
AotSettings LoadAotSettings();
void SaveAotSettings(const AotSettings& settings);
OverlaySettings LoadOverlaySettings();
void SaveOverlaySettings(const OverlaySettings& settings);
HotkeySettings LoadHotkeySettings();
void SaveHotkeySettings(const HotkeySettings& settings);
COLORREF GetSystemAccentColor();
void ShowSettingsDialog(HWND parent);
