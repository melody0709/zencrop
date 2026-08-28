#include "screenshot/annotation/AnnotationItem.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"
#include <cstdio>

static int g_failures = 0;

#define TEST(name) static void name()
#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::printf("  FAIL: %s:%d\n", __FILE__, __LINE__); \
        g_failures++; \
    } \
} while(0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while(0)
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))
#define ASSERT_STR_EQ(a, b) do { \
    if ((a) != std::wstring(b)) { \
        std::printf("  FAIL: %s:%d: string mismatch\n", __FILE__, __LINE__); \
        g_failures++; \
    } \
} while(0)

// We need ScreenshotToolbarCommand for the test.
// Include OverlayWindow.h to get the enum, but we only use the legacy struct.
#include "OverlayWindow.h"

// The conversion function (defined in AnnotationMigration.h/.cpp)
#include "screenshot/annotation/AnnotationMigration.h"

TEST(test_convert_geometry) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolGeometry;
    legacy.start = {10, 20};
    legacy.end = {100, 200};
    legacy.penWidth = 5;
    legacy.filling = true;
    legacy.colorAlpha = 80;

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Geometry);
    ASSERT_EQ(item.start().x, 10);
    ASSERT_EQ(item.start().y, 20);
    ASSERT_EQ(item.end().x, 100);
    ASSERT_EQ(item.end().y, 200);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 5);
    ASSERT_EQ(item.getBool(AnnotationProperty::Filling), true);
    ASSERT_EQ(item.getInt(AnnotationProperty::ColorAlpha), 80);
}

TEST(test_convert_arrow) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolArrow;
    legacy.arrowShape = 3;
    legacy.penWidth = 10;

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Arrow);
    ASSERT_EQ(item.getInt(AnnotationProperty::LineShape), 3);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 10);
}

TEST(test_convert_broken_line) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolBrokenLine;
    legacy.brokenLineMode = 1;
    legacy.brokenLineArrowEnabled = false;
    legacy.brokenLineStartArrowType = 2;
    legacy.brokenLineEndArrowType = 5;
    legacy.points = { { 10, 10 }, { 40, 20 }, { 70, 10 } };

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::BrokenLine);
    ASSERT_EQ(item.getInt(AnnotationProperty::BrokenLineMode), 1);
    ASSERT_EQ(item.getBool(AnnotationProperty::BrokenLineArrowEnabled), false);
    ASSERT_EQ(item.getInt(AnnotationProperty::BrokenLineStartArrowType), 2);
    ASSERT_EQ(item.getInt(AnnotationProperty::BrokenLineEndArrowType), 5);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::PathPoints), L"10,10;40,20;70,10");

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ((int)restored.points.size(), 3);
    ASSERT_EQ(restored.points[1].x, 40);
    ASSERT_EQ(restored.points[1].y, 20);
}

TEST(test_convert_magnifier) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolMagnifier;
    legacy.magnifierLinkType = 2;
    legacy.magnifierMagnification = 200;
    legacy.magnifierAntiAlias = false;
    legacy.magnifierShadow = true;
    legacy.auxPoint = { 42, 64 };

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::None); // Magnifier has no direct type enum
    ASSERT_EQ(item.role(), AnnotationRole::Magnifier);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierLinkType), 2);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierMagnification), 200);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceX), 42);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceY), 64);
    ASSERT_EQ(item.getBool(AnnotationProperty::MagnifierAntiAlias), false);
    ASSERT_EQ(item.getBool(AnnotationProperty::MagnifierShadow), true);

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.auxPoint.x, 42);
    ASSERT_EQ(restored.auxPoint.y, 64);
}

TEST(test_convert_magnifier_source_rect) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolMagnifier;
    legacy.start = { 30, 40 };
    legacy.end = { 180, 140 };
    legacy.magnifierMagnification = 150;
    ScreenshotMagnifierSetSourceRect(legacy, { 50, 60, 150, 120 });

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceLeft), 50);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceTop), 60);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceRight), 150);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceBottom), 120);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceX), 100);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierSourceY), 90);

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    RECT restoredSource = ScreenshotMagnifierSourceRect(restored);
    ASSERT_EQ(restoredSource.left, 50);
    ASSERT_EQ(restoredSource.top, 60);
    ASSERT_EQ(restoredSource.right, 150);
    ASSERT_EQ(restoredSource.bottom, 120);
    ASSERT_EQ(restored.auxPoint.x, 100);
    ASSERT_EQ(restored.auxPoint.y, 90);
}

