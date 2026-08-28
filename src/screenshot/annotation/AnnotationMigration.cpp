#include "screenshot/annotation/AnnotationMigration.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"
#include "screenshot/ScreenshotTypes.h"
#include "core/WideStringUtils.h"

static std::wstring SerializeAnnotationPoints(const std::vector<POINT>& points) {
    std::wstring result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i > 0) result += L";";
        // OWN-127: pure point xy (WideStringUtils).
        result += WideFormatPointXy(points[i].x, points[i].y);
    }
    return result;
}

static std::vector<POINT> ParseAnnotationPoints(const std::wstring& value) {
    std::vector<POINT> points;
    size_t pos = 0;
    while (pos < value.length()) {
        size_t comma = value.find(L',', pos);
        if (comma == std::wstring::npos) break;
        size_t semi = value.find(L';', comma + 1);
        std::wstring xText = value.substr(pos, comma - pos);
        std::wstring yText = semi == std::wstring::npos
            ? value.substr(comma + 1)
            : value.substr(comma + 1, semi - comma - 1);
        // OWN-77: pure int parse (no _wtoi).
        POINT pt = {
            WideParseJsonIntToken(xText),
            WideParseJsonIntToken(yText)
        };
        points.push_back(pt);
        if (semi == std::wstring::npos) break;
        pos = semi + 1;
    }
    return points;
}

static COLORREF AnnotationPresetColorLocal(int index) {
    const COLORREF colors[] = {
        RGB(250, 3, 15), RGB(248, 118, 16), RGB(244, 207, 81),
        RGB(114, 204, 87), RGB(51, 136, 255), RGB(210, 137, 226), RGB(255, 255, 255)
    };
    return colors[(index >= 0 && index < 7) ? index : 0];
}

void EnsureLegacyAnnotationId(ScreenshotAnnotation& legacy) {
    if (legacy.id.empty()) {
        legacy.id = ScreenshotAnnotationItem::generateId();
    }
}

