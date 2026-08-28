#include "PageRange.h"

#include "core/WideStringUtils.h"

namespace {

// OWN-80: thin wrappers over pure WideStringUtils.
std::wstring Trim(const std::wstring& value) {
    return WideTrim(value);
}

std::wstring ToLower(std::wstring value) {
    return WideToLower(std::move(value));
}

bool ParsePositiveInt(const std::wstring& text, int& value) {
    // OWN-80: pure strict int parse; reject non-positive.
    int parsed = 0;
    if (!WideTryParseJsonIntToken(text, parsed) || parsed <= 0) return false;
    value = parsed;
    return true;
}

bool AddRange(
    int start,
    int end,
    int pageCount,
    std::vector<unsigned char>& selected,
    std::wstring& error)
{
    if (start <= 0 || end <= 0) {
        error = L"Page range values must be positive.";
        return false;
    }
    if (start > end) {
        error = L"Page range start must not be greater than end.";
        return false;
    }
    if (start > pageCount || end > pageCount) {
        error = L"Page range exceeds the PDF page count.";
        return false;
    }

    for (int page = start; page <= end; page++) {
        selected[(size_t)page] = 1;
    }
    return true;
}

} // namespace

bool PageRange::Parse(
    const std::wstring& rangeText,
    int pageCount,
    std::vector<int>& pages,
    std::wstring& error)
{
    pages.clear();
    error.clear();

    if (pageCount <= 0) {
        error = L"PDF page count must be positive.";
        return false;
    }

    std::wstring text = ToLower(Trim(rangeText));
    if (text.empty() || text == L"all" || text == L"*") {
        pages.reserve((size_t)pageCount);
        for (int page = 1; page <= pageCount; page++) pages.push_back(page);
        return true;
    }

    std::vector<unsigned char> selected((size_t)pageCount + 1, 0);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t comma = text.find(L',', pos);
        std::wstring part = Trim(text.substr(pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos));
        if (part.empty()) {
            error = L"Page range contains an empty segment.";
            return false;
        }

        size_t dash = part.find(L'-');
        if (dash == std::wstring::npos) {
            int page = 0;
            if (!ParsePositiveInt(part, page)) {
                error = L"Page range contains a non-numeric page.";
                return false;
            }
            if (!AddRange(page, page, pageCount, selected, error)) return false;
        } else {
            if (part.find(L'-', dash + 1) != std::wstring::npos) {
                error = L"Page range contains an invalid range.";
                return false;
            }

            std::wstring left = Trim(part.substr(0, dash));
            std::wstring right = Trim(part.substr(dash + 1));
            if (left.empty() && right.empty()) {
                error = L"Page range contains an empty range.";
                return false;
            }

            int start = 1;
            int end = pageCount;
            if (!left.empty() && !ParsePositiveInt(left, start)) {
                error = L"Page range contains a non-numeric start.";
                return false;
            }
            if (!right.empty() && !ParsePositiveInt(right, end)) {
                error = L"Page range contains a non-numeric end.";
                return false;
            }
            if (!AddRange(start, end, pageCount, selected, error)) return false;
        }

        if (comma == std::wstring::npos) break;
        pos = comma + 1;
    }

    pages.reserve((size_t)pageCount);
    for (int page = 1; page <= pageCount; page++) {
        if (selected[(size_t)page]) pages.push_back(page);
    }
    if (pages.empty()) {
        error = L"Page range selected no pages.";
        return false;
    }
    return true;
}
