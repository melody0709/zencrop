#pragma once

// D-B-8: PDF password dialog extracted from OcrDashboardWindow.cpp.
// Password is returned only via out-param; never persisted by this module.

#include <windows.h>
#include <string>

// Returns true if the user accepted and password is non-empty-capable (may be empty string).
// Returns false on cancel, registration failure, or dialog creation failure.
// password is cleared on entry; set only when accepted.
bool DashboardPromptForPdfPassword(
    HWND owner,
    UINT dpi,
    HFONT font,
    const std::wstring& pdfName,
    int attemptNumber,
    int maxAttempts,
    const std::wstring& previousError,
    std::wstring& password);
