#include "OverlayWindow.h"
#include "screenshot/annotation/AnnotationTypes.h"
#include <cstdio>
#include <cstring>

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

// --- AnnotationType: serialized values must remain stable (0-10) ---

TEST(test_annotation_type_values) {
    ASSERT_EQ((int)AnnotationType::None, 0);
    ASSERT_EQ((int)AnnotationType::Pencil, 1);
    ASSERT_EQ((int)AnnotationType::Geometry, 2);
    ASSERT_EQ((int)AnnotationType::BrokenLine, 3);
    ASSERT_EQ((int)AnnotationType::Arrow, 4);
    ASSERT_EQ((int)AnnotationType::Marker, 5);
    ASSERT_EQ((int)AnnotationType::Mosaic, 6);
    ASSERT_EQ((int)AnnotationType::Text, 7);
    ASSERT_EQ((int)AnnotationType::Eraser, 8);
    ASSERT_EQ((int)AnnotationType::Image, 9);
    ASSERT_EQ((int)AnnotationType::Serial, 10);
}

TEST(test_annotation_type_count) {
    ASSERT_EQ((int)AnnotationType::_Count, 11);
}

// --- AnnotationRole: behavior controller layer ---

TEST(test_annotation_role_has_expected_entries) {
    ASSERT_EQ((int)AnnotationRole::Default, 0);
    ASSERT_TRUE((int)AnnotationRole::HighLight > 0);
    ASSERT_TRUE((int)AnnotationRole::Magnifier > 0);
    ASSERT_TRUE((int)AnnotationRole::Watermark > 0);
    ASSERT_TRUE((int)AnnotationRole::AutoMosaicRect > 0);
    ASSERT_TRUE((int)AnnotationRole::AutoMosaicPath > 0);
}

// --- AnnotationCommandType: for undo/redo ---

TEST(test_command_type_values) {
    ASSERT_EQ((int)AnnotationCommandType::Create, 0);
    ASSERT_EQ((int)AnnotationCommandType::Modify, 1);
    ASSERT_EQ((int)AnnotationCommandType::Delete, 2);
}

// --- AnnotationType <-> ScreenshotToolbarCommand mapping ---

TEST(test_type_to_toolbar_command_mapping) {
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Geometry),
              ScreenshotToolbarCommand::ToolGeometry);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Arrow),
              ScreenshotToolbarCommand::ToolArrow);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::BrokenLine),
              ScreenshotToolbarCommand::ToolBrokenLine);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Pencil),
              ScreenshotToolbarCommand::ToolPencil);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Marker),
              ScreenshotToolbarCommand::ToolMarker);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Mosaic),
              ScreenshotToolbarCommand::ToolMosaic);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Text),
              ScreenshotToolbarCommand::ToolText);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Eraser),
              ScreenshotToolbarCommand::ToolEraser);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Serial),
              ScreenshotToolbarCommand::ToolSerial);
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::Image),
              ScreenshotToolbarCommand::Confirm); // no direct tool
    ASSERT_EQ(AnnotationTypeToToolCommand(AnnotationType::None),
              ScreenshotToolbarCommand::Confirm);
}

TEST(test_tool_command_to_type_mapping) {
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolGeometry),
              AnnotationType::Geometry);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolArrow),
              AnnotationType::Arrow);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolBrokenLine),
              AnnotationType::BrokenLine);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolPencil),
              AnnotationType::Pencil);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolMarker),
              AnnotationType::Marker);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolMosaic),
              AnnotationType::Mosaic);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolText),
              AnnotationType::Text);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolEraser),
              AnnotationType::Eraser);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::ToolSerial),
              AnnotationType::Serial);
    ASSERT_EQ(ToolCommandToAnnotationType(ScreenshotToolbarCommand::Confirm),
              AnnotationType::None);
}

// --- AnnotationType is_valid ---

TEST(test_is_valid_annotation_type) {
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::None));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Pencil));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Geometry));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::BrokenLine));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Arrow));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Marker));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Mosaic));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Text));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Eraser));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Image));
    ASSERT_TRUE(IsValidAnnotationType(AnnotationType::Serial));
}

int main() {
    std::printf("Running annotation enum tests...\n");

    test_annotation_type_values();
    test_annotation_type_count();
    test_annotation_role_has_expected_entries();
    test_command_type_values();
    test_type_to_toolbar_command_mapping();
    test_tool_command_to_type_mapping();
    test_is_valid_annotation_type();

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    } else {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
}
