#pragma once

#include <string>
#include <vector>

class PageRange {
public:
    static bool Parse(
        const std::wstring& rangeText,
        int pageCount,
        std::vector<int>& pages,
        std::wstring& error);
};
