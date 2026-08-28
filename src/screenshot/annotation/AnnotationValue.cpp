#include "screenshot/annotation/AnnotationValue.h"

// S-D-1: sole property-kind schema (GetPropertyType is thin adapter).
// One AnnotationProperty key maps to exactly one value kind.

AnnotationValueKind GetAnnotationPropertyKind(AnnotationProperty p) {
    switch (p) {
    // Color
    case AnnotationProperty::Color:
    case AnnotationProperty::HighLightStrokeColor:
    case AnnotationProperty::TextOutlineColor:
    case AnnotationProperty::TextBackgroundColor:
    case AnnotationProperty::WatermarkColor:
        return AnnotationValueKind::Color;

    // Bool
    case AnnotationProperty::Filling:
    case AnnotationProperty::BrokenLineArrowEnabled:
    case AnnotationProperty::MagnifierAntiAlias:
    case AnnotationProperty::MagnifierEraseMark:
    case AnnotationProperty::MagnifierShadow:
    case AnnotationProperty::HighLightStroke:
    case AnnotationProperty::TextBold:
    case AnnotationProperty::TextItalics:
    case AnnotationProperty::TextOutline:
    case AnnotationProperty::TextBackground:
    case AnnotationProperty::EmbeddedTextOutline:
        return AnnotationValueKind::Bool;

    // Double (incl. TextFontSize sole double; int legacy path converts at migration)
    case AnnotationProperty::Angle:
    case AnnotationProperty::HighLightOpacity:
    case AnnotationProperty::TextFontSize:
    case AnnotationProperty::EmbeddedTextFontSize:
    case AnnotationProperty::WatermarkOpacity:
        return AnnotationValueKind::Double;

    // String
    case AnnotationProperty::PathPoints:
    case AnnotationProperty::TextFontFamily:
    case AnnotationProperty::EmbeddedTextFontFamily:
    case AnnotationProperty::WatermarkFontFamily:
    case AnnotationProperty::WatermarkText:
        return AnnotationValueKind::String;

    // Int (indices, widths, modes, opacities-as-int, etc.)
    default:
        return AnnotationValueKind::Int;
    }
}

bool AnnotationValueMatchesProperty(AnnotationProperty p, const AnnotationValue& v) {
    const auto kind = GetAnnotationPropertyKind(p);
    switch (kind) {
    case AnnotationValueKind::Int: return std::holds_alternative<int>(v);
    case AnnotationValueKind::Bool: return std::holds_alternative<bool>(v);
    case AnnotationValueKind::Double: return std::holds_alternative<double>(v);
    case AnnotationValueKind::Color: return std::holds_alternative<COLORREF>(v);
    case AnnotationValueKind::String: return std::holds_alternative<std::wstring>(v);
    default: return false;
    }
}
