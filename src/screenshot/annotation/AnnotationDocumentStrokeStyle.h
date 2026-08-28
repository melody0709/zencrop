#pragma once

#include "screenshot/annotation/AnnotationLegacyDocument.h"

// Document-first draw-style projection for the stroke/shape renderer family.
// Missing Document items use the immutable Host-shaped value only as recovery.

struct ScreenshotAnnotationGeometryArrowDrawStyle {
    int penWidth = 0;
    int lineStyle = 0;
    bool ellipse = false;
    bool filling = false;
    int roundedRadius = 0;
    int arrowShape = 1;
    bool usesCustomColor = false;
    COLORREF customColor = 0;
    int colorIndex = 0;
    int colorAlpha = 100;
};

inline bool ScreenshotAnnotationDocumentResolveGeometryArrowDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationGeometryArrowDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolGeometry &&
        ann.type != ScreenshotToolbarCommand::ToolArrow) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    const ScreenshotToolbarCommand tool = ScreenshotAnnotationDocumentItemToolCommand(*item);
    if (tool != ScreenshotToolbarCommand::ToolGeometry &&
        tool != ScreenshotToolbarCommand::ToolArrow) {
        return false;
    }

    out.penWidth = item->getInt(AnnotationProperty::PenWidth, 0);
    out.lineStyle = item->getInt(AnnotationProperty::PenStyle, 0);
    out.usesCustomColor = item->hasProperty(AnnotationProperty::Color);
    if (out.usesCustomColor) {
        out.customColor = item->getColor(AnnotationProperty::Color, 0);
    }
    out.colorIndex =
        (std::min)((std::max)(item->getInt(AnnotationProperty::ColorIndex, 0), 0), 6);
    out.colorAlpha = item->getInt(AnnotationProperty::ColorAlpha, 100);

    if (tool == ScreenshotToolbarCommand::ToolGeometry) {
        out.ellipse = item->getInt(AnnotationProperty::PathMode, 0) == 3;
        out.filling = item->getBool(AnnotationProperty::Filling, false);
        out.roundedRadius = item->getInt(AnnotationProperty::RectRoundRadius, 0);
        out.arrowShape = 1;
    } else {
        out.ellipse = false;
        out.filling = false;
        out.roundedRadius = 0;
        const int shape = item->getInt(AnnotationProperty::LineShape, 0);
        out.arrowShape = (shape >= 1 && shape <= 8) ? shape : 1;
    }
    return true;
}

inline ScreenshotAnnotationGeometryArrowDrawStyle
ScreenshotAnnotationGeometryArrowDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationGeometryArrowDrawStyle s;
    s.penWidth = ann.penWidth;
    s.lineStyle = ann.lineStyle;
    s.ellipse = ann.ellipse;
    s.filling = ann.filling;
    s.roundedRadius = ann.roundedRadius;
    s.arrowShape = ann.arrowShape;
    s.usesCustomColor = ann.hasCustomColor;
    s.customColor = ann.customColor;
    s.colorIndex = ann.colorIndex;
    s.colorAlpha = ann.colorAlpha;
    return s;
}

inline ScreenshotAnnotationGeometryArrowDrawStyle
ScreenshotAnnotationResolveGeometryArrowDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationGeometryArrowDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveGeometryArrowDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationGeometryArrowDrawStyleFromHost(ann);
}

struct ScreenshotAnnotationPencilBrokenLineDrawStyle {
    int penWidth = 0;
    int lineStyle = 0;
    int brokenLineMode = 0;
    bool brokenLineArrowEnabled = false;
    int brokenLineStartArrowType = 0;
    int brokenLineEndArrowType = 0;
    bool usesCustomColor = false;
    COLORREF customColor = 0;
    int colorIndex = 0;
    int colorAlpha = 100;
};

inline bool ScreenshotAnnotationDocumentResolvePencilBrokenLineDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationPencilBrokenLineDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolPencil &&
        ann.type != ScreenshotToolbarCommand::ToolBrokenLine) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    const ScreenshotToolbarCommand tool = ScreenshotAnnotationDocumentItemToolCommand(*item);
    if (tool != ScreenshotToolbarCommand::ToolPencil &&
        tool != ScreenshotToolbarCommand::ToolBrokenLine) {
        return false;
    }

    out.penWidth = item->getInt(AnnotationProperty::PenWidth, 0);
    out.lineStyle = item->getInt(AnnotationProperty::PenStyle, 0);
    out.usesCustomColor = item->hasProperty(AnnotationProperty::Color);
    if (out.usesCustomColor) {
        out.customColor = item->getColor(AnnotationProperty::Color, 0);
    }
    out.colorIndex =
        (std::min)((std::max)(item->getInt(AnnotationProperty::ColorIndex, 0), 0), 6);
    out.colorAlpha = item->getInt(AnnotationProperty::ColorAlpha, 100);

    if (tool == ScreenshotToolbarCommand::ToolBrokenLine) {
        out.brokenLineMode =
            (std::min)((std::max)(item->getInt(AnnotationProperty::BrokenLineMode, 0), 0), 1);
        out.brokenLineArrowEnabled =
            item->getBool(AnnotationProperty::BrokenLineArrowEnabled, false);
        out.brokenLineStartArrowType =
            item->getInt(AnnotationProperty::BrokenLineStartArrowType, 0);
        out.brokenLineEndArrowType =
            item->getInt(AnnotationProperty::BrokenLineEndArrowType, 0);
    } else {
        out.brokenLineMode = 0;
        out.brokenLineArrowEnabled = false;
        out.brokenLineStartArrowType = 0;
        out.brokenLineEndArrowType = 0;
    }
    return true;
}