ScreenshotAnnotationItem convertLegacyAnnotation(const ScreenshotAnnotation& legacy, int index) {
    AnnotationType type = ToolCommandToAnnotationType(legacy.type);
    ScreenshotAnnotationItem item(type);

    if (!legacy.id.empty()) {
        item.setId(legacy.id);
    } else if (index >= 0) {
        // OWN-127: pure legacy id (WideStringUtils).
        item.setId(WideFormatLegacyId(index));
    }

    item.setStart(legacy.start);
    item.setEnd(legacy.end);
    item.setText(legacy.text);

    // Set roles for annotations whose behavior is independent of the item type.
    if (legacy.type == ScreenshotToolbarCommand::ToolMagnifier)
        item.setRole(AnnotationRole::Magnifier);
    else if (legacy.type == ScreenshotToolbarCommand::ToolHighLight)
        item.setRole(AnnotationRole::HighLight);
    else if (legacy.type == ScreenshotToolbarCommand::ToolWatermark)
        item.setRole(AnnotationRole::Watermark);
    else if (legacy.type == ScreenshotToolbarCommand::ToolAutoMosaic)
        item.setRole(AnnotationRole::AutoMosaicRect);

    if (index >= 0)
        item.setInt(AnnotationProperty::StackIndex, index);
    item.setInt(AnnotationProperty::ColorIndex, legacy.colorIndex);
    item.setInt(AnnotationProperty::PenWidth, legacy.penWidth);
    item.setInt(AnnotationProperty::RectRoundRadius, legacy.roundedRadius);
    item.setDouble(AnnotationProperty::Angle, legacy.angle);
    item.setInt(AnnotationProperty::PenStyle, legacy.lineStyle);
    item.setInt(AnnotationProperty::LineShape, legacy.arrowShape);
    item.setBool(AnnotationProperty::Filling, legacy.filling);
    int pathMode = legacy.pathMode;
    if (pathMode == 0 && legacy.ellipse) pathMode = 3;
    if (pathMode != 0)
        item.setInt(AnnotationProperty::PathMode, pathMode);
    if (!legacy.points.empty())
        item.setString(AnnotationProperty::PathPoints, SerializeAnnotationPoints(legacy.points));
    item.setInt(AnnotationProperty::ColorAlpha, legacy.colorAlpha);
    item.setInt(AnnotationProperty::MarkerBlendMode, (std::min)((std::max)(legacy.markerBlendMode, 0), 1));
    if (legacy.hasCustomColor)
        item.setColor(AnnotationProperty::Color, legacy.customColor);

    if (legacy.type == ScreenshotToolbarCommand::ToolMosaic ||
        legacy.type == ScreenshotToolbarCommand::ToolAutoMosaic) {
        item.setInt(AnnotationProperty::MosaicMode, (std::min)((std::max)(legacy.mosaicMode, 0), 1));
    }

    // BrokenLine
    item.setInt(AnnotationProperty::BrokenLineMode, legacy.brokenLineMode);
    item.setBool(AnnotationProperty::BrokenLineArrowEnabled, legacy.brokenLineArrowEnabled);
    item.setInt(AnnotationProperty::BrokenLineStartArrowType, legacy.brokenLineStartArrowType);
    item.setInt(AnnotationProperty::BrokenLineEndArrowType, legacy.brokenLineEndArrowType);

    // Magnifier
    item.setInt(AnnotationProperty::MagnifierLinkType, legacy.magnifierLinkType);
    item.setInt(AnnotationProperty::MagnifierMagnification, legacy.magnifierMagnification);
    if (legacy.type == ScreenshotToolbarCommand::ToolMagnifier) {
        RECT source = ScreenshotMagnifierSourceRect(legacy);
        POINT sourceCenter = ScreenshotAnnotationRectCenter(source);
        item.setInt(AnnotationProperty::MagnifierSourceX, sourceCenter.x);
        item.setInt(AnnotationProperty::MagnifierSourceY, sourceCenter.y);
        item.setInt(AnnotationProperty::MagnifierSourceLeft, source.left);
        item.setInt(AnnotationProperty::MagnifierSourceTop, source.top);
        item.setInt(AnnotationProperty::MagnifierSourceRight, source.right);
        item.setInt(AnnotationProperty::MagnifierSourceBottom, source.bottom);
    }
    item.setBool(AnnotationProperty::MagnifierAntiAlias, legacy.magnifierAntiAlias);
    item.setBool(AnnotationProperty::MagnifierEraseMark, legacy.magnifierEraseMark);
    item.setBool(AnnotationProperty::MagnifierShadow, legacy.magnifierShadow);

    // HighLight
    item.setDouble(AnnotationProperty::HighLightOpacity, (double)legacy.highLightOpacity);
    item.setBool(AnnotationProperty::HighLightStroke, legacy.highLightStroke);
    item.setInt(AnnotationProperty::HighLightStrokeWidth, legacy.penWidth);
    item.setColor(AnnotationProperty::HighLightStrokeColor, legacy.highLightStrokeColor);

    // Text
    item.setBool(AnnotationProperty::TextBold, legacy.textBold);
    item.setBool(AnnotationProperty::TextItalics, legacy.textItalics);
    item.setBool(AnnotationProperty::TextOutline, legacy.textOutline);
    item.setInt(AnnotationProperty::TextOutlineSize, legacy.textOutlineSize);
    item.setColor(AnnotationProperty::TextOutlineColor, legacy.textOutlineColor);
    item.setBool(AnnotationProperty::TextBackground, legacy.textBackground);
    item.setColor(AnnotationProperty::TextBackgroundColor, legacy.textBackgroundColor);
    item.setInt(AnnotationProperty::TextBackgroundOpacity, legacy.textBackgroundOpacity);
    item.setInt(AnnotationProperty::TextBackgroundRounded, legacy.textBackgroundRounded);
    item.setInt(AnnotationProperty::TextBackgroundPadding, legacy.textBackgroundPadding);
    if (!legacy.textFontFamily.empty())
        item.setString(AnnotationProperty::TextFontFamily, legacy.textFontFamily);
    // S-D-1: TextFontSize sole Double. Prefer F; fall back to int.
    if (legacy.textFontSizeF > 0.0)
        item.setDouble(AnnotationProperty::TextFontSize, legacy.textFontSizeF);
    else if (legacy.textFontSize > 0)
        item.setDouble(AnnotationProperty::TextFontSize, static_cast<double>(legacy.textFontSize));

    // Watermark
    if (legacy.type == ScreenshotToolbarCommand::ToolWatermark) {
        item.setString(AnnotationProperty::WatermarkText, legacy.text);
        item.setColor(AnnotationProperty::WatermarkColor, legacy.watermarkColor);
        item.setDouble(AnnotationProperty::WatermarkOpacity, (double)legacy.watermarkOpacity);
        item.setInt(AnnotationProperty::WatermarkFontSize, legacy.watermarkFontSize);
        item.setInt(AnnotationProperty::WatermarkGap, legacy.watermarkGap);
        item.setInt(AnnotationProperty::WatermarkAngle, legacy.watermarkAngle);
        if (!legacy.watermarkFontFamily.empty())
            item.setString(AnnotationProperty::WatermarkFontFamily, legacy.watermarkFontFamily);
        item.setInt(AnnotationProperty::WatermarkPosition, legacy.watermarkPosition);
    }

    // Serial
    if (legacy.serialNumber > 0)
        item.setInt(AnnotationProperty::SerialIndex, legacy.serialNumber);

    return item;
}

