#pragma once

// D-B-10: OCR output artifact options dialog + pure profile/summary helpers.
// Extracted from OcrDashboardWindow.cpp (Stage1 D-B Import/Dialogs).

#include "BatchOcrTypes.h"

#include <windows.h>
#include <string>

enum class DashboardOutputArtifactProfile {
    Compact = 0,
    Review = 1,
    LosslessDebug = 2,
    Custom = 3
};

void DashboardApplyOutputArtifactProfile(
    OcrOutputArtifactOptions& options,
    DashboardOutputArtifactProfile profile);

bool DashboardOutputArtifactOptionsEqual(
    const OcrOutputArtifactOptions& left,
    const OcrOutputArtifactOptions& right);

DashboardOutputArtifactProfile DashboardOutputArtifactProfileFor(
    const OcrOutputArtifactOptions& options);

std::wstring DashboardFormatOutputArtifactSummary(const OcrOutputArtifactOptions& source);
std::wstring DashboardFormatOutputArtifactToolbarLabel(const OcrOutputArtifactOptions& source);

// Modal dialog. Returns true if user accepted; options/outputRoot updated in place.
bool DashboardShowOutputArtifactOptionsDialog(
    HWND owner,
    OcrOutputArtifactOptions& options,
    std::wstring* outputRoot,
    UINT dpi,
    HFONT fallbackFont);
