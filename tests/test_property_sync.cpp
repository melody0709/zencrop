#include "screenshot/annotation/AnnotationHistory.h"
#include "screenshot/annotation/AnnotationModel.h"
#include "screenshot/annotation/AnnotationMigration.h"
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

#include "OverlayWindow.h"

TEST(test_apply_style_pushes_modify_to_history) {
    // Simulate: create annotation, modify its properties, verify history
    ScreenshotAnnotation ann;
    ann.type = ScreenshotToolbarCommand::ToolGeometry;
    ann.start = {10, 20};
    ann.end = {100, 200};
    ann.penWidth = 5;
    ann.filling = false;

    auto beforeItem = convertLegacyAnnotation(ann);
    AnnotationSnapshot beforeSnap = beforeItem.takeSnapshot();

    // Simulate ApplyActiveScreenshotStyleToSelection changing penWidth
    ann.penWidth = 10;
    ann.filling = true;

    auto afterItem = convertLegacyAnnotation(ann);
    AnnotationSnapshot afterSnap = afterItem.takeSnapshot();

    // Push to history
    AnnotationHistory history;
    history.pushModify(afterItem.id(), beforeSnap, afterSnap);

    ASSERT_TRUE(history.canUndo());
    auto entry = history.undo();
    ASSERT_EQ(entry.type, AnnotationCommandType::Modify);
    ASSERT_EQ(entry.before.getInt(AnnotationProperty::PenWidth), 5);
    ASSERT_EQ(entry.after.getInt(AnnotationProperty::PenWidth), 10);
    ASSERT_EQ(entry.before.getBool(AnnotationProperty::Filling), false);
    ASSERT_EQ(entry.after.getBool(AnnotationProperty::Filling), true);
}

TEST(test_property_bridge_reads_correctly) {
    ScreenshotAnnotation ann;
    ann.type = ScreenshotToolbarCommand::ToolBrokenLine;
    ann.brokenLineMode = 1;
    ann.brokenLineArrowEnabled = false;
    ann.brokenLineStartArrowType = 3;
    ann.brokenLineEndArrowType = 7;

    auto item = convertLegacyAnnotation(ann);
    ASSERT_EQ(item.getInt(AnnotationProperty::BrokenLineMode), 1);
    ASSERT_EQ(item.getBool(AnnotationProperty::BrokenLineArrowEnabled), false);
    ASSERT_EQ(item.getInt(AnnotationProperty::BrokenLineStartArrowType), 3);
    ASSERT_EQ(item.getInt(AnnotationProperty::BrokenLineEndArrowType), 7);
}

TEST(test_snapshot_restores_properties) {
    ScreenshotAnnotation ann;
    ann.type = ScreenshotToolbarCommand::ToolGeometry;
    ann.penWidth = 5;
    ann.roundedRadius = 10;
    ann.filling = true;

    auto item = convertLegacyAnnotation(ann);
    AnnotationSnapshot snap = item.takeSnapshot();

    // Modify
    item.setInt(AnnotationProperty::PenWidth, 20);
    item.setBool(AnnotationProperty::Filling, false);

    // Restore
    item.restoreFromSnapshot(snap);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 5);
    ASSERT_EQ(item.getInt(AnnotationProperty::RectRoundRadius), 10);
    ASSERT_EQ(item.getBool(AnnotationProperty::Filling), true);
}

TEST(test_magnifier_properties_roundtrip) {
    ScreenshotAnnotation ann;
    ann.type = ScreenshotToolbarCommand::ToolMagnifier;
    ann.magnifierLinkType = 2;
    ann.magnifierMagnification = 250;
    ann.magnifierAntiAlias = false;
    ann.magnifierShadow = true;
    ann.magnifierEraseMark = true;

    auto item = convertLegacyAnnotation(ann);
    ASSERT_EQ(item.role(), AnnotationRole::Magnifier);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierLinkType), 2);
    ASSERT_EQ(item.getInt(AnnotationProperty::MagnifierMagnification), 250);
    ASSERT_EQ(item.getBool(AnnotationProperty::MagnifierAntiAlias), false);
    ASSERT_EQ(item.getBool(AnnotationProperty::MagnifierShadow), true);
    ASSERT_EQ(item.getBool(AnnotationProperty::MagnifierEraseMark), true);
}

