#include "SelectionTypes.h"

#include <algorithm>
#include <cwctype>
#include <limits>

namespace selection {

HWND TopLevelWindow(HWND window) {
    if (!window) return nullptr;
    const HWND root = GetAncestor(window, GA_ROOT);
    return root ? root : window;
}

RECT CursorAnchorRect(POINT cursor) {
    return {cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
}

bool HasNonWhitespace(const std::wstring& text) {
    for (const wchar_t character : text) {
        if (!iswspace(character) && character != L'\0') return true;
    }
    return false;
}

bool IsValidSelectionUtf16(const std::wstring& text) {
    for (size_t index = 0; index < text.size(); ++index) {
        const wchar_t value = text[index];
        if (value >= 0xD800 && value <= 0xDBFF) {
            if (index + 1 >= text.size()) return false;
            const wchar_t next = text[++index];
            if (next < 0xDC00 || next > 0xDFFF) return false;
        } else if (value >= 0xDC00 && value <= 0xDFFF) {
            return false;
        }
    }
    return true;
}

bool IsNativePasswordEdit(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[96] = {};
    if (!GetClassNameW(window, className, static_cast<int>(std::size(className)))) {
        return false;
    }
    const bool editClass = _wcsicmp(className, L"Edit") == 0 ||
        _wcsnicmp(className, L"RichEdit", 8) == 0;
    return editClass &&
        (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD) != 0;
}

bool IsSelectionResultSuccess(const SelectionAcquisitionResult& result) {
    return result.error == SelectionAcquisitionError::None &&
        result.source != SelectionAcquisitionSource::None &&
        ((HasNonWhitespace(result.content.plainText) &&
          IsValidSelectionUtf16(result.content.plainText)) ||
         (HasNonWhitespace(result.content.markdown) &&
          IsValidSelectionUtf16(result.content.markdown)) ||
         (HasNonWhitespace(result.content.html) &&
          IsValidSelectionUtf16(result.content.html)) ||
         (HasNonWhitespace(result.content.structuredPlanJson) &&
          IsValidSelectionUtf16(result.content.structuredPlanJson)));
}

RECT ChooseSelectionAnchor(
    const std::vector<RECT>& lineRectangles, POINT cursor) {
    const RECT* nearest = nullptr;
    unsigned long long nearestDistance =
        (std::numeric_limits<unsigned long long>::max)();
    for (const RECT& rectangle : lineRectangles) {
        if (rectangle.right <= rectangle.left ||
            rectangle.bottom <= rectangle.top ||
            !MonitorFromRect(&rectangle, MONITOR_DEFAULTTONULL)) {
            continue;
        }
        if (PtInRect(&rectangle, cursor)) return rectangle;
        const long long dx = cursor.x < rectangle.left
            ? static_cast<long long>(rectangle.left) - cursor.x
            : (cursor.x >= rectangle.right
                ? static_cast<long long>(cursor.x) - rectangle.right + 1 : 0);
        const long long dy = cursor.y < rectangle.top
            ? static_cast<long long>(rectangle.top) - cursor.y
            : (cursor.y >= rectangle.bottom
                ? static_cast<long long>(cursor.y) - rectangle.bottom + 1 : 0);
        const unsigned long long distance =
            static_cast<unsigned long long>(dx * dx + dy * dy);
        if (!nearest || distance < nearestDistance) {
            nearest = &rectangle;
            nearestDistance = distance;
        }
    }
    return nearest ? *nearest : CursorAnchorRect(cursor);
}

} // namespace selection
