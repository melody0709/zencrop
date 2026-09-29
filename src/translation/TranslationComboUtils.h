#pragma once

// COMBOBOX helpers shared by the translation settings pages. Header-only so the
// behavior the pages rely on is pinned by a test (there is no way to unit-test a
// static helper buried in a DialogProc).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace translation {

// Replace the label of the combo entry at `index`, keeping the caller's item
// pointer as that entry's item data (ownership stays with the caller, so the
// caller must free it when the combo is cleared).
//
// Insert-then-delete, never delete-then-insert: a failed insert (a full combo)
// leaves the list untouched instead of dropping the entry, which used to leave
// the combo one item short of the state it mirrors with no way back.
//
// The replacement is *index preserving*: every entry other than `index` keeps
// the absolute position it already had (the insertion above compensates the
// deletion below), so the selection must not be re-derived. It was once shifted
// down by one, reading "the deleted entry was above the selected one" -- and the
// pages call this while saving the profile the user just left, i.e. exactly when
// CB_GETCURSEL already points at the entry the user just clicked. The result was
// that clicking entry N jumped to entry N-1 (clicking the neighbour entry was
// impossible at all), so the wrong provider was rendered and saved.
inline bool ReplaceComboItemLabel(
    HWND combo, int index, const std::wstring& label, void* itemData) {
    if (!combo || index < 0) return false;
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    if (index >= count) return false;
    const LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    const LRESULT inserted = SendMessageW(combo, CB_INSERTSTRING, index + 1,
        reinterpret_cast<LPARAM>(label.c_str()));
    if (inserted == CB_ERR || inserted == CB_ERRSPACE) return false;
    SendMessageW(combo, CB_SETITEMDATA, inserted,
        reinterpret_cast<LPARAM>(itemData));
    SendMessageW(combo, CB_DELETESTRING, index, 0);
    if (selected == index) {
        // The replacement entry slid into the slot the selection points at.
        SendMessageW(combo, CB_SETCURSEL, index, 0);
    }
    return true;
}

} // namespace translation
