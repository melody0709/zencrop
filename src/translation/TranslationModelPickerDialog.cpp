#include "TranslationModelPickerDialog.h"

#include "core/ResourceIds.h"
#include "TranslationTextUtils.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace translation {
namespace {

constexpr int kColumnModel = 0;
constexpr int kColumnLabel = 1;
constexpr int kColumnSource = 2;

// One row of the catalogue. `listed` rows come from the preset and cannot be
// pooled -- the pool contract drops catalog ids -- so their check box is refused
// instead of being accepted and then silently dropped on OK.
//
// `label` is the display name a provider's listing reported, and is empty for a
// row that has none: the id is then what the user sees, and the id is what OK
// returns either way.
//
// The labels are English like the rest of the provider page: this page has never
// been localized (see the audit's N2), and translating only the strings this
// change adds would make the dialog half-translated. The messages the *page*
// authors itself are still bilingual.
struct PickerRow {
    std::wstring id;
    std::wstring label;
    std::wstring source;
    bool listed = false;
    bool checked = false;
};

struct PickerState {
    ModelPickerRequest request;
    ModelPickerResult result;
    std::vector<PickerRow> rows;
    std::vector<size_t> visible; // indices into `rows`, in list order
    std::wstring filter;
    // The list width the current column layout was computed from (0 = not laid out
    // yet). The width depends on whether the vertical scrollbar is up, so the layout
    // has to notice when that changes -- see FitColumns.
    int columnLayoutWidth = 0;
    // Set while the list view is rebuilt: LVN_ITEMCHANGED also fires for the
    // programmatic check-state writes, and reacting to those would fight the
    // state this dialog owns.
    bool syncing = false;
};

std::wstring ToLowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

std::wstring TrimCopy(const std::wstring& value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring ReadControlText(HWND dialog, int id) {
    const int length = GetWindowTextLengthW(GetDlgItem(dialog, id));
    if (length <= 0) return {};
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetDlgItemTextW(
        dialog, id, value.data(), static_cast<int>(value.size()));
    value.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return TrimCopy(value);
}

size_t CheckedCount(const PickerState& state) {
    return static_cast<size_t>(std::count_if(state.rows.begin(), state.rows.end(),
        [](const PickerRow& row) { return row.checked && !row.listed; }));
}

void UpdateHint(HWND dialog, PickerState& state) {
    const size_t checked = CheckedCount(state);
    std::wstring hint = std::to_wstring(state.rows.size()) + L" model(s)";
    hint += L"  \u2022  ";
    hint += std::to_wstring(checked) + L" selected";
    if (state.request.capacity > 0) {
        hint += L"  \u2022  limit " + std::to_wstring(state.request.capacity);
        if (checked > state.request.capacity) hint += L"  (over the limit)";
    }
    // Which model is in use is the state a user picks against, and nothing else in
    // this dialog showed it: the active id is not necessarily checked (a catalog seed
    // is active without being collectable) and the list runs to hundreds of rows.
    // Prefer the display name, like the rows do, and keep the line bounded.
    if (!state.request.active.empty()) {
        std::wstring active = state.request.active;
        for (const auto& row : state.rows) {
            if (row.id == state.request.active && !row.label.empty()) {
                active = row.label;
                break;
            }
        }
        const size_t untruncated = active.size();
        TruncateUtf16Safe(active, 48);
        if (active.size() < untruncated) active += L"\u2026";
        hint += L"  \u2022  active: " + active;
    }
    SetDlgItemTextW(dialog, IDC_MODEL_PICKER_HINT, hint.c_str());
}

// Sizes the three columns from the list's *own* width, in the ratio the template
// encoded (150/74/60 of 284). Two things make this more than a division:
//   - `LVCOLUMN::cx` is in **pixels** while the dialog's geometry is in DLU, so fixed
//     numbers would stay 150/74/60 px at every DPI while the window and its font
//     double -- at 200% the name column collapses into a few letters with the rest of
//     the list sitting empty.
//   - The width is not constant either. This runs before the first row exists, so it
//     is the width *without* a vertical scrollbar; a 458-entry listing adds that
//     scrollbar right after and takes ~SM_CXVSCROLL out of the client area. Laying the
//     columns out against the wider number made their sum exceed what was left, which
//     forced a horizontal scrollbar that could only travel a few pixels.
// So the layout is re-fitted whenever the client width actually changes, and only
// then: a column the user dragged changes `cx`, not the client width, so a manual
// resize is not undone.
void FitColumns(HWND dialog, PickerState& state) {
    const HWND list = GetDlgItem(dialog, IDC_MODEL_PICKER_LIST);
    if (!list) return;
    RECT rect = {};
    GetClientRect(list, &rect);
    // The client rect already excludes the scrollbar when it is up; the two pixels are
    // the list's own edge so the last column cannot round onto a horizontal bar.
    const int width =
        (std::max)(0, static_cast<int>(rect.right - rect.left) - 2);
    if (width <= 0 || width == state.columnLayoutWidth) return;
    state.columnLayoutWidth = width;
    const int modelWidth = width * 53 / 100;
    const int nameWidth = width * 26 / 100;
    ListView_SetColumnWidth(list, kColumnModel, modelWidth);
    ListView_SetColumnWidth(list, kColumnLabel, nameWidth);
    ListView_SetColumnWidth(list, kColumnSource, width - modelWidth - nameWidth);
}

void RebuildList(HWND dialog, PickerState& state) {
    state.syncing = true;
    const HWND list = GetDlgItem(dialog, IDC_MODEL_PICKER_LIST);
    ListView_DeleteAllItems(list);
    state.visible.clear();
    const std::wstring needle = ToLowerCopy(state.filter);
    int index = 0;
    for (size_t row = 0; row < state.rows.size(); ++row) {
        const PickerRow& entry = state.rows[row];
        // Both spellings are searchable: the id is what a request uses, but the
        // vendor's display name is what a user remembers ("Sonnet", the bare number
        // of a Qwen build) and the whole point of carrying the name alongside.
        if (!needle.empty() &&
            ToLowerCopy(entry.id).find(needle) == std::wstring::npos &&
            (entry.label.empty() ||
                ToLowerCopy(entry.label).find(needle) == std::wstring::npos)) {
            continue;
        }
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = index;
        item.iSubItem = kColumnModel;
        item.pszText = const_cast<wchar_t*>(entry.id.c_str());
        item.lParam = static_cast<LPARAM>(row);
        const int inserted = ListView_InsertItem(list, &item);
        if (inserted < 0) continue;
        // A name equal to the id is not repeated: the id column already shows it.
        if (!entry.label.empty() && entry.label != entry.id) {
            ListView_SetItemText(list, inserted, kColumnLabel,
                const_cast<wchar_t*>(entry.label.c_str()));
        }
        ListView_SetItemText(list, inserted, kColumnSource,
            const_cast<wchar_t*>(entry.source.c_str()));
        ListView_SetCheckState(list, inserted, entry.checked ? TRUE : FALSE);
        state.visible.push_back(row);
        ++index;
    }
    // After the rows are in: that is when the control has decided whether it needs a
    // vertical scrollbar, and therefore what the columns have to fit into.
    FitColumns(dialog, state);
    state.syncing = false;
    UpdateHint(dialog, state);
}

void RebuildRows(HWND dialog, PickerState& state) {
    // Order: the user's own pool first (the list they curate), then what the
    // provider just reported, then the built-in catalogue as reference.
    state.rows.clear();
    const auto exists = [&](const std::wstring& id) {
        return std::any_of(state.rows.begin(), state.rows.end(),
            [&](const PickerRow& row) { return row.id == id; });
    };
    // One id, one row: a model the vendor reported *and* the catalog publishes is a
    // single entry, and the pool contract would drop it either way. The marker decides
    // what its checkbox is allowed to do.
    const auto isSeed = [&](const std::wstring& id) {
        return std::find(state.request.listed.begin(), state.request.listed.end(),
                   id) != state.request.listed.end();
    };
    for (const auto& entry : state.request.pool) {
        if (entry.id.empty() || exists(entry.id)) continue;
        PickerRow row;
        row.id = entry.id;
        row.label = entry.label;
        row.source = L"My list";
        row.checked = true;
        state.rows.push_back(std::move(row));
    }
    for (const auto& entry : state.request.available) {
        if (entry.id.empty() || exists(entry.id)) continue;
        PickerRow row;
        row.id = entry.id;
        row.label = entry.label;
        row.source = L"Fetched";
        // A catalog model the vendor also reported keeps the vendor's display name --
        // it is better than the id -- but it is still not collectable, and the pool
        // writer would drop it again on save. Marking it here rather than in the
        // built-in loop below, which cannot see it: `exists` skips the duplicate, so
        // a fetched row used to arrive unmarked, could be checked, and was then
        // silently discarded by SetCustomModelPool.
        row.listed = isSeed(entry.id);
        state.rows.push_back(std::move(row));
    }
    for (const auto& id : state.request.listed) {
        if (id.empty() || exists(id)) continue;
        PickerRow row;
        row.id = id;
        row.source = L"Built-in";
        row.listed = true;
        state.rows.push_back(std::move(row));
    }
    RebuildList(dialog, state);
}

void Accept(HWND dialog, PickerState& state) {
    const size_t checked = CheckedCount(state);
    if (state.request.capacity > 0 && checked > state.request.capacity) {
        std::wstring message = L"Too many models selected: " +
            std::to_wstring(checked) + L" / " +
            std::to_wstring(state.request.capacity) +
            L". Uncheck a few first: nothing of yours is evicted silently.";
        MessageBoxW(dialog, message.c_str(), L"Models", MB_OK | MB_ICONWARNING);
        return;
    }
    std::vector<ModelNameEntry> pool;
    for (const auto& row : state.rows) {
        if (row.listed || !row.checked) continue;
        if (std::any_of(pool.begin(), pool.end(),
                [&](const ModelNameEntry& kept) { return kept.id == row.id; })) {
            continue;
        }
        ModelNameEntry kept;
        kept.id = row.id;
        // A name that only repeats the id is not carried: the side table stores
        // names, not echoes of the id column.
        kept.label = row.label == row.id ? std::wstring() : row.label;
        pool.push_back(std::move(kept));
    }
    // The active model is applied *after* this pool is written, and applying it
    // remembers it, which on a full pool evicts the oldest entry. Counting only the
    // checkboxes made that eviction happen with no warning at all -- the dialog said
    // "nothing of yours is evicted silently" and then did exactly that, one click
    // after OK. So the id the page is about to add is part of what this dialog has to
    // approve, and the cap is checked with it included.
    std::vector<std::wstring> poolIds;
    poolIds.reserve(pool.size());
    for (const auto& entry : pool) poolIds.push_back(entry.id);
    const std::wstring& active = state.request.active;
    const bool activeJoinsPool = ActiveModelJoinsPool(active,
        state.request.allowsCustomModel, state.request.listed, poolIds);
    if (activeJoinsPool &&
        !PoolFitsWithinCapacity(pool.size(), 1, state.request.capacity)) {
        const std::wstring message =
            L"Your list already holds " + std::to_wstring(pool.size()) +
            L" models, which is the limit. Setting '" + active + L"' active would add "
            L"it to the list, and the oldest entry would be dropped.\n\n"
            L"Uncheck a few first, or set active to a listed model. "
            L"Nothing of yours is evicted silently.";
        MessageBoxW(dialog, message.c_str(), L"Models", MB_OK | MB_ICONWARNING);
        return;
    }
    state.result.pool = std::move(pool);
    state.result.active = state.request.active;
    state.result.modified = true;
    EndDialog(dialog, IDOK);
}

INT_PTR CALLBACK PickerProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PickerState*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<PickerState*>(lParam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        const HWND list = GetDlgItem(dialog, IDC_MODEL_PICKER_LIST);
        ListView_SetExtendedListViewStyle(
            list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        // The widths are FitColumns' business: it measures the list, and it re-measures
        // once the rows have decided whether a vertical scrollbar is needed.
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(L"Model");
        column.iSubItem = kColumnModel;
        ListView_InsertColumn(list, kColumnModel, &column);
        column.pszText = const_cast<wchar_t*>(L"Display name");
        column.iSubItem = kColumnLabel;
        ListView_InsertColumn(list, kColumnLabel, &column);
        column.pszText = const_cast<wchar_t*>(L"Source");
        column.iSubItem = kColumnSource;
        ListView_InsertColumn(list, kColumnSource, &column);
        FitColumns(dialog, *state);
        RebuildRows(dialog, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND) {
        const int control = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (control == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (control == IDOK) {
            Accept(dialog, *state);
            return TRUE;
        }
        if (control == IDC_MODEL_PICKER_SEARCH && notification == EN_CHANGE) {
            state->filter = ReadControlText(dialog, IDC_MODEL_PICKER_SEARCH);
            RebuildList(dialog, *state);
            return TRUE;
        }
        if (control == IDC_MODEL_PICKER_SET_ACTIVE && notification == BN_CLICKED) {
            const HWND list = GetDlgItem(dialog, IDC_MODEL_PICKER_LIST);
            const int item = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (item < 0 || static_cast<size_t>(item) >= state->visible.size()) {
                MessageBoxW(dialog, L"Select a model first.",
                    L"Models", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            state->request.active =
                state->rows[state->visible[static_cast<size_t>(item)]].id;
            UpdateHint(dialog, *state);
            return TRUE;
        }
        if (control == IDC_MODEL_PICKER_ADD && notification == BN_CLICKED) {
            const std::wstring typed =
                TrimCopy(ReadControlText(dialog, IDC_MODEL_PICKER_NEW));
            const bool tooLong = state->request.maxIdLength > 0 &&
                typed.size() > state->request.maxIdLength;
            if (typed.empty() || tooLong) {
                MessageBoxW(dialog, L"Enter a valid model id.",
                    L"Models", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            // Refused here rather than at Apply, and with the actual reason: the url
            // predicate is what the pool writers and the settings validator use, so
            // a typed id that fails it can never become a saved model.
            if (!IsStorableModelIdentifier(typed)) {
                MessageBoxW(dialog,
                    L"A model id cannot contain '?', '#', or any space or control "
                    L"character.",
                    L"Models", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            // A typed id that happens to be a catalog model must not become
            // collectable: the pool contract drops catalog ids, so accepting the
            // check here would show it as collected and then silently discard it.
            // `listed` is therefore never cleared -- the row keeps refusing the
            // check like every other built-in entry.
            for (auto& row : state->rows) {
                if (row.id != typed || row.listed) continue;
                row.checked = true;
            }
            const bool exists = std::any_of(state->rows.begin(), state->rows.end(),
                [&](const PickerRow& row) { return row.id == typed; });
            const bool listedExists = exists &&
                std::any_of(state->rows.begin(), state->rows.end(),
                    [&](const PickerRow& row) {
                        return row.id == typed && row.listed;
                    });
            if (listedExists) {
                MessageBoxW(dialog,
                    L"Built-in catalog models need no collecting: pick them directly "
                    L"in the Model list.",
                    L"Models", MB_OK | MB_ICONINFORMATION);
                SetDlgItemTextW(dialog, IDC_MODEL_PICKER_NEW, L"");
                return TRUE;
            }
            if (!exists) {
                PickerRow row;
                row.id = typed;
                row.source = L"Typed";
                row.checked = true;
                state->rows.insert(state->rows.begin(), std::move(row));
            }
            // The new row must be visible even while a filter is active.
            SetDlgItemTextW(dialog, IDC_MODEL_PICKER_SEARCH, L"");
            state->filter.clear();
            SetDlgItemTextW(dialog, IDC_MODEL_PICKER_NEW, L"");
            RebuildList(dialog, *state);
            return TRUE;
        }
        if (control == IDC_MODEL_PICKER_SELECT_ALL ||
            control == IDC_MODEL_PICKER_CLEAR_ALL) {
            if (notification != BN_CLICKED) return TRUE;
            const bool select = control == IDC_MODEL_PICKER_SELECT_ALL;
            const HWND list = GetDlgItem(dialog, IDC_MODEL_PICKER_LIST);
            // The buttons say "all", so they mean all: a filter decides what is on
            // screen, not what a global action covers. Previously they walked the
            // visible rows only, which made "Clear all" behave like "Clear what you
            // happen to be looking at" -- the rows a filter was hiding stayed checked
            // and still reached the pool on OK, which is the surprise the label
            // invited. Every row is updated here; the visible ones are painted
            // directly so the list's own selection survives the click.
            for (auto& row : state->rows) {
                if (row.listed) continue;
                row.checked = select;
            }
            state->syncing = true;
            for (size_t i = 0; i < state->visible.size(); ++i) {
                if (state->rows[state->visible[i]].listed) continue;
                ListView_SetCheckState(
                    list, static_cast<int>(i), select ? TRUE : FALSE);
            }
            state->syncing = false;
            UpdateHint(dialog, *state);
            return TRUE;
        }
        return FALSE;
    }
    if (message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->idFrom != IDC_MODEL_PICKER_LIST ||
            header->code != LVN_ITEMCHANGED) {
            return FALSE;
        }
        const auto* change = reinterpret_cast<NMLISTVIEW*>(lParam);
        if (state->syncing || (change->uChanged & LVIF_STATE) == 0) return TRUE;
        const bool checkChanged =
            (change->uNewState & LVIS_STATEIMAGEMASK) !=
            (change->uOldState & LVIS_STATEIMAGEMASK);
        if (!checkChanged) return TRUE;
        const int item = change->iItem;
        if (item < 0 || static_cast<size_t>(item) >= state->visible.size()) {
            return TRUE;
        }
        PickerRow& row = state->rows[state->visible[static_cast<size_t>(item)]];
        const bool checked =
            ListView_GetCheckState(GetDlgItem(dialog, IDC_MODEL_PICKER_LIST), item)
            != FALSE;
        if (row.listed && checked) {
            state->syncing = true;
            ListView_SetCheckState(
                GetDlgItem(dialog, IDC_MODEL_PICKER_LIST), item, FALSE);
            state->syncing = false;
            MessageBoxW(dialog,
                L"Built-in catalog models need no collecting: pick them directly "
                L"in the Model list.",
                L"Models", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        row.checked = checked;
        UpdateHint(dialog, *state);
        return TRUE;
    }
    return FALSE;
}

} // namespace

bool ShowTranslationModelPicker(
    HWND owner, const ModelPickerRequest& request, ModelPickerResult& result) {
    PickerState state;
    state.request = request;
    // The list view class must exist before the template is created. The property
    // sheet already initializes the common controls for the tooltip it owns, and
    // this call is idempotent.
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&controls);
    const INT_PTR outcome = DialogBoxParamW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_TRANSLATION_MODEL_PICKER), owner, PickerProc,
        reinterpret_cast<LPARAM>(&state));
    if (outcome != IDOK) return false;
    result = state.result;
    return true;
}

} // namespace translation