TEST(test_convert_text) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolText;
    legacy.start = { 40, 50 };
    legacy.end = { 260, 120 };
    legacy.angle = 27.0;
    legacy.text = L"Hello";
    legacy.textBold = true;
    legacy.textItalics = true;
    legacy.textOutline = true;
    legacy.textOutlineSize = 4;
    legacy.textOutlineColor = RGB(10, 20, 30);
    legacy.textBackground = true;
    legacy.textBackgroundColor = RGB(40, 50, 60);
    legacy.textBackgroundOpacity = 66;
    legacy.textBackgroundRounded = 8;
    legacy.textBackgroundPadding = 12;
    legacy.textFontFamily = L"Arial";
    legacy.textFontSize = 32;

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Text);
    ASSERT_STR_EQ(item.text(), L"Hello");
    ASSERT_EQ(item.getBool(AnnotationProperty::TextBold), true);
    ASSERT_EQ(item.getBool(AnnotationProperty::TextItalics), true);
    ASSERT_EQ(item.getBool(AnnotationProperty::TextOutline), true);
    ASSERT_EQ(item.getInt(AnnotationProperty::TextOutlineSize), 4);
    ASSERT_EQ(item.getColor(AnnotationProperty::TextOutlineColor), RGB(10, 20, 30));
    ASSERT_EQ(item.getBool(AnnotationProperty::TextBackground), true);
    ASSERT_EQ(item.getColor(AnnotationProperty::TextBackgroundColor), RGB(40, 50, 60));
    ASSERT_EQ(item.getInt(AnnotationProperty::TextBackgroundOpacity), 66);
    ASSERT_EQ(item.getInt(AnnotationProperty::TextBackgroundRounded), 8);
    ASSERT_EQ(item.getInt(AnnotationProperty::TextBackgroundPadding), 12);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::TextFontFamily), L"Arial");
    // S-D-1: TextFontSize sole Double (legacy int coerces at migration).
    ASSERT_TRUE(std::abs(item.getDouble(AnnotationProperty::TextFontSize) - 32.0) < 0.01);
    ASSERT_TRUE(std::abs(item.getDouble(AnnotationProperty::Angle) - 27.0) < 0.01);

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.start.x, 40);
    ASSERT_EQ(restored.start.y, 50);
    ASSERT_EQ(restored.end.x, 260);
    ASSERT_EQ(restored.end.y, 120);
    ASSERT_TRUE(std::abs(restored.angle - 27.0) < 0.01);
    ASSERT_EQ(restored.textOutline, true);
    ASSERT_EQ(restored.textOutlineSize, 4);
    ASSERT_EQ(restored.textOutlineColor, RGB(10, 20, 30));
    ASSERT_EQ(restored.textBackground, true);
    ASSERT_EQ(restored.textBackgroundColor, RGB(40, 50, 60));
    ASSERT_EQ(restored.textBackgroundOpacity, 66);
    ASSERT_EQ(restored.textBackgroundRounded, 8);
    ASSERT_EQ(restored.textBackgroundPadding, 12);
    ASSERT_STR_EQ(restored.textFontFamily, L"Arial");
    ASSERT_EQ(restored.textFontSize, 32);
}

TEST(test_convert_highlight) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolHighLight;
    legacy.highLightOpacity = 72;
    legacy.highLightStroke = true;
    legacy.penWidth = 9;
    legacy.highLightStrokeColor = RGB(12, 34, 56);

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.role(), AnnotationRole::HighLight);
    ASSERT_EQ((int)item.getDouble(AnnotationProperty::HighLightOpacity), 72);
    ASSERT_EQ(item.getBool(AnnotationProperty::HighLightStroke), true);
    ASSERT_EQ(item.getInt(AnnotationProperty::HighLightStrokeWidth), 9);
    ASSERT_EQ(item.getColor(AnnotationProperty::HighLightStrokeColor), RGB(12, 34, 56));

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.highLightOpacity, 72);
    ASSERT_EQ(restored.highLightStroke, true);
    ASSERT_EQ(restored.highLightStrokeColor, RGB(12, 34, 56));
}

