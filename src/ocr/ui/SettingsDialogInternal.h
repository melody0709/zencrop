#pragma once

#include "core/GdiHandles.h"
#include "core/Settings.h"
#include "core/SettingsHotkeyDraft.h"

#include <windows.h>

namespace settings_ui {

inline constexpr wchar_t kSettingsHotkeyDraftProperty[] = L"ZenCrop.SettingsHotkeyDraft";
inline constexpr wchar_t kSettingsOcrPendingProp[] = L"ZenCrop.SettingsOcrPending";
inline constexpr wchar_t kSettingsWindowClassName[] = L"ZenCropSettingsWindowClass";
inline constexpr wchar_t kSettingsStateProp[] = L"ZenCrop.SettingsState";

struct SettingsState {
    SettingsDraft draft;
    SettingsHotkeyDraft hotkeyDraft;
    HWND hwnd = nullptr;
    HWND hTab = nullptr;
    HWND m_pages[6] = {};
    HWND hStatus = nullptr;
    HWND hApplyBtn = nullptr;
    HWND hOkBtn = nullptr;
    HWND hCancelBtn = nullptr;
    zencrop::ScopedHFONT hFont;
    zencrop::ScopedHFONT hHintFont;
    zencrop::ScopedHFONT hPageFont;
    bool isDirty = false;
    int curTab = 0;
};

SettingsState* SettingsStateForPage(HWND page);
void UpdateSettingsDialogStrings(SettingsState* state);
bool PreviewChineseForLanguage(AppLanguage::Value language);
SettingsHotkeyDraft* AttachHotkeyDraft(HWND page, LPARAM initParam);
SettingsHotkeyDraft* HotkeyDraftForPage(HWND page);
const HotkeySettings& DraftHotkeysOrShared(const SettingsHotkeyDraft* draft);
int ReadDlgIntClamped(HWND page, int id, int fallback, int minValue, int maxValue);
bool BrowseFolderForSettings(HWND owner, const std::wstring& initialDir,
    const std::wstring& title, std::wstring& resultPath);
UINT GetPageDpi(HWND hwnd);
void UpdatePageScroll(HWND page, int contentHeight);
void RestorePageScroll(HWND page, int position);
void HandlePageVScroll(HWND page, WPARAM wParam);
void HandlePageMouseWheel(HWND page, short delta);
LRESULT CALLBACK PageScrollSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void RelayoutOcrPage(HWND page, UINT dpi);
void RelayoutGeneralPage(HWND page, UINT dpi);
void RelayoutZenCropPage(HWND page, UINT dpi);
void RelayoutAotPage(HWND page, UINT dpi);
void RelayoutScreenshotPage(HWND page, UINT dpi);
void RelayoutTranslatePage(HWND page, UINT dpi);
void RelayoutPageForTab(SettingsState* state, int tabIndex, UINT dpi);
INT_PTR CALLBACK GeneralPageProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK ZenCropPageProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AotPageProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK OcrPageProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK ScreenshotPageProc(HWND, UINT, WPARAM, LPARAM);
void CollectGeneralPage(HWND page, SettingsDraft& draft);
void CollectZenCropPage(HWND page, SettingsDraft& draft);
void CollectAotPage(HWND page, SettingsDraft& draft);
void CollectOcrPage(HWND page, SettingsDraft& draft);
void CollectScreenshotPage(HWND page, SettingsDraft& draft);

} // namespace settings_ui
