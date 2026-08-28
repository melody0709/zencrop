#pragma once

#include "core/WideStringUtils.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Legacy annotation types, extracted from OverlayWindow for migration bridge access.
// These will be deprecated once all tools are migrated to ScreenshotAnnotationItem.

enum class ScreenshotToolbarCommand;

struct ScreenshotToolbarButton {
    RECT rect = {};
    ScreenshotToolbarCommand command{};
    const wchar_t* label = L"";
    bool enabled = true;
};

struct ScreenshotAnnotation {
    ScreenshotToolbarCommand type{};
    std::wstring id;
    POINT start = {};
    POINT end = {};
    std::wstring text;
    int serialNumber = 0;
    bool ellipse = false;
    bool filling = false;
    int colorIndex = 0;
    COLORREF customColor = RGB(0, 0, 0);
    bool hasCustomColor = false;
    int colorAlpha = 100;
    bool textBold = false;
    bool textItalics = false;
    bool textOutline = false;
    int textOutlineSize = 1;
    COLORREF textOutlineColor = RGB(255, 255, 255);
    bool textBackground = false;
    COLORREF textBackgroundColor = RGB(0, 0, 0);
    int textBackgroundOpacity = 100;
    int textBackgroundRounded = 0;
    int textBackgroundPadding = 0;
    std::wstring textFontFamily;
    int textFontSize = 0;
    double textFontSizeF = 0.0;
    int lineStyle = 1;
    int arrowShape = 1;
    int penWidth = 0;
    int roundedRadius = 0;
    double angle = 0.0;
    int pathMode = 0; // 1=free path, 2=rect, 3=ellipse
    int markerBlendMode = 0; // 0=Multiply, 1=Translucent/SourceOver
    int mosaicMode = 0; // 0=pixelate, 1=blur
    std::vector<POINT> points;
    POINT auxPoint = {};
    int brokenLineMode = 0;
    bool brokenLineArrowEnabled = true;
    int brokenLineStartArrowType = 0;
    int brokenLineEndArrowType = 1;
    int magnifierLinkType = 0;
    int magnifierMagnification = 150;
    POINT magnifierSourceStart = {};
    POINT magnifierSourceEnd = {};
    bool magnifierAntiAlias = true;
    bool magnifierEraseMark = false;
    bool magnifierShadow = false;
    int highLightOpacity = 68;
    bool highLightStroke = false;
    COLORREF highLightStrokeColor = RGB(255, 15, 0);
    COLORREF watermarkColor = RGB(250, 3, 15);
    int watermarkOpacity = 50;
    int watermarkFontSize = 27;
    int watermarkGap = 20;
    int watermarkAngle = 0;
    std::wstring watermarkFontFamily;
    int watermarkPosition = 1;
};

inline RECT ScreenshotAnnotationNormalizeRect(POINT start, POINT end) {
    return {
        (std::min)(start.x, end.x),
        (std::min)(start.y, end.y),
        (std::max)(start.x, end.x),
        (std::max)(start.y, end.y)
    };
}

