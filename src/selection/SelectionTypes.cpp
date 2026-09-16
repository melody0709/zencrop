#include "SelectionTypes.h"

#include <algorithm>
#include <cwctype>

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

SelectionAcquisitionDisposition ClassifySelectionAcquisition(
    const SelectionAcquisitionResult& result) {
    if (IsSelectionResultSuccess(result)) {
        return SelectionAcquisitionDisposition::SelectedText;
    }
    switch (result.error) {
    case SelectionAcquisitionError::Cancelled:
        return SelectionAcquisitionDisposition::Cancelled;
    case SelectionAcquisitionError::NoSelection:
    case SelectionAcquisitionError::UiaSelectionUnavailable:
    case SelectionAcquisitionError::CopyTimedOut:
    case SelectionAcquisitionError::CopyNotPermittedOrUnsupported:
    case SelectionAcquisitionError::SyntheticCopySuppressed:
        return SelectionAcquisitionDisposition::ManualEntry;
    case SelectionAcquisitionError::None:
        if ((!result.content.plainText.empty() &&
             !IsValidSelectionUtf16(result.content.plainText)) ||
            (!result.content.markdown.empty() &&
             !IsValidSelectionUtf16(result.content.markdown)) ||
            (!result.content.html.empty() &&
             !IsValidSelectionUtf16(result.content.html)) ||
            (!result.content.structuredPlanJson.empty() &&
             !IsValidSelectionUtf16(result.content.structuredPlanJson))) {
            return SelectionAcquisitionDisposition::Error;
        }
        return SelectionAcquisitionDisposition::ManualEntry;
    case SelectionAcquisitionError::SecureField:
    case SelectionAcquisitionError::TextTooLong:
    case SelectionAcquisitionError::TargetChanged:
    case SelectionAcquisitionError::TriggerKeysHeld:
    case SelectionAcquisitionError::CopyShortcutConflict:
    case SelectionAcquisitionError::ClipboardBusy:
    case SelectionAcquisitionError::PlatformError:
        return SelectionAcquisitionDisposition::Error;
    }
    return SelectionAcquisitionDisposition::Error;
}

RECT ChooseSelectionAnchor(
    const std::vector<RECT>& lineRectangles, POINT cursor) {
    RECT anchor = {};
    bool hasAnchor = false;
    for (const RECT& rectangle : lineRectangles) {
        if (rectangle.right <= rectangle.left ||
            rectangle.bottom <= rectangle.top ||
            !MonitorFromRect(&rectangle, MONITOR_DEFAULTTONULL)) {
            continue;
        }
        if (!hasAnchor) {
            anchor = rectangle;
            hasAnchor = true;
        } else {
            anchor.left = (std::min)(anchor.left, rectangle.left);
            anchor.top = (std::min)(anchor.top, rectangle.top);
            anchor.right = (std::max)(anchor.right, rectangle.right);
            anchor.bottom = (std::max)(anchor.bottom, rectangle.bottom);
        }
    }
    return hasAnchor ? anchor : CursorAnchorRect(cursor);
}

namespace {

bool RectsOverlap(const RECT& left, const RECT& right) {
    return left.left < right.right && right.left < left.right &&
        left.top < right.bottom && right.top < left.bottom;
}

bool FitsInside(const RECT& candidate, const RECT& work) {
    return candidate.left >= work.left && candidate.top >= work.top &&
        candidate.right <= work.right && candidate.bottom <= work.bottom;
}

POINT ClampIntoWork(POINT position, int width, int height, const RECT& work) {
    const int left = static_cast<int>(work.left);
    const int top = static_cast<int>(work.top);
    const int maximumX = (std::max)(left, static_cast<int>(work.right) - width);
    const int maximumY = (std::max)(top, static_cast<int>(work.bottom) - height);
    position.x = static_cast<LONG>(
        (std::clamp)(static_cast<int>(position.x), left, maximumX));
    position.y = static_cast<LONG>(
        (std::clamp)(static_cast<int>(position.y), top, maximumY));
    return position;
}

} // namespace

POINT ChooseToastPosition(
    const RECT& work, int width, int height, POINT anchor,
    int offsetX, int offsetY, const RECT* avoid) {
    const int workLeft = static_cast<int>(work.left);
    const int workTop = static_cast<int>(work.top);
    const int workRight = static_cast<int>(work.right);
    const int workBottom = static_cast<int>(work.bottom);
    const int anchorX = static_cast<int>(anchor.x);
    const int anchorY = static_cast<int>(anchor.y);

    int x = anchorX + offsetX;
    int y = anchorY + offsetY;
    // Flip to the opposite side of the anchor when the preferred placement would
    // leave the work area (the cursor can sit right against an edge).
    if (x + width > workRight) x = anchorX - width - offsetX;
    if (y + height > workBottom) y = anchorY - height - offsetY;
    const RECT preferred = {x, y, x + width, y + height};
    if (!avoid || !RectsOverlap(preferred, *avoid)) {
        return ClampIntoWork({x, y}, width, height, work);
    }

    // The preferred spot would cover the avoided window: try the sides of it,
    // then give up and use the work-area corner, which never covers content.
    const int avoidLeft = static_cast<int>(avoid->left);
    const int avoidTop = static_cast<int>(avoid->top);
    const int avoidRight = static_cast<int>(avoid->right);
    const int avoidBottom = static_cast<int>(avoid->bottom);
    const int anchorXInside = (std::max)(
        workLeft, (std::min)(anchorX, workRight - width));
    const int anchorYInside = (std::max)(
        workTop, (std::min)(anchorY, workBottom - height));
    const POINT candidates[4] = {
        {avoidRight + offsetX, anchorYInside},
        {avoidLeft - width - offsetX, anchorYInside},
        {anchorXInside, avoidBottom + offsetY},
        {anchorXInside, avoidTop - height - offsetY},
    };
    for (const POINT candidate : candidates) {
        const RECT rect = {candidate.x, candidate.y,
            candidate.x + width, candidate.y + height};
        if (FitsInside(rect, work) && !RectsOverlap(rect, *avoid)) {
            return candidate;
        }
    }
    return ClampIntoWork(
        {workRight - width - offsetX, workBottom - height - offsetY},
        width, height, work);
}

} // namespace selection
