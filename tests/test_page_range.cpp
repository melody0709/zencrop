#include "ocr/batch/PageRange.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int Fail(const std::wstring& message) {
    std::wcerr << L"FAIL: " << message << L"\n";
    return 1;
}

bool Equal(const std::vector<int>& actual, std::initializer_list<int> expected) {
    return actual == std::vector<int>(expected);
}

bool ExpectOk(const std::wstring& text, int pageCount, std::initializer_list<int> expected) {
    std::vector<int> pages;
    std::wstring error;
    if (!PageRange::Parse(text, pageCount, pages, error)) {
        std::wcerr << L"FAIL: expected parse success for [" << text << L"], got: " << error << L"\n";
        return false;
    }
    if (!Equal(pages, expected)) {
        std::wcerr << L"FAIL: unexpected pages for [" << text << L"]\n";
        return false;
    }
    return true;
}

bool ExpectFail(const std::wstring& text, int pageCount) {
    std::vector<int> pages;
    std::wstring error;
    if (PageRange::Parse(text, pageCount, pages, error)) {
        std::wcerr << L"FAIL: expected parse failure for [" << text << L"]\n";
        return false;
    }
    if (error.empty()) {
        std::wcerr << L"FAIL: expected parse error for [" << text << L"]\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!ExpectOk(L"", 5, {1, 2, 3, 4, 5})) return 1;
    if (!ExpectOk(L" all ", 3, {1, 2, 3})) return 1;
    if (!ExpectOk(L"*", 2, {1, 2})) return 1;
    if (!ExpectOk(L"3", 5, {3})) return 1;
    if (!ExpectOk(L"1-3", 5, {1, 2, 3})) return 1;
    if (!ExpectOk(L"3-", 5, {3, 4, 5})) return 1;
    if (!ExpectOk(L"-3", 5, {1, 2, 3})) return 1;
    if (!ExpectOk(L"1-3, 3, 5, 2", 5, {1, 2, 3, 5})) return 1;
    if (!ExpectOk(L"2, 5-6, 8-", 10, {2, 5, 6, 8, 9, 10})) return 1;

    if (!ExpectFail(L"0", 5)) return 1;
    if (!ExpectFail(L"6", 5)) return 1;
    if (!ExpectFail(L"3-2", 5)) return 1;
    if (!ExpectFail(L"1,,2", 5)) return 1;
    if (!ExpectFail(L"abc", 5)) return 1;
    if (!ExpectFail(L"1-a", 5)) return 1;
    if (!ExpectFail(L"1--3", 5)) return 1;
    if (!ExpectFail(L"-", 5)) return 1;
    if (!ExpectFail(L"1", 0)) return 1;

    std::wcout << L"PageRange tests passed.\n";
    return 0;
}
