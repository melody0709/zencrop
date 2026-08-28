#pragma once

#include "screenshot/annotation/AnnotationLegacyDocument.h"

// Document-first draw styles for Magnifier, Mosaic, HighLight and Eraser.
// Geometry stays with the renderer; these functions only resolve style values.

// S-E-37: Magnifier GDI draw style product-read from Document by id.
// Host ann geometry (dest/source rects/angle) remains GDI layout; style props prefer Document.
// Ellipse from PathMode 3 (convertLegacyAnnotation); role=Magnifier.
struct ScreenshotAnnotationMagnifierDrawStyle {
    int penWidth = 0;
    bool ellipse = false;
    int roundedRadius = 0;
    int linkType = 0;
    int magnification = 0;
    bool antiAlias = false;
    bool eraseMark = false;
    bool shadow = false;
    bool usesCustomColor = false;
    COLORREF customColor = 0;
    int colorIndex = 0;
    int colorAlpha = 100;
};

inline bool ScreenshotAnnotationDocumentResolveMagnifierDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationMagnifierDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolMagnifier) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    // Magnifier is role-based (type may be None); accept role or tool command.
    if (item->role() != AnnotationRole::Magnifier &&
        ScreenshotAnnotationDocumentItemToolCommand(*item) != ScreenshotToolbarCommand::ToolMagnifier) {
        return false;
    }

    out.penWidth = item->getInt(AnnotationProperty::PenWidth, 0);
    // PathMode 3 = ellipse (convertLegacyAnnotation).
    out.ellipse = item->getInt(AnnotationProperty::PathMode, 0) == 3;
    out.roundedRadius = item->getInt(AnnotationProperty::RectRoundRadius, 0);
    out.linkType = item->getInt(AnnotationProperty::MagnifierLinkType, 0);
    out.magnification = item->getInt(AnnotationProperty::MagnifierMagnification, 0);
    out.antiAlias = item->getBool(AnnotationProperty::MagnifierAntiAlias, false);
    out.eraseMark = item->getBool(AnnotationProperty::MagnifierEraseMark, false);
    out.shadow = item->getBool(AnnotationProperty::MagnifierShadow, false);
    out.usesCustomColor = item->hasProperty(AnnotationProperty::Color);
    if (out.usesCustomColor) {
        out.customColor = item->getColor(AnnotationProperty::Color, 0);
    }
    out.colorIndex =
        (std::min)((std::max)(item->getInt(AnnotationProperty::ColorIndex, 0), 0), 6);
    out.colorAlpha = item->getInt(AnnotationProperty::ColorAlpha, 100);
    return true;
}

inline ScreenshotAnnotationMagnifierDrawStyle
ScreenshotAnnotationMagnifierDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationMagnifierDrawStyle s;
    s.penWidth = ann.penWidth;
    s.ellipse = ann.ellipse;
    s.roundedRadius = ann.roundedRadius;
    s.linkType = ann.magnifierLinkType;
    s.magnification = ann.magnifierMagnification;
    s.antiAlias = ann.magnifierAntiAlias;
    s.eraseMark = ann.magnifierEraseMark;
    s.shadow = ann.magnifierShadow;
    s.usesCustomColor = ann.hasCustomColor;
    s.customColor = ann.customColor;
    s.colorIndex = ann.colorIndex;
    s.colorAlpha = ann.colorAlpha;
    return s;
}

inline ScreenshotAnnotationMagnifierDrawStyle
ScreenshotAnnotationResolveMagnifierDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationMagnifierDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveMagnifierDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationMagnifierDrawStyleFromHost(ann);
}

// S-E-39: Mosaic GDI draw style product-read from Document by id.
// Host ann geometry (start/end/points/angle) remains GDI layout; style props prefer Document.
// Covers ToolMosaic + ToolAutoMosaic (same dual-write props: PenWidth/PathMode/MosaicMode).
struct ScreenshotAnnotationMosaicDrawStyle {
    int penWidth = 0;
    int pathMode = 0;
    int mosaicMode = 0;
};

inline bool ScreenshotAnnotationDocumentResolveMosaicDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationMosaicDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolMosaic &&
        ann.type != ScreenshotToolbarCommand::ToolAutoMosaic) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    const ScreenshotToolbarCommand tool = ScreenshotAnnotationDocumentItemToolCommand(*item);
    if (tool != ScreenshotToolbarCommand::ToolMosaic &&
        tool != ScreenshotToolbarCommand::ToolAutoMosaic) {
        return false;
    }

    out.penWidth = item->getInt(AnnotationProperty::PenWidth, 0);
    out.pathMode = item->getInt(AnnotationProperty::PathMode, 0);
    out.mosaicMode =
        (std::min)((std::max)(item->getInt(AnnotationProperty::MosaicMode, 0), 0), 1);
    return true;
}

