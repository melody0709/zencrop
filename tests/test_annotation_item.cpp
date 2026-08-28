#include "screenshot/annotation/AnnotationItem.h"
#include <cstdio>
#include <cmath>

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
#define ASSERT_STR_EQ(a, b) do { \
    if ((a) != std::wstring(b)) { \
        std::printf("  FAIL: %s:%d: string mismatch\n", __FILE__, __LINE__); \
        g_failures++; \
    } \
} while(0)

// --- Construction ---

TEST(test_item_has_unique_id) {
    ScreenshotAnnotationItem a(AnnotationType::Geometry);
    ScreenshotAnnotationItem b(AnnotationType::Geometry);
    ASSERT_TRUE(a.id() != b.id());
    ASSERT_TRUE(!a.id().empty());
}

TEST(test_item_has_type) {
    ScreenshotAnnotationItem item(AnnotationType::Arrow);
    ASSERT_EQ(item.type(), AnnotationType::Arrow);
}

TEST(test_item_default_role_is_default) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    ASSERT_EQ(item.role(), AnnotationRole::Default);
}

TEST(test_item_can_set_role) {
    ScreenshotAnnotationItem item(AnnotationType::Mosaic);
    item.setRole(AnnotationRole::AutoMosaicRect);
    ASSERT_EQ(item.role(), AnnotationRole::AutoMosaicRect);
}

// --- Properties ---

TEST(test_item_default_properties) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    // Default pen width should be 0 (unset)
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 0);
    ASSERT_EQ(item.getBool(AnnotationProperty::Filling), false);
}

TEST(test_item_set_get_int_property) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setInt(AnnotationProperty::PenWidth, 5);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 5);
}

TEST(test_item_set_get_bool_property) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setBool(AnnotationProperty::Filling, true);
    ASSERT_EQ(item.getBool(AnnotationProperty::Filling), true);
}

TEST(test_item_set_get_color_property) {
    ScreenshotAnnotationItem item(AnnotationType::Arrow);
    item.setColor(AnnotationProperty::Color, RGB(255, 0, 0));
    ASSERT_EQ(item.getColor(AnnotationProperty::Color), RGB(255, 0, 0));
}

TEST(test_item_set_get_double_property) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setDouble(AnnotationProperty::HighLightOpacity, 68.0);
    ASSERT_TRUE(std::abs(item.getDouble(AnnotationProperty::HighLightOpacity) - 68.0) < 0.01);
}

TEST(test_item_set_get_string_property) {
    ScreenshotAnnotationItem item(AnnotationType::Text);
    item.setString(AnnotationProperty::TextFontFamily, L"Segoe UI");
    ASSERT_STR_EQ(item.getString(AnnotationProperty::TextFontFamily), L"Segoe UI");
}

TEST(test_item_has_property_returns_false_by_default) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    ASSERT_EQ(item.hasProperty(AnnotationProperty::PenWidth), false);
}

TEST(test_item_has_property_returns_true_after_set) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setInt(AnnotationProperty::PenWidth, 5);
    ASSERT_EQ(item.hasProperty(AnnotationProperty::PenWidth), true);
}

// --- Geometry ---

TEST(test_item_start_end_points) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setStart({10, 20});
    item.setEnd({100, 200});
    ASSERT_EQ(item.start().x, 10);
    ASSERT_EQ(item.start().y, 20);
    ASSERT_EQ(item.end().x, 100);
    ASSERT_EQ(item.end().y, 200);
}

TEST(test_item_bounding_rect) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setStart({10, 20});
    item.setEnd({100, 200});
    RECT r = item.boundingRect();
    ASSERT_TRUE(r.left <= r.right);
    ASSERT_TRUE(r.top <= r.bottom);
}

// --- Snapshot ---

TEST(test_snapshot_captures_state) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setStart({10, 20});
    item.setEnd({100, 200});
    item.setInt(AnnotationProperty::PenWidth, 5);
    item.setBool(AnnotationProperty::Filling, true);

    AnnotationSnapshot snap = item.takeSnapshot();
    ASSERT_EQ(snap.type, AnnotationType::Geometry);
    ASSERT_EQ(snap.start.x, 10);
    ASSERT_EQ(snap.end.x, 100);
    ASSERT_EQ(snap.getInt(AnnotationProperty::PenWidth), 5);
    ASSERT_EQ(snap.getBool(AnnotationProperty::Filling), true);
}

TEST(test_restore_from_snapshot) {
    ScreenshotAnnotationItem item(AnnotationType::Geometry);
    item.setStart({10, 20});
    item.setEnd({100, 200});
    item.setInt(AnnotationProperty::PenWidth, 5);

    AnnotationSnapshot snap = item.takeSnapshot();

    // Modify item
    item.setStart({50, 50});
    item.setInt(AnnotationProperty::PenWidth, 10);

    // Restore
    item.restoreFromSnapshot(snap);
    ASSERT_EQ(item.start().x, 10);
    ASSERT_EQ(item.getInt(AnnotationProperty::PenWidth), 5);
}

int main() {
    std::printf("Running ScreenshotAnnotationItem tests...\n");

    test_item_has_unique_id();
    test_item_has_type();
    test_item_default_role_is_default();
    test_item_can_set_role();
    test_item_default_properties();
    test_item_set_get_int_property();
    test_item_set_get_bool_property();
    test_item_set_get_color_property();
    test_item_set_get_double_property();
    test_item_set_get_string_property();
    test_item_has_property_returns_false_by_default();
    test_item_has_property_returns_true_after_set();
    test_item_start_end_points();
    test_item_bounding_rect();
    test_snapshot_captures_state();
    test_restore_from_snapshot();

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    } else {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
}
