#pragma once
#include <windows.h>

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

AotSettings LoadAotSettings();
void SaveAotSettings(const AotSettings& settings);
OverlaySettings LoadOverlaySettings();
void SaveOverlaySettings(const OverlaySettings& settings);
COLORREF GetSystemAccentColor();
void ShowSettingsDialog(HWND parent);