inline ScreenshotAnnotationMosaicDrawStyle
ScreenshotAnnotationMosaicDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationMosaicDrawStyle s;
    s.penWidth = ann.penWidth;
    s.pathMode = ann.pathMode;
    s.mosaicMode = ann.mosaicMode;
    return s;
}

inline ScreenshotAnnotationMosaicDrawStyle
ScreenshotAnnotationResolveMosaicDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationMosaicDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveMosaicDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationMosaicDrawStyleFromHost(ann);
}

// S-E-40: HighLight GDI draw style product-read from Document by id.
// Host ann geometry (start/end/angle) remains GDI layout; style props prefer Document.
// Ellipse from PathMode 3 (convertLegacyAnnotation); role=HighLight.
// penWidth dual-written as PenWidth + HighLightStrokeWidth.
struct ScreenshotAnnotationHighLightDrawStyle {
    int penWidth = 0;
    int opacity = 0;
    bool stroke = false;
    COLORREF strokeColor = 0;
    bool ellipse = false;
};

inline bool ScreenshotAnnotationDocumentResolveHighLightDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationHighLightDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolHighLight) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    if (item->role() != AnnotationRole::HighLight &&
        ScreenshotAnnotationDocumentItemToolCommand(*item) != ScreenshotToolbarCommand::ToolHighLight) {
        return false;
    }

    // Prefer HighLightStrokeWidth; fall back to PenWidth (both dual-written from penWidth).
    const int strokeWidth = item->getInt(AnnotationProperty::HighLightStrokeWidth, 0);
    out.penWidth = strokeWidth > 0
        ? strokeWidth
        : item->getInt(AnnotationProperty::PenWidth, 0);
    out.opacity = static_cast<int>(item->getDouble(AnnotationProperty::HighLightOpacity, 0.0));
    out.stroke = item->getBool(AnnotationProperty::HighLightStroke, false);
    out.strokeColor = item->getColor(AnnotationProperty::HighLightStrokeColor, 0);
    // PathMode 3 = ellipse (convertLegacyAnnotation).
    out.ellipse = item->getInt(AnnotationProperty::PathMode, 0) == 3;
    return true;
}

inline ScreenshotAnnotationHighLightDrawStyle
ScreenshotAnnotationHighLightDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationHighLightDrawStyle s;
    s.penWidth = ann.penWidth;
    s.opacity = ann.highLightOpacity;
    s.stroke = ann.highLightStroke;
    s.strokeColor = ann.highLightStrokeColor;
    s.ellipse = ann.ellipse;
    return s;
}

inline ScreenshotAnnotationHighLightDrawStyle
ScreenshotAnnotationResolveHighLightDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationHighLightDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveHighLightDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationHighLightDrawStyleFromHost(ann);
}

// S-E-41: Eraser GDI draw style product-read from Document by id.
// Host ann geometry (start/end/points/angle) remains GDI layout; style props prefer Document.
// Ellipse from PathMode 3 (convertLegacyAnnotation).
struct ScreenshotAnnotationEraserDrawStyle {
    int penWidth = 0;
    int pathMode = 0;
    bool ellipse = false;
};

inline bool ScreenshotAnnotationDocumentResolveEraserDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationEraserDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolEraser) {
        return false;
    }
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    const ScreenshotToolbarCommand tool = ScreenshotAnnotationDocumentItemToolCommand(*item);
    if (tool != ScreenshotToolbarCommand::ToolEraser) {
        return false;
    }

    out.penWidth = item->getInt(AnnotationProperty::PenWidth, 0);
    out.pathMode = item->getInt(AnnotationProperty::PathMode, 0);
    // PathMode 3 = ellipse (convertLegacyAnnotation).
    out.ellipse = out.pathMode == 3;
    return true;
}

inline ScreenshotAnnotationEraserDrawStyle
ScreenshotAnnotationEraserDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationEraserDrawStyle s;
    s.penWidth = ann.penWidth;
    s.pathMode = ann.pathMode;
    s.ellipse = ann.ellipse;
    return s;
}

inline ScreenshotAnnotationEraserDrawStyle
ScreenshotAnnotationResolveEraserDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationEraserDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveEraserDrawStyle(document, ann, s)) {
        return s;
    }
    return ScreenshotAnnotationEraserDrawStyleFromHost(ann);
}
