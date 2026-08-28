#include "OverlayWindow.h"
#include "screenshot/annotation/AnnotationProperty.h"
#include <cstdio>

static int g_failures = 0;

#define TEST(name) static void name()
#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::printf("  FAIL: %s:%d: expected %d, got %d\n", __FILE__, __LINE__, (int)(b), (int)(a)); \
        g_failures++; \
    } \
} while(0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while(0)

// --- Property enum exists and has expected entries ---

TEST(test_common_properties_exist) {
    // These must exist for all tools
    ASSERT_TRUE((int)AnnotationProperty::Color >= 0);
    ASSERT_TRUE((int)AnnotationProperty::PenWidth >= 0);
    ASSERT_TRUE((int)AnnotationProperty::PenStyle >= 0);
    ASSERT_TRUE((int)AnnotationProperty::PathMode >= 0);
    ASSERT_TRUE((int)AnnotationProperty::RectRoundRadius >= 0);
    ASSERT_TRUE((int)AnnotationProperty::Filling >= 0);
    ASSERT_TRUE((int)AnnotationProperty::LineShape >= 0);
}

TEST(test_tool_specific_properties_exist) {
    ASSERT_TRUE((int)AnnotationProperty::MarkerBlendMode >= 0);
    ASSERT_TRUE((int)AnnotationProperty::MosaicMode >= 0);
    ASSERT_TRUE((int)AnnotationProperty::MosaicStrength >= 0);
    ASSERT_TRUE((int)AnnotationProperty::SerialType >= 0);
    ASSERT_TRUE((int)AnnotationProperty::BrokenLineMode >= 0);
    ASSERT_TRUE((int)AnnotationProperty::BrokenLineArrowEnabled >= 0);
    ASSERT_TRUE((int)AnnotationProperty::BrokenLineStartArrowType >= 0);
    ASSERT_TRUE((int)AnnotationProperty::BrokenLineEndArrowType >= 0);
}

TEST(test_magnifier_properties_exist) {
    ASSERT_TRUE((int)AnnotationProperty::MagnifierLinkType >= 0);
    ASSERT_TRUE((int)AnnotationProperty::MagnifierMagnification >= 0);
    ASSERT_TRUE((int)AnnotationProperty::MagnifierAntiAlias >= 0);
    ASSERT_TRUE((int)AnnotationProperty::MagnifierEraseMark >= 0);
    ASSERT_TRUE((int)AnnotationProperty::MagnifierShadow >= 0);
}

TEST(test_highlight_properties_exist) {
    ASSERT_TRUE((int)AnnotationProperty::HighLightOpacity >= 0);
    ASSERT_TRUE((int)AnnotationProperty::HighLightStroke >= 0);
    ASSERT_TRUE((int)AnnotationProperty::HighLightStrokeWidth >= 0);
    ASSERT_TRUE((int)AnnotationProperty::HighLightStrokeColor >= 0);
}

TEST(test_text_properties_exist) {
    ASSERT_TRUE((int)AnnotationProperty::TextBold >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextItalics >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextOutline >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextOutlineSize >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextOutlineColor >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextBackground >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextBackgroundColor >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextBackgroundOpacity >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextBackgroundRounded >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextBackgroundPadding >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextFontFamily >= 0);
    ASSERT_TRUE((int)AnnotationProperty::TextFontSize >= 0);
}

TEST(test_serial_properties_exist) {
    ASSERT_TRUE((int)AnnotationProperty::SerialIndex >= 0);
    ASSERT_TRUE((int)AnnotationProperty::SerialStyleType >= 0);
    ASSERT_TRUE((int)AnnotationProperty::EmbeddedTextFontSize >= 0);
    ASSERT_TRUE((int)AnnotationProperty::EmbeddedTextOutline >= 0);
    ASSERT_TRUE((int)AnnotationProperty::EmbeddedTextFontFamily >= 0);
}

TEST(test_watermark_properties_exist) {
    ASSERT_TRUE((int)AnnotationProperty::WatermarkText >= 0);
    ASSERT_TRUE((int)AnnotationProperty::WatermarkColor >= 0);
    ASSERT_TRUE((int)AnnotationProperty::WatermarkOpacity >= 0);
    ASSERT_TRUE((int)AnnotationProperty::WatermarkFontSize >= 0);
    ASSERT_TRUE((int)AnnotationProperty::WatermarkGap >= 0);
    ASSERT_TRUE((int)AnnotationProperty::WatermarkAngle >= 0);
    ASSERT_TRUE((int)AnnotationProperty::WatermarkFontFamily >= 0);
    ASSERT_TRUE((int)AnnotationProperty::WatermarkPosition >= 0);
}

TEST(test_color_alpha_exists) {
    ASSERT_TRUE((int)AnnotationProperty::ColorAlpha >= 0);
}

TEST(test_has_count) {
    ASSERT_TRUE((int)AnnotationProperty::_Count > 0);
}

// --- PropertyType categorization ---

TEST(test_property_type_categorization) {
    ASSERT_EQ(GetPropertyType(AnnotationProperty::Color), PropertyValueType::Color);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::PenWidth), PropertyValueType::Int);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::Filling), PropertyValueType::Bool);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::PathMode), PropertyValueType::Int);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::TextFontFamily), PropertyValueType::String);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::HighLightStrokeColor), PropertyValueType::Color);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::HighLightOpacity), PropertyValueType::Double);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::WatermarkText), PropertyValueType::String);
    ASSERT_EQ(GetPropertyType(AnnotationProperty::WatermarkColor), PropertyValueType::Color);
}

int main() {
    std::printf("Running AnnotationProperty tests...\n");

    test_common_properties_exist();
    test_tool_specific_properties_exist();
    test_magnifier_properties_exist();
    test_highlight_properties_exist();
    test_text_properties_exist();
    test_serial_properties_exist();
    test_watermark_properties_exist();
    test_color_alpha_exists();
    test_has_count();
    test_property_type_categorization();

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    } else {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
}
