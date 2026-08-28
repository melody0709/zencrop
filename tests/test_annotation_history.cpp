#include "screenshot/annotation/AnnotationHistory.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/annotation/AnnotationModel.h"
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

// --- Basic stack operations ---

TEST(test_empty_stack) {
    AnnotationHistory history;
    ASSERT_FALSE(history.canUndo());
    ASSERT_FALSE(history.canRedo());
}

TEST(test_push_create_and_undo) {
    AnnotationHistory history;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    item->setInt(AnnotationProperty::PenWidth, 5);
    std::wstring id = item->id();
    AnnotationSnapshot after = item->takeSnapshot();

    history.pushCreate(id, after);
    ASSERT_TRUE(history.canUndo());
    ASSERT_FALSE(history.canRedo());

    auto entry = history.undo();
    ASSERT_EQ(entry.type, AnnotationCommandType::Create);
    ASSERT_EQ(entry.itemId, id);
}

TEST(test_undo_create_removes_item) {
    ScreenshotAnnotationModel model;
    AnnotationHistory history;

    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    std::wstring id = item->id();
    AnnotationSnapshot after = item->takeSnapshot();
    model.add(std::move(item));

    history.pushCreate(id, after);
    ASSERT_EQ(model.count(), 1);

    auto entry = history.undo();
    // After undoing create, the item should be removed
    model.removeById(entry.itemId);
    ASSERT_EQ(model.count(), 0);
}

TEST(test_push_delete_and_undo) {
    AnnotationHistory history;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Arrow);
    item->setInt(AnnotationProperty::PenWidth, 10);
    std::wstring id = item->id();
    AnnotationSnapshot before = item->takeSnapshot();

    history.pushDelete(id, before);
    ASSERT_TRUE(history.canUndo());

    auto entry = history.undo();
    ASSERT_EQ(entry.type, AnnotationCommandType::Delete);
    ASSERT_EQ(entry.itemId, id);
}

TEST(test_push_modify_and_undo) {
    AnnotationHistory history;
    std::wstring id = L"test_id";
    AnnotationSnapshot before, after;
    before.setInt(AnnotationProperty::PenWidth, 5);
    after.setInt(AnnotationProperty::PenWidth, 10);

    history.pushModify(id, before, after);
    ASSERT_TRUE(history.canUndo());

    auto entry = history.undo();
    ASSERT_EQ(entry.type, AnnotationCommandType::Modify);
    ASSERT_TRUE(entry.before.getInt(AnnotationProperty::PenWidth) == 5);
}

TEST(test_redo_after_undo) {
    AnnotationHistory history;
    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    std::wstring id = item->id();
    AnnotationSnapshot after = item->takeSnapshot();

    history.pushCreate(id, after);
    history.undo();
    ASSERT_FALSE(history.canUndo());
    ASSERT_TRUE(history.canRedo());

    auto entry = history.redo();
    ASSERT_EQ(entry.type, AnnotationCommandType::Create);
    ASSERT_EQ(entry.itemId, id);
}

TEST(test_undo_clears_redo_stack) {
    AnnotationHistory history;
    AnnotationSnapshot snap;

    history.pushCreate(L"id1", snap);
    history.pushCreate(L"id2", snap);
    history.undo();
    ASSERT_TRUE(history.canRedo());

    // New operation should clear redo stack
    history.pushCreate(L"id3", snap);
    ASSERT_FALSE(history.canRedo());
}

// --- Committed Document Undo/Redo owner ---

