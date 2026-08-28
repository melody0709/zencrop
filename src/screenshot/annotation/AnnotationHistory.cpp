#include "screenshot/annotation/AnnotationHistory.h"

#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotEditorState.h"

namespace {

void RecalculateAnnotationHistorySerialCounter(
    const ScreenshotAnnotationModel& document,
    ScreenshotEditorState& state)
{
    int nextSerial = 1;
    const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(document);
    for (const auto& annotation : ordered) {
        if (annotation.type == ScreenshotToolbarCommand::ToolSerial) {
            if (annotation.serialNumber >= nextSerial) nextSerial = annotation.serialNumber + 1;
        }
    }
    state.effectStyle.serialCounter = nextSerial;
}

void ProjectAnnotationHistoryMutation(
    AnnotationHistory& history,
    ScreenshotAnnotationModel& document,
    ScreenshotEditorState& state,
    const std::wstring& selectedId)
{
    ScreenshotAnnotationSelectById(state, document, selectedId);
    ScreenshotEditorSyncTextEditingById(state, -1, L"");
    RecalculateAnnotationHistorySerialCounter(document, state);
    ScreenshotEditorSetHistoryAvailability(state, history.canUndo(), history.canRedo());
}

bool ApplyAnnotationHistoryEntry(
    const AnnotationHistoryEntry& entry,
    bool redo,
    ScreenshotAnnotationModel& document,
    ScreenshotEditorState& state,
    std::wstring& selectedId)
{
    if (entry.type == AnnotationCommandType::Create ||
        entry.type == AnnotationCommandType::Delete) {
        const bool restoreSnapshot =
            (entry.type == AnnotationCommandType::Create) == redo;
        if (restoreSnapshot) {
            const std::wstring restoredId = ScreenshotAnnotationDocumentInsertFromSnapshotSole(
                document,
                entry.type == AnnotationCommandType::Create ? entry.after : entry.before,
                entry.itemId);
            if (restoredId.empty()) return false;
            selectedId = restoredId;
            return true;
        }
        if (!ScreenshotAnnotationDocumentRemove(document, entry.itemId)) return false;
        if (entry.itemId == ScreenshotEditorPendingTextAnnotationCreateId(state)) {
            ScreenshotEditorSyncPendingTextAnnotationCreateId(state, L"");
        }
        selectedId.clear();
        return true;
    }
    if (entry.type != AnnotationCommandType::Modify ||
        !ScreenshotAnnotationDocumentReplaceFromSnapshotSole(
            document, entry.itemId, redo ? entry.after : entry.before)) {
        return false;
    }
    selectedId = entry.itemId;
    return true;
}

} // namespace

void AnnotationHistory::pushEntry(AnnotationHistoryEntry entry) {
    if (m_inGroup) {
        entry.groupId = m_currentGroup;
    }
    m_undoStack.push_back(std::move(entry));
    m_redoStack.clear(); // new operation invalidates redo
}

void AnnotationHistory::pushCreate(const std::wstring& itemId, const AnnotationSnapshot& after) {
    AnnotationHistoryEntry entry;
    entry.type = AnnotationCommandType::Create;
    entry.itemId = itemId;
    entry.after = after;
    pushEntry(std::move(entry));
}

void AnnotationHistory::pushDelete(const std::wstring& itemId, const AnnotationSnapshot& before) {
    AnnotationHistoryEntry entry;
    entry.type = AnnotationCommandType::Delete;
    entry.itemId = itemId;
    entry.before = before;
    pushEntry(std::move(entry));
}

void AnnotationHistory::pushModify(const std::wstring& itemId, const AnnotationSnapshot& before, const AnnotationSnapshot& after) {
    AnnotationHistoryEntry entry;
    entry.type = AnnotationCommandType::Modify;
    entry.itemId = itemId;
    entry.before = before;
    entry.after = after;
    pushEntry(std::move(entry));
}

void AnnotationHistory::beginGroup(const std::wstring& groupId) {
    m_currentGroup = groupId;
    m_inGroup = true;
}

void AnnotationHistory::endGroup() {
    m_currentGroup.clear();
    m_inGroup = false;
}

bool AnnotationHistory::canUndo() const {
    return !m_undoStack.empty();
}

bool AnnotationHistory::canRedo() const {
    return !m_redoStack.empty();
}

AnnotationHistoryEntry AnnotationHistory::undo() {
    auto entries = undoGroup();
    return entries.empty() ? AnnotationHistoryEntry{} : entries.back();
}

AnnotationHistoryEntry AnnotationHistory::redo() {
    auto entries = redoGroup();
    return entries.empty() ? AnnotationHistoryEntry{} : entries.back();
}

std::vector<AnnotationHistoryEntry> AnnotationHistory::undoGroup() {
    std::vector<AnnotationHistoryEntry> entries;
    if (m_undoStack.empty()) return entries;

    std::wstring groupId = m_undoStack.back().groupId;

    do {
        AnnotationHistoryEntry entry = m_undoStack.back();
        entries.push_back(entry);
        m_redoStack.push_back(entry);
        m_undoStack.pop_back();
    } while (!groupId.empty() && !m_undoStack.empty() && m_undoStack.back().groupId == groupId);

    return entries;
}

std::vector<AnnotationHistoryEntry> AnnotationHistory::redoGroup() {
    std::vector<AnnotationHistoryEntry> entries;
    if (m_redoStack.empty()) return entries;

    std::wstring groupId = m_redoStack.back().groupId;

    do {
        AnnotationHistoryEntry entry = m_redoStack.back();
        entries.push_back(entry);
        m_undoStack.push_back(entry);
        m_redoStack.pop_back();
    } while (!groupId.empty() && !m_redoStack.empty() && m_redoStack.back().groupId == groupId);

    return entries;
}

bool AnnotationHistory::applyUndoRedo(
    bool redo,
    ScreenshotAnnotationModel& document,
    ScreenshotEditorState& state)
{
    ScreenshotEditorSetHistoryAvailability(state, canUndo(), canRedo());
    if (redo ? !ScreenshotEditorRedoAvailable(state) : !ScreenshotEditorUndoAvailable(state)) {
        return false;
    }

    const auto entries = redo ? redoGroup() : undoGroup();
    bool handled = false;
    std::wstring selectedId;
    for (const auto& entry : entries) {
        handled = ApplyAnnotationHistoryEntry(entry, redo, document, state, selectedId) || handled;
    }
    if (handled) ProjectAnnotationHistoryMutation(*this, document, state, selectedId);
    return handled;
}

void AnnotationHistory::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
    m_currentGroup.clear();
    m_inGroup = false;
}