inline ScreenshotAnnotationPencilBrokenLineDrawStyle
ScreenshotAnnotationPencilBrokenLineDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationPencilBrokenLineDrawStyle s;
    s.penWidth = ann.penWidth;
    s.lineStyle = ann.lineStyle;
    s.brokenLineMode = ann.brokenLineMode;
    s.brokenLineArrowEnabled = ann.brokenLineArrowEnabled;
    s.brokenLineStartArrowType = ann.brokenLineStartArrowType;
    s.brokenLineEndArrowType = ann.brokenLineEndArrowType;
    s.usesCustomColor = ann.hasCustomColor;
    s.customColor = ann.customColor;
    s.colorIndex = ann.colorIndex;
    s.colorAlpha = ann.colorAlpha;
    return s;
}

inline ScreenshotAnnotationPencilBrokenLineDrawStyle
ScreenshotAnnotationResolvePencilBrokenLineDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationPencilBrokenLineDrawStyle s;
    if (ScreenshotAnnotationDocumentResolvePencilBrokenLineDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationPencilBrokenLineDrawStyleFromHost(ann);
}

struct ScreenshotAnnotationMarkerDrawStyle {
    int penWidth = 0;
    int pathMode = 0;
    int markerBlendMode = 0;
    bool usesCustomColor = false;
    COLORREF customColor = 0;
    int colorIndex = 0;
    int colorAlpha = 100;
};

inline bool ScreenshotAnnotationDocumentResolveMarkerDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationMarkerDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolMarker) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    const ScreenshotToolbarCommand tool = ScreenshotAnnotationDocumentItemToolCommand(*item);
    if (tool != ScreenshotToolbarCommand::ToolMarker) {
        return false;
    }

    out.penWidth = item->getInt(AnnotationProperty::PenWidth, 0);
    out.pathMode = item->getInt(AnnotationProperty::PathMode, 0);
    out.markerBlendMode =
        (std::min)((std::max)(item->getInt(AnnotationProperty::MarkerBlendMode, 0), 0), 1);
    out.usesCustomColor = item->hasProperty(AnnotationProperty::Color);
    if (out.usesCustomColor) {
        out.customColor = item->getColor(AnnotationProperty::Color, 0);
    }
    out.colorIndex =
        (std::min)((std::max)(item->getInt(AnnotationProperty::ColorIndex, 0), 0), 6);
    out.colorAlpha = item->getInt(AnnotationProperty::ColorAlpha, 100);
    return true;
}

inline ScreenshotAnnotationMarkerDrawStyle
ScreenshotAnnotationMarkerDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationMarkerDrawStyle s;
    s.penWidth = ann.penWidth;
    s.pathMode = ann.pathMode;
    s.markerBlendMode = ann.markerBlendMode;
    s.usesCustomColor = ann.hasCustomColor;
    s.customColor = ann.customColor;
    s.colorIndex = ann.colorIndex;
    s.colorAlpha = ann.colorAlpha;
    return s;
}

inline ScreenshotAnnotationMarkerDrawStyle
ScreenshotAnnotationResolveMarkerDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationMarkerDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveMarkerDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationMarkerDrawStyleFromHost(ann);
}

struct ScreenshotAnnotationSerialDrawStyle {
    int serialNumber = 0;
    int serialType = 0;
    bool usesCustomColor = false;
    COLORREF customColor = 0;
    int colorIndex = 0;
    int colorAlpha = 100;
};

inline bool ScreenshotAnnotationDocumentResolveSerialDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationSerialDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolSerial) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    const ScreenshotToolbarCommand tool = ScreenshotAnnotationDocumentItemToolCommand(*item);
    if (tool != ScreenshotToolbarCommand::ToolSerial) {
        return false;
    }

    out.serialNumber = item->getInt(AnnotationProperty::SerialIndex, 0);
    out.serialType = item->getInt(AnnotationProperty::LineShape, 0);
    out.usesCustomColor = item->hasProperty(AnnotationProperty::Color);
    if (out.usesCustomColor) {
        out.customColor = item->getColor(AnnotationProperty::Color, 0);
    }
    out.colorIndex =
        (std::min)((std::max)(item->getInt(AnnotationProperty::ColorIndex, 0), 0), 6);
    out.colorAlpha = item->getInt(AnnotationProperty::ColorAlpha, 100);
    return true;
}

inline ScreenshotAnnotationSerialDrawStyle
ScreenshotAnnotationSerialDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationSerialDrawStyle s;
    s.serialNumber = ann.serialNumber;
    s.serialType = ann.arrowShape;
    s.usesCustomColor = ann.hasCustomColor;
    s.customColor = ann.customColor;
    s.colorIndex = ann.colorIndex;
    s.colorAlpha = ann.colorAlpha;
    return s;
}

inline ScreenshotAnnotationSerialDrawStyle
ScreenshotAnnotationResolveSerialDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationSerialDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveSerialDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationSerialDrawStyleFromHost(ann);
}
