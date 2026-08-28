#pragma once

#include "screenshot/annotation/AnnotationLegacyDocument.h"

// Document-first textual annotation styles. Host-shaped values are recovery only
// when the committed Document item is unavailable.

struct ScreenshotAnnotationTextDrawStyle {
    int fontSize = 0;
    std::wstring fontFamily;
    bool bold = false;
    bool italics = false;
    bool background = false;
    COLORREF backgroundColor = 0;
    int backgroundOpacity = 0;
    int backgroundPadding = 0;
    int backgroundRounded = 0;
    bool outline = false;
    COLORREF outlineColor = 0;
    int outlineSize = 0;
    bool usesCustomColor = false;
    COLORREF customColor = 0;
    int colorIndex = 0;
    int colorAlpha = 100;
};

inline bool ScreenshotAnnotationDocumentResolveTextDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationTextDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolText) return false;
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    if (ScreenshotAnnotationDocumentItemToolCommand(*item) != ScreenshotToolbarCommand::ToolText) {
        return false;
    }

    const double fontSizeF = item->getDouble(AnnotationProperty::TextFontSize, 0.0);
    out.fontSize = fontSizeF > 0.0
        ? (std::max)(static_cast<int>(std::lround(fontSizeF)), 8)
        : 0;
    out.fontFamily = item->getString(AnnotationProperty::TextFontFamily);
    out.bold = item->getBool(AnnotationProperty::TextBold, false);
    out.italics = item->getBool(AnnotationProperty::TextItalics, false);
    out.background = item->getBool(AnnotationProperty::TextBackground, false);
    out.backgroundColor = item->getColor(AnnotationProperty::TextBackgroundColor, 0);
    out.backgroundOpacity = item->getInt(AnnotationProperty::TextBackgroundOpacity, 0);
    out.backgroundPadding = item->getInt(AnnotationProperty::TextBackgroundPadding, 0);
    out.backgroundRounded = item->getInt(AnnotationProperty::TextBackgroundRounded, 0);
    out.outline = item->getBool(AnnotationProperty::TextOutline, false);
    out.outlineColor = item->getColor(AnnotationProperty::TextOutlineColor, 0);
    out.outlineSize = item->getInt(AnnotationProperty::TextOutlineSize, 0);
    out.usesCustomColor = item->hasProperty(AnnotationProperty::Color);
    if (out.usesCustomColor) out.customColor = item->getColor(AnnotationProperty::Color, 0);
    out.colorIndex =
        (std::min)((std::max)(item->getInt(AnnotationProperty::ColorIndex, 0), 0), 6);
    out.colorAlpha = item->getInt(AnnotationProperty::ColorAlpha, 100);
    return true;
}

inline ScreenshotAnnotationTextDrawStyle
ScreenshotAnnotationTextDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationTextDrawStyle s;
    if (ann.textFontSizeF > 0.0) {
        s.fontSize = (std::max)(static_cast<int>(std::lround(ann.textFontSizeF)), 8);
    } else if (ann.textFontSize > 0) {
        s.fontSize = (std::max)(ann.textFontSize, 8);
    }
    s.fontFamily = ann.textFontFamily;
    s.bold = ann.textBold;
    s.italics = ann.textItalics;
    s.background = ann.textBackground;
    s.backgroundColor = ann.textBackgroundColor;
    s.backgroundOpacity = ann.textBackgroundOpacity;
    s.backgroundPadding = ann.textBackgroundPadding;
    s.backgroundRounded = ann.textBackgroundRounded;
    s.outline = ann.textOutline;
    s.outlineColor = ann.textOutlineColor;
    s.outlineSize = ann.textOutlineSize;
    s.usesCustomColor = ann.hasCustomColor;
    s.customColor = ann.customColor;
    s.colorIndex = ann.colorIndex;
    s.colorAlpha = ann.colorAlpha;
    return s;
}

inline ScreenshotAnnotationTextDrawStyle
ScreenshotAnnotationResolveTextDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationTextDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveTextDrawStyle(document, ann, s)) return s;
    return ScreenshotAnnotationTextDrawStyleFromHost(ann);
}

struct ScreenshotAnnotationWatermarkDrawStyle {
    std::wstring text;
    COLORREF color = 0;
    int opacity = 0;
    int fontSize = 0;
    std::wstring fontFamily;
    bool bold = false;
    bool italics = false;
    int position = 0;
    int gap = 0;
    int angle = 0;
};

inline bool ScreenshotAnnotationDocumentResolveWatermarkDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationWatermarkDrawStyle& out)
{
    if (ann.id.empty()) return false;
    if (ann.type != ScreenshotToolbarCommand::ToolWatermark) return false;
    const ScreenshotAnnotationItem* item = document.findById(ann.id);
    if (!item) return false;

    if (item->role() != AnnotationRole::Watermark &&
        ScreenshotAnnotationDocumentItemToolCommand(*item) != ScreenshotToolbarCommand::ToolWatermark) {
        return false;
    }

    const std::wstring& watermarkText = item->getString(AnnotationProperty::WatermarkText);
    out.text = !watermarkText.empty() ? watermarkText : item->text();
    out.color = item->getColor(AnnotationProperty::WatermarkColor, 0);
    out.opacity = static_cast<int>(item->getDouble(AnnotationProperty::WatermarkOpacity, 0.0));
    out.fontSize = item->getInt(AnnotationProperty::WatermarkFontSize, 0);
    out.fontFamily = item->getString(AnnotationProperty::WatermarkFontFamily);
    out.bold = item->getBool(AnnotationProperty::TextBold, false);
    out.italics = item->getBool(AnnotationProperty::TextItalics, false);
    out.position = item->getInt(AnnotationProperty::WatermarkPosition, 0);
    out.gap = item->getInt(AnnotationProperty::WatermarkGap, 0);
    out.angle = item->getInt(AnnotationProperty::WatermarkAngle, 0);
    return true;
}

inline ScreenshotAnnotationWatermarkDrawStyle
ScreenshotAnnotationWatermarkDrawStyleFromHost(const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationWatermarkDrawStyle s;
    s.text = ann.text;
    s.color = ann.watermarkColor;
    s.opacity = ann.watermarkOpacity;
    s.fontSize = ann.watermarkFontSize;
    s.fontFamily = ann.watermarkFontFamily;
    s.bold = ann.textBold;
    s.italics = ann.textItalics;
    s.position = ann.watermarkPosition;
    s.gap = ann.watermarkGap;
    s.angle = ann.watermarkAngle;
    return s;
}

inline ScreenshotAnnotationWatermarkDrawStyle
ScreenshotAnnotationResolveWatermarkDrawStyle(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann)
{
    ScreenshotAnnotationWatermarkDrawStyle s;
    if (ScreenshotAnnotationDocumentResolveWatermarkDrawStyle(document, ann, s)) return s;
    return ScreenshotAnnotationWatermarkDrawStyleFromHost(ann);
}