TEST(test_convert_watermark) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolWatermark;
    legacy.text = L"$yyyy-$MM-$dd";
    legacy.watermarkOpacity = 45;
    legacy.watermarkFontSize = 36;
    legacy.watermarkGap = 80;
    legacy.watermarkAngle = -30;
    legacy.watermarkFontFamily = L"Segoe UI";
    legacy.watermarkPosition = 0;
    legacy.watermarkColor = RGB(12, 34, 56);

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.role(), AnnotationRole::Watermark);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::WatermarkText), L"$yyyy-$MM-$dd");
    ASSERT_EQ(item.getColor(AnnotationProperty::WatermarkColor), RGB(12, 34, 56));
    ASSERT_EQ((int)item.getDouble(AnnotationProperty::WatermarkOpacity), 45);
    ASSERT_EQ(item.getInt(AnnotationProperty::WatermarkFontSize), 36);
    ASSERT_EQ(item.getInt(AnnotationProperty::WatermarkGap), 80);
    ASSERT_EQ(item.getInt(AnnotationProperty::WatermarkAngle), -30);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::WatermarkFontFamily), L"Segoe UI");
    ASSERT_EQ(item.getInt(AnnotationProperty::WatermarkPosition), 0);

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_STR_EQ(restored.text, L"$yyyy-$MM-$dd");
    ASSERT_EQ(restored.watermarkOpacity, 45);
    ASSERT_EQ(restored.watermarkFontSize, 36);
    ASSERT_EQ(restored.watermarkGap, 80);
    ASSERT_EQ(restored.watermarkAngle, -30);
    ASSERT_STR_EQ(restored.watermarkFontFamily, L"Segoe UI");
    ASSERT_EQ(restored.watermarkPosition, 0);
    ASSERT_EQ(restored.watermarkColor, RGB(12, 34, 56));
}

TEST(test_convert_empty_watermark) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolWatermark;
    legacy.text = L"";
    legacy.watermarkOpacity = 45;
    legacy.watermarkFontSize = 36;
    legacy.watermarkGap = 80;
    legacy.watermarkAngle = -30;
    legacy.watermarkFontFamily = L"Segoe UI";
    legacy.watermarkPosition = 0;
    legacy.watermarkColor = RGB(12, 34, 56);

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.role(), AnnotationRole::Watermark);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::WatermarkText), L"");

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_STR_EQ(restored.text, L"");
    ASSERT_EQ(restored.watermarkOpacity, 45);
    ASSERT_EQ(restored.watermarkFontSize, 36);
    ASSERT_EQ(restored.watermarkGap, 80);
    ASSERT_EQ(restored.watermarkAngle, -30);
    ASSERT_STR_EQ(restored.watermarkFontFamily, L"Segoe UI");
    ASSERT_EQ(restored.watermarkPosition, 0);
    ASSERT_EQ(restored.watermarkColor, RGB(12, 34, 56));
}

TEST(test_convert_serial) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolSerial;
    legacy.start = { 120, 140 };
    legacy.end = { 168, 188 };
    legacy.angle = -32.0;
    legacy.serialNumber = 5;

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Serial);
    ASSERT_EQ(item.getInt(AnnotationProperty::SerialIndex), 5);
    ASSERT_TRUE(std::abs(item.getDouble(AnnotationProperty::Angle) + 32.0) < 0.01);

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.start.x, 120);
    ASSERT_EQ(restored.start.y, 140);
    ASSERT_EQ(restored.end.x, 168);
    ASSERT_EQ(restored.end.y, 188);
    ASSERT_TRUE(std::abs(restored.angle + 32.0) < 0.01);
}

TEST(test_convert_custom_color) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolGeometry;
    legacy.hasCustomColor = true;
    legacy.customColor = RGB(255, 0, 0);

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.getColor(AnnotationProperty::Color), RGB(255, 0, 0));
}

TEST(test_convert_ellipse) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolGeometry;
    legacy.ellipse = true;

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.getInt(AnnotationProperty::PathMode), 3);
}

TEST(test_convert_marker_path_mode) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolMarker;
    legacy.pathMode = 2;

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Marker);
    ASSERT_EQ(item.getInt(AnnotationProperty::PathMode), 2);

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.pathMode, 2);
}

TEST(test_convert_marker_free_path_round_trip) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolMarker;
    legacy.penWidth = 11;
    legacy.pathMode = 1;
    legacy.points = { { 4, 8 }, { 12, 16 }, { 28, 32 } };

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Marker);
    ASSERT_EQ(item.getInt(AnnotationProperty::PathMode), 1);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::PathPoints), L"4,8;12,16;28,32");

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.type, ScreenshotToolbarCommand::ToolMarker);
    ASSERT_EQ(restored.pathMode, 1);
    ASSERT_EQ((int)restored.points.size(), 3);
    ASSERT_EQ(restored.points[2].x, 28);
    ASSERT_EQ(restored.points[2].y, 32);
}

TEST(test_convert_pencil_free_path_round_trip) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolPencil;
    legacy.penWidth = 7;
    legacy.points = { { 10, 12 }, { 20, 24 }, { 45, 40 }, { 70, 58 } };

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Pencil);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 7);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::PathPoints), L"10,12;20,24;45,40;70,58");

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.type, ScreenshotToolbarCommand::ToolPencil);
    ASSERT_EQ(restored.penWidth, 7);
    ASSERT_EQ((int)restored.points.size(), 4);
    ASSERT_EQ(restored.points[3].x, 70);
    ASSERT_EQ(restored.points[3].y, 58);
}

