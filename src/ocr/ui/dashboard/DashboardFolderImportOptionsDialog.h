#pragma once

// D-B-9: Folder import options dialog extracted from OcrDashboardWindow.cpp.
// Returns typed result; caller (Window) owns persistence into DashboardState.

#include <windows.h>
#include <string>

struct DashboardFolderImportOptionsResult {
    bool accepted = false;
    bool recursive = true;
    int maxDepth = 16;
    std::wstring excludePatterns;
    std::wstring outputRoot;
};

// Shows modal folder-import options dialog.
// - On accept: accepted=true and fields filled; password-free prefs only.
// - On cancel: accepted=false.
// - On class registration failure: returns result with accepted=false and
//   dialogFailedOpen=true so host can keep last-saved settings (legacy behavior).
struct DashboardFolderImportOptionsRun {
    DashboardFolderImportOptionsResult result;
    bool dialogFailedOpen = false;
};

DashboardFolderImportOptionsRun DashboardRunFolderImportOptionsDialog(
    HWND owner,
    UINT dpi,
    HFONT font,
    size_t directoryCount,
    bool recursiveSeed,
    int maxDepthSeed,
    const std::wstring& excludeSeed,
    const std::wstring& outputRootSeed);

// Shared folder picker used by folder/PDF/artifact dialogs (was static in mega-cpp).
bool DashboardSelectOcrOptionsOutputRoot(
    HWND owner,
    const std::wstring& currentRoot,
    const wchar_t* title,
    std::wstring& outputRoot);
