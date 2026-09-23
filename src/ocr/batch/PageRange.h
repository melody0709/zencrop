#pragma once

#include <string>
#include <string_view>
#include <vector>

class PageRange {
public:
    static bool Parse(
        std::wstring_view rangeText,
        int pageCount,
        std::vector<int>& pages,
        std::wstring& error);
};
