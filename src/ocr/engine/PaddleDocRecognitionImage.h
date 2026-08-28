#pragma once

#include "PaddleDocRegionGrouping.h"

#include <windows.h>

#include <cstddef>
#include <vector>

struct PaddleDocRecognitionImageStats {
    RECT sourceRect = {};
    int width = 0;
    int height = 0;
    size_t memberCount = 0;
    bool polygonApplied = false;
    bool polygonFallback = false;
    bool formulaMarginApplied = false;
    bool formulaMarginFallback = false;
    bool legacyPad8Union = false;
};

HBITMAP CropPaddleDocRecognitionRegion(
    HBITMAP source,
    const LayoutRegion& region,
    bool applyFormulaMargin,
    PaddleDocRecognitionImageStats* stats = nullptr);

HBITMAP ComposePaddleDocRecognitionGroup(
    HBITMAP source,
    const std::vector<LayoutRegion>& regions,
    const PaddleDocRecognitionGroup& group,
    PaddleDocRecognitionImageStats* stats = nullptr);