TEST(test_convert_mosaic_free_path_round_trip) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolMosaic;
    legacy.penWidth = 16;
    legacy.pathMode = 1;
    legacy.points = { { 5, 6 }, { 20, 24 }, { 48, 52 } };

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Mosaic);
    ASSERT_EQ(item.getInt(AnnotationProperty::PathMode), 1);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::PathPoints), L"5,6;20,24;48,52");

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.type, ScreenshotToolbarCommand::ToolMosaic);
    ASSERT_EQ(restored.pathMode, 1);
    ASSERT_EQ((int)restored.points.size(), 3);
    ASSERT_EQ(restored.points[1].x, 20);
    ASSERT_EQ(restored.points[1].y, 24);
}

TEST(test_convert_eraser_free_path_round_trip) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolEraser;
    legacy.start = { 10, 20 };
    legacy.end = { 80, 90 };
    legacy.penWidth = 14;
    legacy.pathMode = 1;
    legacy.points = { { 10, 20 }, { 25, 30 }, { 50, 42 }, { 80, 90 } };

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Eraser);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 14);
    ASSERT_EQ(item.getInt(AnnotationProperty::PathMode), 1);
    ASSERT_STR_EQ(item.getString(AnnotationProperty::PathPoints), L"10,20;25,30;50,42;80,90");

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.type, ScreenshotToolbarCommand::ToolEraser);
    ASSERT_EQ(restored.penWidth, 14);
    ASSERT_EQ(restored.pathMode, 1);
    ASSERT_EQ((int)restored.points.size(), 4);
    ASSERT_EQ(restored.points[2].x, 50);
    ASSERT_EQ(restored.points[2].y, 42);
}

TEST(test_convert_eraser_rotated_rect_round_trip) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolEraser;
    legacy.start = { 30, 40 };
    legacy.end = { 130, 120 };
    legacy.penWidth = 18;
    legacy.pathMode = 2;
    legacy.angle = 37.5;

    auto item = convertLegacyAnnotation(legacy);
    ASSERT_EQ(item.type(), AnnotationType::Eraser);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 18);
    ASSERT_EQ(item.getInt(AnnotationProperty::PathMode), 2);
    ASSERT_EQ((int)item.getDouble(AnnotationProperty::Angle), 37);

    auto restored = LegacyAnnotationFromSnapshot(item.takeSnapshot(), item.id());
    ASSERT_EQ(restored.type, ScreenshotToolbarCommand::ToolEraser);
    ASSERT_EQ(restored.pathMode, 2);
    ASSERT_EQ(restored.penWidth, 18);
    ASSERT_TRUE(restored.angle > 37.4 && restored.angle < 37.6);
}

TEST(test_deterministic_id_with_index) {
    ScreenshotAnnotation legacy;
    legacy.type = ScreenshotToolbarCommand::ToolGeometry;
    legacy.start = {10, 20};
    legacy.end = {100, 200};

    // Same index should produce same ID
    auto item1 = convertLegacyAnnotation(legacy, 5);
    auto item2 = convertLegacyAnnotation(legacy, 5);
    ASSERT_TRUE(item1.id() == item2.id());
    ASSERT_TRUE(item1.id() == L"legacy_5");

    // Different index should produce different ID
    auto item3 = convertLegacyAnnotation(legacy, 7);
    ASSERT_TRUE(item3.id() == L"legacy_7");
    ASSERT_FALSE(item1.id() == item3.id());

    // No index should produce auto-generated ID (different each time)
    auto item4 = convertLegacyAnnotation(legacy);
    auto item5 = convertLegacyAnnotation(legacy);
    ASSERT_FALSE(item4.id() == item5.id());
}

int main() {
    std::printf("Running migration bridge tests...\n");

    test_convert_geometry();
    test_convert_arrow();
    test_convert_broken_line();
    test_convert_magnifier();
    test_convert_magnifier_source_rect();
    test_convert_text();
    test_convert_highlight();
    test_convert_watermark();
    test_convert_empty_watermark();
    test_convert_serial();
    test_convert_custom_color();
    test_convert_ellipse();
    test_convert_marker_path_mode();
    test_convert_marker_free_path_round_trip();
    test_convert_pencil_free_path_round_trip();
    test_convert_mosaic_free_path_round_trip();
    test_convert_eraser_free_path_round_trip();
    test_convert_eraser_rotated_rect_round_trip();
    test_deterministic_id_with_index();

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    } else {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
}
