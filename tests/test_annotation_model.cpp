#include "screenshot/annotation/AnnotationModel.h"
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
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))
#define ASSERT_NULL(ptr) ASSERT_TRUE((ptr) == nullptr)
#define ASSERT_NOT_NULL(ptr) ASSERT_TRUE((ptr) != nullptr)

// --- CRUD ---

TEST(test_add_and_count) {
    ScreenshotAnnotationModel model;
    ASSERT_EQ(model.count(), 0);
    model.add(std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry));
    ASSERT_EQ(model.count(), 1);
    model.add(std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Arrow));
    ASSERT_EQ(model.count(), 2);
}

TEST(test_find_by_id) {
    ScreenshotAnnotationModel model;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    std::wstring id = item->id();
    model.add(std::move(item));

    ScreenshotAnnotationItem* found = model.findById(id);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(found->type(), AnnotationType::Geometry);
}

TEST(test_find_by_id_not_found) {
    ScreenshotAnnotationModel model;
    ASSERT_NULL(model.findById(L"nonexistent"));
}

TEST(test_remove_by_id) {
    ScreenshotAnnotationModel model;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Arrow);
    std::wstring id = item->id();
    model.add(std::move(item));
    ASSERT_EQ(model.count(), 1);

    bool removed = model.removeById(id);
    ASSERT_TRUE(removed);
    ASSERT_EQ(model.count(), 0);
    ASSERT_NULL(model.findById(id));
}

TEST(test_remove_nonexistent) {
    ScreenshotAnnotationModel model;
    ASSERT_FALSE(model.removeById(L"nonexistent"));
}

// --- Active item ---

TEST(test_no_active_item_by_default) {
    ScreenshotAnnotationModel model;
    ASSERT_NULL(model.activeItem());
}

TEST(test_set_active_item) {
    ScreenshotAnnotationModel model;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    std::wstring id = item->id();
    model.add(std::move(item));

    model.setActiveItem(id);
    ASSERT_NOT_NULL(model.activeItem());
    ASSERT_EQ(model.activeItem()->type(), AnnotationType::Geometry);
}

TEST(test_set_active_to_nonexistent_clears) {
    ScreenshotAnnotationModel model;
    model.setActiveItem(L"nonexistent");
    ASSERT_NULL(model.activeItem());
}

TEST(test_clear_active) {
    ScreenshotAnnotationModel model;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Arrow);
    std::wstring id = item->id();
    model.add(std::move(item));
    model.setActiveItem(id);
    ASSERT_NOT_NULL(model.activeItem());

    model.clearActiveItem();
    ASSERT_NULL(model.activeItem());
}

TEST(test_remove_active_item_clears_active) {
    ScreenshotAnnotationModel model;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Text);
    std::wstring id = item->id();
    model.add(std::move(item));
    model.setActiveItem(id);
    ASSERT_NOT_NULL(model.activeItem());

    model.removeById(id);
    ASSERT_NULL(model.activeItem());
}

// --- Iteration ---

TEST(test_iteration_order) {
    ScreenshotAnnotationModel model;
    model.add(std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry));
    model.add(std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Arrow));
    model.add(std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Pencil));

    int count = 0;
    AnnotationType lastType = AnnotationType::None;
    model.forEach([&](ScreenshotAnnotationItem& item) {
        count++;
        lastType = item.type();
    });
    ASSERT_EQ(count, 3);
    ASSERT_EQ(lastType, AnnotationType::Pencil); // last added
}

// --- Clear all ---

TEST(test_clear_all) {
    ScreenshotAnnotationModel model;
    model.add(std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry));
    model.add(std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Arrow));
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Text);
    std::wstring id = item->id();
    model.add(std::move(item));
    model.setActiveItem(id);

    model.clear();
    ASSERT_EQ(model.count(), 0);
    ASSERT_NULL(model.activeItem());
}

// --- Take ownership (for undo/redo re-insertion) ---

TEST(test_take_and_reinsert) {
    ScreenshotAnnotationModel model;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Marker);
    std::wstring id = item->id();
    model.add(std::move(item));

    // Take ownership
    auto taken = model.takeById(id);
    ASSERT_NOT_NULL(taken.get());
    ASSERT_EQ(model.count(), 0);

    // Re-insert
    model.add(std::move(taken));
    ASSERT_EQ(model.count(), 1);
    ASSERT_NOT_NULL(model.findById(id));
}

int main() {
    std::printf("Running ScreenshotAnnotationModel tests...\n");

    test_add_and_count();
    test_find_by_id();
    test_find_by_id_not_found();
    test_remove_by_id();
    test_remove_nonexistent();
    test_no_active_item_by_default();
    test_set_active_item();
    test_set_active_to_nonexistent_clears();
    test_clear_active();
    test_remove_active_item_clears_active();
    test_iteration_order();
    test_clear_all();
    test_take_and_reinsert();

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    } else {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
}