TEST(test_apply_undo_redo_create_projects_document_state) {
    AnnotationDocument document;
    AnnotationHistory history;
    ScreenshotEditorState state;

    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    item->setInt(AnnotationProperty::PenWidth, 5);
    const std::wstring id = item->id();
    const AnnotationSnapshot after = item->takeSnapshot();
    document.add(std::move(item));
    history.pushCreate(id, after);
    ScreenshotAnnotationSelectById(state, document, id);
    ScreenshotEditorSyncTextEditingById(state, -1, id);
    ScreenshotEditorSyncPendingTextAnnotationCreateId(state, id);

    ASSERT_TRUE(history.applyUndoRedo(false, document, state));
    ASSERT_EQ(document.count(), 0);
    ASSERT_TRUE(ScreenshotEditorSelectedAnnotationId(state).empty());
    ASSERT_TRUE(ScreenshotEditorTextEditingId(state).empty());
    ASSERT_TRUE(ScreenshotEditorPendingTextAnnotationCreateId(state).empty());
    ASSERT_FALSE(ScreenshotEditorUndoAvailable(state));
    ASSERT_TRUE(ScreenshotEditorRedoAvailable(state));

    ASSERT_TRUE(history.applyUndoRedo(true, document, state));
    ASSERT_EQ(document.count(), 1);
    ASSERT_EQ(ScreenshotEditorSelectedAnnotationId(state), id);
    ASSERT_TRUE(ScreenshotEditorUndoAvailable(state));
    ASSERT_FALSE(ScreenshotEditorRedoAvailable(state));
}

TEST(test_apply_undo_redo_modify_restores_snapshots) {
    AnnotationDocument document;
    AnnotationHistory history;
    ScreenshotEditorState state;

    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    const std::wstring id = item->id();
    item->setInt(AnnotationProperty::PenWidth, 5);
    const AnnotationSnapshot before = item->takeSnapshot();
    item->setInt(AnnotationProperty::PenWidth, 10);
    const AnnotationSnapshot after = item->takeSnapshot();
    document.add(std::move(item));
    history.pushModify(id, before, after);

    ASSERT_TRUE(history.applyUndoRedo(false, document, state));
    ASSERT_EQ(document.findById(id)->getInt(AnnotationProperty::PenWidth), 5);
    ASSERT_EQ(ScreenshotEditorSelectedAnnotationId(state), id);

    ASSERT_TRUE(history.applyUndoRedo(true, document, state));
    ASSERT_EQ(document.findById(id)->getInt(AnnotationProperty::PenWidth), 10);
    ASSERT_EQ(ScreenshotEditorSelectedAnnotationId(state), id);
}

TEST(test_apply_undo_redo_recalculates_serial_counter) {
    AnnotationDocument document;
    AnnotationHistory history;
    ScreenshotEditorState state;

    auto item = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Serial);
    const std::wstring id = item->id();
    item->setInt(AnnotationProperty::SerialIndex, 7);
    const AnnotationSnapshot after = item->takeSnapshot();
    document.add(std::move(item));
    history.pushCreate(id, after);
    state.effectStyle.serialCounter = 99;

    ASSERT_TRUE(history.applyUndoRedo(false, document, state));
    ASSERT_EQ(state.effectStyle.serialCounter, 1);
    ASSERT_TRUE(history.applyUndoRedo(true, document, state));
    ASSERT_EQ(state.effectStyle.serialCounter, 8);
}

TEST(test_apply_undo_redo_group_preserves_document_order) {
    AnnotationDocument document;
    AnnotationHistory history;
    ScreenshotEditorState state;

    auto first = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Geometry);
    const std::wstring firstId = first->id();
    AnnotationSnapshot firstBefore = first->takeSnapshot();
    firstBefore.setInt(AnnotationProperty::StackIndex, 0);
    auto second = std::make_unique<ScreenshotAnnotationItem>(AnnotationType::Arrow);
    const std::wstring secondId = second->id();
    AnnotationSnapshot secondBefore = second->takeSnapshot();
    secondBefore.setInt(AnnotationProperty::StackIndex, 1);

    history.beginGroup(L"delete_pair");
    history.pushDelete(firstId, firstBefore);
    history.pushDelete(secondId, secondBefore);
    history.endGroup();

    ASSERT_TRUE(history.applyUndoRedo(false, document, state));
    const auto restored = ScreenshotAnnotationDocumentProjectOrdered(document);
    ASSERT_EQ((int)restored.size(), 2);
    ASSERT_EQ(restored[0].id, firstId);
    ASSERT_EQ(restored[1].id, secondId);
    ASSERT_EQ(ScreenshotEditorSelectedAnnotationId(state), firstId);

    ASSERT_TRUE(history.applyUndoRedo(true, document, state));
    ASSERT_EQ(document.count(), 0);
    ASSERT_TRUE(ScreenshotEditorSelectedAnnotationId(state).empty());
}