inline POINT ScreenshotAnnotationRectCenter(RECT rc) {
    return { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
}

inline bool ScreenshotMagnifierHasExplicitSourceRect(const ScreenshotAnnotation& ann) {
    RECT rc = ScreenshotAnnotationNormalizeRect(ann.magnifierSourceStart, ann.magnifierSourceEnd);
    return rc.right > rc.left && rc.bottom > rc.top;
}

inline RECT ScreenshotMagnifierFallbackSourceRect(const ScreenshotAnnotation& ann) {
    RECT dest = ScreenshotAnnotationNormalizeRect(ann.start, ann.end);
    const int destW = (std::max)(1, (int)(dest.right - dest.left));
    const int destH = (std::max)(1, (int)(dest.bottom - dest.top));
    const double mag = (std::max)(100, ann.magnifierMagnification) / 100.0;
    const int sourceW = (std::max)(1, (int)std::lround(destW / mag));
    const int sourceH = (std::max)(1, (int)std::lround(destH / mag));
    POINT center = ann.auxPoint;
    if (center.x == 0 && center.y == 0) {
        center = ScreenshotAnnotationRectCenter(dest);
    }
    return {
        center.x - sourceW / 2,
        center.y - sourceH / 2,
        center.x - sourceW / 2 + sourceW,
        center.y - sourceH / 2 + sourceH
    };
}

inline RECT ScreenshotMagnifierSourceRect(const ScreenshotAnnotation& ann) {
    if (ScreenshotMagnifierHasExplicitSourceRect(ann)) {
        return ScreenshotAnnotationNormalizeRect(ann.magnifierSourceStart, ann.magnifierSourceEnd);
    }
    return ScreenshotMagnifierFallbackSourceRect(ann);
}

inline void ScreenshotMagnifierSetSourceRect(ScreenshotAnnotation& ann, RECT source) {
    source = ScreenshotAnnotationNormalizeRect({ source.left, source.top }, { source.right, source.bottom });
    ann.magnifierSourceStart = { source.left, source.top };
    ann.magnifierSourceEnd = { source.right, source.bottom };
    ann.auxPoint = ScreenshotAnnotationRectCenter(source);
}

inline RECT ScreenshotMagnifierResultRectFromSource(RECT source, int magnificationPercent) {
    source = ScreenshotAnnotationNormalizeRect({ source.left, source.top }, { source.right, source.bottom });
    const int sourceW = (std::max)(1, (int)(source.right - source.left));
    const int sourceH = (std::max)(1, (int)(source.bottom - source.top));
    const double mag = (std::max)(100, magnificationPercent) / 100.0;
    const int resultW = (std::max)(1, (int)std::lround(sourceW * mag));
    const int resultH = (std::max)(1, (int)std::lround(sourceH * mag));
    POINT center = ScreenshotAnnotationRectCenter(source);
    return {
        center.x - resultW / 2,
        center.y - resultH / 2,
        center.x - resultW / 2 + resultW,
        center.y - resultH / 2 + resultH
    };
}

inline void ScreenshotMagnifierSetResultRect(ScreenshotAnnotation& ann, RECT result) {
    result = ScreenshotAnnotationNormalizeRect({ result.left, result.top }, { result.right, result.bottom });
    ann.start = { result.left, result.top };
    ann.end = { result.right, result.bottom };
}

inline void ScreenshotMagnifierTranslateSource(ScreenshotAnnotation& ann, int dx, int dy) {
    if (!ScreenshotMagnifierHasExplicitSourceRect(ann)) {
        ScreenshotMagnifierSetSourceRect(ann, ScreenshotMagnifierFallbackSourceRect(ann));
    }
    ann.magnifierSourceStart.x += dx;
    ann.magnifierSourceStart.y += dy;
    ann.magnifierSourceEnd.x += dx;
    ann.magnifierSourceEnd.y += dy;
    ann.auxPoint.x += dx;
    ann.auxPoint.y += dy;
}

inline void ScreenshotMagnifierResizeResultFromSource(ScreenshotAnnotation& ann) {
    RECT source = ScreenshotMagnifierSourceRect(ann);
    RECT oldResult = ScreenshotAnnotationNormalizeRect(ann.start, ann.end);
    RECT newResult = ScreenshotMagnifierResultRectFromSource(source, ann.magnifierMagnification);
    POINT oldCenter = ScreenshotAnnotationRectCenter(oldResult);
    POINT newCenter = ScreenshotAnnotationRectCenter(newResult);
    int dx = oldCenter.x - newCenter.x;
    int dy = oldCenter.y - newCenter.y;
    newResult.left += dx;
    newResult.right += dx;
    newResult.top += dy;
    newResult.bottom += dy;
    ScreenshotMagnifierSetResultRect(ann, newResult);
}

inline const wchar_t* ScreenshotTextFontFamilyName(int index) {
    switch (index) {
    case 1: return L"Segoe UI";
    case 2: return L"SimSun";
    case 3: return L"Arial";
    default: return L"Microsoft YaHei";
    }
}

inline int ScreenshotTextFontFamilyIndexFromName(const std::wstring& family) {
    if (WideEqualsNoCase(family, L"Segoe UI")) return 1;
    if (WideEqualsNoCase(family, L"SimSun")) return 2;
    if (WideEqualsNoCase(family, L"Arial")) return 3;
    return 0;
}

inline std::wstring ScreenshotTextFontSizeLabel(double size) {
    if (std::abs(size - 14.0) < 0.005) return L"14";
    if (std::abs(size - 18.0) < 0.005) return L"18";
    if (std::abs(size - 24.0) < 0.005) return L"24";
    if (std::abs(size - 26.98) < 0.005) return L"26.98";
    if (std::abs(size - 27.0) < 0.005) return L"26.98";
    if (std::abs(size - 32.0) < 0.005) return L"32";
    if (std::abs(size - 48.0) < 0.005) return L"48";
    // OWN-114: pure float2 size label (WideStringUtils).
    return WideFormatFloat2(size);
}
