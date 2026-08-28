#pragma once

#include "screenshot/annotation/AnnotationItem.h"
#include <vector>
#include <string>

class ScreenshotAnnotationModel;
struct ScreenshotEditorState;

struct AnnotationHistoryEntry {
    AnnotationCommandType type = AnnotationCommandType::Create;
    std::wstring itemId;
    std::wstring groupId;
    AnnotationSnapshot before;
    AnnotationSnapshot after;
};

class AnnotationHistory {
public:
    AnnotationHistory() = default;

    // Push operations
    void pushCreate(const std::wstring& itemId, const AnnotationSnapshot& after);
    void pushDelete(const std::wstring& itemId, const AnnotationSnapshot& before);
    void pushModify(const std::wstring& itemId, const AnnotationSnapshot& before, const AnnotationSnapshot& after);

    // Grouping (for slider drag batching)
    void beginGroup(const std::wstring& groupId);
    void endGroup();

    // Undo/Redo
    bool canUndo() const;
    bool canRedo() const;
    AnnotationHistoryEntry undo();
    AnnotationHistoryEntry redo();
    std::vector<AnnotationHistoryEntry> undoGroup();
    std::vector<AnnotationHistoryEntry> redoGroup();

    bool applyUndoRedo(bool redo, ScreenshotAnnotationModel& document, ScreenshotEditorState& state);

    // Clear
    void clear();

private:
    std::vector<AnnotationHistoryEntry> m_undoStack;
    std::vector<AnnotationHistoryEntry> m_redoStack;
    std::wstring m_currentGroup;
    bool m_inGroup = false;

    void pushEntry(AnnotationHistoryEntry entry);
};
