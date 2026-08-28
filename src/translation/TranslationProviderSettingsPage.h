#pragma once

#include <windows.h>

namespace translation {

INT_PTR CALLBACK TranslationProviderSettingsPageProc(
    HWND page, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace translation

