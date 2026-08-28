#include "screenshot/annotation/AnnotationValue.h"
#include "screenshot/annotation/AnnotationProperty.h"
#include "screenshot/annotation/AnnotationItem.h"
#include <cmath>
#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static PropertyValueType KindToPropertyType(AnnotationValueKind k) {
    switch (k) {
    case AnnotationValueKind::Bool: return PropertyValueType::Bool;
    case AnnotationValueKind::Double: return PropertyValueType::Double;
    case AnnotationValueKind::Color: return PropertyValueType::Color;
    case AnnotationValueKind::String: return PropertyValueType::String;
    case AnnotationValueKind::Int:
    default: return PropertyValueType::Int;
    }
}

int main() {
    // Spot-check a representative set of properties for stable kinds.
    const AnnotationProperty props[] = {
        AnnotationProperty::Color,
        AnnotationProperty::PenWidth,
        AnnotationProperty::Filling,
        AnnotationProperty::Angle,
        AnnotationProperty::PathPoints,
        AnnotationProperty::TextBold,
        AnnotationProperty::TextFontFamily,
        AnnotationProperty::MagnifierMagnification,
        AnnotationProperty::HighLightOpacity,
        AnnotationProperty::SerialIndex,
        AnnotationProperty::TextFontSize,
        AnnotationProperty::WatermarkOpacity,
        AnnotationProperty::WatermarkColor,
        AnnotationProperty::WatermarkFontFamily,
        AnnotationProperty::EmbeddedTextFontSize,
    };
    for (auto p : props) {
        Expect(GetAnnotationPropertyKind(p) != AnnotationValueKind::Unknown, "kind known");
        // S-D-1: GetPropertyType thin adapter must mirror sole schema.
        Expect(GetPropertyType(p) == KindToPropertyType(GetAnnotationPropertyKind(p)),
            "adapter parity");
    }

    Expect(GetAnnotationPropertyKind(AnnotationProperty::TextFontSize) == AnnotationValueKind::Double,
        "font size double sole");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::HighLightOpacity) == AnnotationValueKind::Double,
        "hl opacity double");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::WatermarkOpacity) == AnnotationValueKind::Double,
        "wm opacity double");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::WatermarkColor) == AnnotationValueKind::Color,
        "wm color");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::WatermarkFontFamily) == AnnotationValueKind::String,
        "wm font family");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::EmbeddedTextFontSize) == AnnotationValueKind::Double,
        "embedded font size double");

    // Same key cannot match two different kinds at once.
    AnnotationValue i = 1;
    AnnotationValue b = true;
    AnnotationValue d = 32.0;
    Expect(AnnotationValueMatchesProperty(AnnotationProperty::PenWidth, i), "pen int");
    Expect(!AnnotationValueMatchesProperty(AnnotationProperty::PenWidth, b), "pen not bool");
    Expect(AnnotationValueMatchesProperty(AnnotationProperty::TextBold, b), "bold bool");
    Expect(!AnnotationValueMatchesProperty(AnnotationProperty::TextBold, i), "bold not int");
    Expect(AnnotationValueMatchesProperty(AnnotationProperty::TextFontSize, d), "font size double match");
    Expect(!AnnotationValueMatchesProperty(AnnotationProperty::TextFontSize, i), "font size not int");

    // S-D-1: single-type-per-key — setInt on Double schema coerces + clears int map.
    {
        ScreenshotAnnotationItem item(AnnotationType::Text);
        item.setInt(AnnotationProperty::TextFontSize, 32);
        Expect(item.hasProperty(AnnotationProperty::TextFontSize), "has font size after int set");
        Expect(std::fabs(item.getDouble(AnnotationProperty::TextFontSize) - 32.0) < 1e-9,
            "int set coerces to double");
        Expect(item.getInt(AnnotationProperty::TextFontSize, -1) == -1,
            "int map cleared after double coerce");

        item.setDouble(AnnotationProperty::TextFontSize, 26.98);
        Expect(std::fabs(item.getDouble(AnnotationProperty::TextFontSize) - 26.98) < 1e-9,
            "double set wins");
        Expect(item.getInt(AnnotationProperty::TextFontSize, -1) == -1,
            "int map still empty after double set");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
