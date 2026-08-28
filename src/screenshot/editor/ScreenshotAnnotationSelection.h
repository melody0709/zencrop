#pragma once

#include <cstddef>

// Stage 2 pure helpers for annotation list selection / index repair.
// Overlay still owns the vector; these encode adjust-after-erase rules.

// After erasing `erasedIndex`, adjust selection. Returns new selected index (-1 = none).
inline int ScreenshotAnnotationSelectionAfterErase(int selectedIndex, int erasedIndex)
{
    if (selectedIndex < 0) return -1;
    if (erasedIndex < 0) return selectedIndex;
    if (selectedIndex == erasedIndex) return -1;
    if (selectedIndex > erasedIndex) return selectedIndex - 1;
    return selectedIndex;
}

// After inserting at `insertIndex`, selection moves to the new item (insertIndex).
// If previously unselected, still selects insertIndex (common create path).
// selectedIndex is reserved for future "keep selection when insert is unrelated" rules.
inline int ScreenshotAnnotationSelectionAfterInsert(int selectedIndex, int insertIndex)
{
    (void)selectedIndex;
    return insertIndex;
}

// Clamp selected index into list of size `count` (-1 stays -1 when empty or already clear).
inline int ScreenshotAnnotationSelectionClamp(int selectedIndex, size_t count)
{
    if (count == 0) return -1;
    if (selectedIndex < 0) return -1;
    if (static_cast<size_t>(selectedIndex) >= count) {
        return static_cast<int>(count) - 1;
    }
    return selectedIndex;
}

// True when index is a valid annotation slot.
inline bool ScreenshotAnnotationSelectionIsValid(int selectedIndex, size_t count)
{
    return selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < count;
}