TEST(test_apply_undo_redo_rejects_unavailable_history) {
    AnnotationDocument document;
    AnnotationHistory history;
    ScreenshotEditorState state;

    ASSERT_FALSE(history.applyUndoRedo(false, document, state));
    ASSERT_FALSE(history.applyUndoRedo(true, document, state));
    ASSERT_EQ(document.count(), 0);
    ASSERT_FALSE(ScreenshotEditorUndoAvailable(state));
    ASSERT_FALSE(ScreenshotEditorRedoAvailable(state));
}

// --- Grouping ---

TEST(test_group_undo_redo) {
    AnnotationHistory history;
    AnnotationSnapshot snap;

    history.beginGroup(L"group1");
    history.pushModify(L"id1", snap, snap);
    history.pushModify(L"id2", snap, snap);
    history.endGroup();

    // Undoing one entry in the group should undo all
    auto entry = history.undo();
    ASSERT_EQ(entry.groupId, L"group1");
    // After group undo, canUndo should be false (both entries undone)
    ASSERT_FALSE(history.canUndo());
}

TEST(test_slider_drag_grouping) {
    AnnotationHistory history;
    AnnotationSnapshot before, after;
    before.setInt(AnnotationProperty::PenWidth, 5);
    after.setInt(AnnotationProperty::PenWidth, 10);

    // Simulate slider drag: multiple modifies in one group
    history.beginGroup(L"slider_drag");
    history.pushModify(L"id1", before, after);
    history.pushModify(L"id1", before, after);
    history.pushModify(L"id1", before, after);
    history.endGroup();

    // Should only need one undo to revert all
    ASSERT_TRUE(history.canUndo());
    history.undo();
    ASSERT_FALSE(history.canUndo());
}

TEST(test_group_entries_preserve_apply_order) {
    AnnotationHistory history;
    AnnotationSnapshot snap;

    history.beginGroup(L"clear_all");
    history.pushDelete(L"id2", snap);
    history.pushDelete(L"id1", snap);
    history.pushDelete(L"id0", snap);
    history.endGroup();

    auto undoEntries = history.undoGroup();
    ASSERT_EQ((int)undoEntries.size(), 3);
    ASSERT_EQ(undoEntries[0].itemId, L"id0");
    ASSERT_EQ(undoEntries[1].itemId, L"id1");
    ASSERT_EQ(undoEntries[2].itemId, L"id2");

    auto redoEntries = history.redoGroup();
    ASSERT_EQ((int)redoEntries.size(), 3);
    ASSERT_EQ(redoEntries[0].itemId, L"id2");
    ASSERT_EQ(redoEntries[1].itemId, L"id1");
    ASSERT_EQ(redoEntries[2].itemId, L"id0");
}

// --- Multiple undo/redo ---

TEST(test_multiple_undo_redo_sequence) {
    AnnotationHistory history;
    AnnotationSnapshot snap;

    history.pushCreate(L"id1", snap);
    history.pushCreate(L"id2", snap);
    history.pushCreate(L"id3", snap);

    ASSERT_TRUE(history.canUndo());
    history.undo(); // undo id3
    history.undo(); // undo id2
    history.undo(); // undo id1
    ASSERT_FALSE(history.canUndo());

    // Redo all
    history.redo(); // redo id1
    history.redo(); // redo id2
    history.redo(); // redo id3
    ASSERT_FALSE(history.canRedo());
    ASSERT_TRUE(history.canUndo());
}

int main() {
    std::printf("Running AnnotationHistory tests...\n");

    test_empty_stack();
    test_push_create_and_undo();
    test_undo_create_removes_item();
    test_push_delete_and_undo();
    test_push_modify_and_undo();
    test_redo_after_undo();
    test_undo_clears_redo_stack();
    test_apply_undo_redo_create_projects_document_state();
    test_apply_undo_redo_modify_restores_snapshots();
    test_apply_undo_redo_recalculates_serial_counter();
    test_apply_undo_redo_group_preserves_document_order();
    test_apply_undo_redo_rejects_unavailable_history();
    test_group_undo_redo();
    test_slider_drag_grouping();
    test_group_entries_preserve_apply_order();
    test_multiple_undo_redo_sequence();

    if (g_failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    } else {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
}
