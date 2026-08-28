#pragma once

#include "ocr/ui/DashboardTextMode.h"

// Stage 1 D-D: typed events from pure Controller transitions (no HWND / GDI).

enum class DashboardEventKind {
    None,
    TextModeChanged,
    FilterChanged,
    SelectionChanged,
    VisibleHistoryChanged,
    PreviewHostRequired,
    HistoryTextRebuildRequired,
    SourceListRebuildRequired,
};

struct DashboardEvent {
    DashboardEventKind kind = DashboardEventKind::None;
    bool preferredTextModeChanged = false;
    bool needPreviewHost = false;
    bool needSelectLastVisible = false;
    int resolvedHistoryIndex = -1;
};
