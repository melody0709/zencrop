#pragma once

#include "ocr/ui/DashboardTextMode.h"

#include <string>

// Stage 1 D-D: typed commands for Dashboard business transitions (no HWND).

enum class DashboardCommandKind {
    SetTextMode,
    SetFilter,
    SelectHistoryIndex,
    ClearHistorySelection,
};

struct DashboardCommand {
    DashboardCommandKind kind = DashboardCommandKind::SetFilter;
    DashboardTextMode textMode = DashboardTextMode::Preview;
    std::wstring filterText;
    int historyIndex = -1;
};