TEST(test_text_watermark_highlight_properties_roundtrip) {
    ScreenshotAnnotation textAnn;
    textAnn.type = ScreenshotToolbarCommand::ToolText;
    textAnn.textOutline = true;
    textAnn.textOutlineSize = 5;
    textAnn.textOutlineColor = RGB(1, 2, 3);
    textAnn.textBackground = true;
    textAnn.textBackgroundColor = RGB(4, 5, 6);
    textAnn.textBackgroundOpacity = 70;
    textAnn.textBackgroundRounded = 9;
    textAnn.textBackgroundPadding = 11;

    auto textItem = convertLegacyAnnotation(textAnn);
    AnnotationSnapshot textSnap = textItem.takeSnapshot();
    auto restoredText = LegacyAnnotationFromSnapshot(textSnap, textItem.id());
    ASSERT_EQ(restoredText.textOutline, true);
    ASSERT_EQ(restoredText.textOutlineSize, 5);
    ASSERT_EQ(restoredText.textOutlineColor, RGB(1, 2, 3));
    ASSERT_EQ(restoredText.textBackground, true);
    ASSERT_EQ(restoredText.textBackgroundColor, RGB(4, 5, 6));
    ASSERT_EQ(restoredText.textBackgroundOpacity, 70);
    ASSERT_EQ(restoredText.textBackgroundRounded, 9);
    ASSERT_EQ(restoredText.textBackgroundPadding, 11);

    ScreenshotAnnotation watermarkAnn;
    watermarkAnn.type = ScreenshotToolbarCommand::ToolWatermark;
    watermarkAnn.text = L"wm";
    watermarkAnn.watermarkOpacity = 33;
    watermarkAnn.watermarkFontSize = 28;
    watermarkAnn.watermarkGap = 44;
    watermarkAnn.watermarkAngle = 15;
    watermarkAnn.watermarkFontFamily = L"Arial";
    watermarkAnn.watermarkPosition = 7;
    watermarkAnn.watermarkColor = RGB(10, 20, 30);

    auto watermarkItem = convertLegacyAnnotation(watermarkAnn);
    auto restoredWatermark = LegacyAnnotationFromSnapshot(watermarkItem.takeSnapshot(), watermarkItem.id());
    ASSERT_EQ(restoredWatermark.watermarkColor, RGB(10, 20, 30));
    ASSERT_EQ(restoredWatermark.watermarkOpacity, 33);
    ASSERT_EQ(restoredWatermark.watermarkFontSize, 28);
    ASSERT_EQ(restoredWatermark.watermarkGap, 44);
    ASSERT_EQ(restoredWatermark.watermarkAngle, 15);
    ASSERT_EQ(restoredWatermark.watermarkPosition, 7);

    ScreenshotAnnotation highlightAnn;
    highlightAnn.type = ScreenshotToolbarCommand::ToolHighLight;
    highlightAnn.highLightOpacity = 62;
    highlightAnn.highLightStroke = true;
    highlightAnn.highLightStrokeColor = RGB(7, 8, 9);

    auto highlightItem = convertLegacyAnnotation(highlightAnn);
    auto restoredHighlight = LegacyAnnotationFromSnapshot(highlightItem.takeSnapshot(), highlightItem.id());
    ASSERT_EQ(restoredHighlight.highLightOpacity, 62);
    ASSERT_EQ(restoredHighlight.highLightStroke, true);
    ASSERT_EQ(restoredHighlight.highLightStrokeColor, RGB(7, 8, 9));
}

int main() {
    std::printf("Running property sync tests...\n");

    test_apply_style_pushes_modify_to_history();
    test_property_bridge_reads_correctly();
    test_snapshot_restores_properties();
    test_magnifier_properties_roundtrip();
    test_text_watermark_highlight_properties_roundtrip();

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    } else {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
}