ScreenshotAnnotation LegacyAnnotationFromSnapshot(const AnnotationSnapshot& snap, const std::wstring& fallbackId) {
    ScreenshotAnnotation ann;
    ann.id = !snap.id.empty() ? snap.id : fallbackId;

    switch (snap.role) {
    case AnnotationRole::Magnifier:
        ann.type = ScreenshotToolbarCommand::ToolMagnifier;
        break;
    case AnnotationRole::HighLight:
        ann.type = ScreenshotToolbarCommand::ToolHighLight;
        break;
    case AnnotationRole::Watermark:
        ann.type = ScreenshotToolbarCommand::ToolWatermark;
        break;
    case AnnotationRole::AutoMosaicRect:
    case AnnotationRole::AutoMosaicPath:
        ann.type = ScreenshotToolbarCommand::ToolAutoMosaic;
        break;
    default:
        ann.type = AnnotationTypeToToolCommand(snap.type);
        break;
    }

    ann.start = snap.start;
    ann.end = snap.end;
    ann.text = snap.text;
    ann.colorIndex = snap.getInt(AnnotationProperty::ColorIndex, 0);
    ann.colorAlpha = snap.getInt(AnnotationProperty::ColorAlpha, 100);
    // S-D-2: sole props map; presence of Color marks custom color.
    auto colorIt = snap.props.find(AnnotationProperty::Color);
    if (colorIt != snap.props.end()) {
        if (const COLORREF* c = std::get_if<COLORREF>(&colorIt->second)) {
            ann.hasCustomColor = true;
            ann.customColor = *c;
        }
    }
    ann.penWidth = snap.getInt(AnnotationProperty::PenWidth, 0);
    ann.roundedRadius = snap.getInt(AnnotationProperty::RectRoundRadius, 0);
    ann.angle = snap.getDouble(AnnotationProperty::Angle, 0.0);
    ann.lineStyle = snap.getInt(AnnotationProperty::PenStyle, 1);
    ann.arrowShape = snap.getInt(AnnotationProperty::LineShape, 1);
    ann.filling = snap.getBool(AnnotationProperty::Filling, false);
    ann.pathMode = snap.getInt(AnnotationProperty::PathMode, 0);
    ann.markerBlendMode = (std::min)((std::max)(snap.getInt(AnnotationProperty::MarkerBlendMode, 0), 0), 1);
    ann.mosaicMode = (std::min)((std::max)(snap.getInt(AnnotationProperty::MosaicMode, 0), 0), 1);
    ann.points = ParseAnnotationPoints(snap.getString(AnnotationProperty::PathPoints));
    ann.ellipse = ann.pathMode == 3;
    ann.brokenLineMode = snap.getInt(AnnotationProperty::BrokenLineMode, 0);
    ann.brokenLineArrowEnabled = snap.getBool(AnnotationProperty::BrokenLineArrowEnabled, true);
    ann.brokenLineStartArrowType = snap.getInt(AnnotationProperty::BrokenLineStartArrowType, 0);
    ann.brokenLineEndArrowType = snap.getInt(AnnotationProperty::BrokenLineEndArrowType, 1);
    ann.magnifierLinkType = snap.getInt(AnnotationProperty::MagnifierLinkType, 0);
    ann.magnifierMagnification = snap.getInt(AnnotationProperty::MagnifierMagnification, 150);
    if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
        RECT fallbackSource = ScreenshotMagnifierFallbackSourceRect(ann);
        RECT source = {
            snap.getInt(AnnotationProperty::MagnifierSourceLeft, fallbackSource.left),
            snap.getInt(AnnotationProperty::MagnifierSourceTop, fallbackSource.top),
            snap.getInt(AnnotationProperty::MagnifierSourceRight, fallbackSource.right),
            snap.getInt(AnnotationProperty::MagnifierSourceBottom, fallbackSource.bottom)
        };
        if (source.right > source.left && source.bottom > source.top) {
            ScreenshotMagnifierSetSourceRect(ann, source);
        } else {
            ann.auxPoint = {
                snap.getInt(AnnotationProperty::MagnifierSourceX, ann.start.x),
                snap.getInt(AnnotationProperty::MagnifierSourceY, ann.start.y)
            };
        }
    }
    ann.magnifierAntiAlias = snap.getBool(AnnotationProperty::MagnifierAntiAlias, true);
    ann.magnifierEraseMark = snap.getBool(AnnotationProperty::MagnifierEraseMark, false);
    ann.magnifierShadow = snap.getBool(AnnotationProperty::MagnifierShadow, false);
    ann.highLightOpacity = (int)snap.getDouble(AnnotationProperty::HighLightOpacity, 68.0);
    ann.highLightStroke = snap.getBool(AnnotationProperty::HighLightStroke, false);
    ann.highLightStrokeColor = snap.getColor(AnnotationProperty::HighLightStrokeColor, RGB(255, 15, 0));
    ann.textBold = snap.getBool(AnnotationProperty::TextBold, false);
    ann.textItalics = snap.getBool(AnnotationProperty::TextItalics, false);
    ann.textOutline = snap.getBool(AnnotationProperty::TextOutline, false);
    ann.textOutlineSize = snap.getInt(AnnotationProperty::TextOutlineSize, 1);
    ann.textOutlineColor = snap.getColor(AnnotationProperty::TextOutlineColor, RGB(255, 255, 255));
    ann.textBackground = snap.getBool(AnnotationProperty::TextBackground, false);
    ann.textBackgroundColor = snap.getColor(AnnotationProperty::TextBackgroundColor, RGB(0, 0, 0));
    ann.textBackgroundOpacity = snap.getInt(AnnotationProperty::TextBackgroundOpacity, 100);
    ann.textBackgroundRounded = snap.getInt(AnnotationProperty::TextBackgroundRounded, 0);
    ann.textBackgroundPadding = snap.getInt(AnnotationProperty::TextBackgroundPadding, 0);
    ann.textFontFamily = snap.getString(AnnotationProperty::TextFontFamily);
    // S-D-1: TextFontSize sole Double; derive int for legacy field.
    ann.textFontSizeF = snap.getDouble(AnnotationProperty::TextFontSize, 0.0);
    ann.textFontSize = ann.textFontSizeF > 0.0
        ? static_cast<int>(ann.textFontSizeF + 0.5)
        : 0;
    if (ann.type == ScreenshotToolbarCommand::ToolWatermark) {
        // S-D-2: sole props map; presence of WatermarkText overwrites text.
        auto watermarkTextIt = snap.props.find(AnnotationProperty::WatermarkText);
        if (watermarkTextIt != snap.props.end()) {
            if (const std::wstring* t = std::get_if<std::wstring>(&watermarkTextIt->second)) {
                ann.text = *t;
            }
        }
        ann.watermarkOpacity = (int)snap.getDouble(AnnotationProperty::WatermarkOpacity, 50.0);
        ann.watermarkFontSize = snap.getInt(AnnotationProperty::WatermarkFontSize, ann.textFontSize > 0 ? ann.textFontSize : 27);
        ann.watermarkGap = snap.getInt(AnnotationProperty::WatermarkGap, 20);
        ann.watermarkAngle = snap.getInt(AnnotationProperty::WatermarkAngle, 0);
        ann.watermarkFontFamily = snap.getString(AnnotationProperty::WatermarkFontFamily);
        if (ann.watermarkFontFamily.empty()) ann.watermarkFontFamily = ann.textFontFamily;
        ann.watermarkPosition = snap.getInt(AnnotationProperty::WatermarkPosition, 1);
        COLORREF fallbackColor = ann.hasCustomColor ? ann.customColor : AnnotationPresetColorLocal(ann.colorIndex);
        ann.watermarkColor = snap.getColor(AnnotationProperty::WatermarkColor, fallbackColor);
    }
    ann.serialNumber = snap.getInt(AnnotationProperty::SerialIndex, 0);

    return ann;
}
