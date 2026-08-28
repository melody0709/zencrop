#include "JsonUtils.h"
#include "WideStringUtils.h"

const wchar_t* kPaddleOcrJobsUrl = L"https://paddleocr.aistudio-app.com/api/v2/ocr/jobs";

// OWN-74: thin wrappers over pure WideStringUtils helpers.
std::wstring TrimString(const std::wstring& value) {
    return WideTrim(value);
}

bool StartsWithNoCase(const std::wstring& value, const std::wstring& prefix) {
    return WideStartsWithNoCase(value, prefix);
}

bool ContainsNoCase(const std::wstring& value, const std::wstring& needle) {
    return WideContainsNoCase(value, needle);
}

std::wstring NormalizePaddleOcrJobsUrl(const std::wstring& input) {
    std::wstring url = WideTrimTrailingSlashes(WideTrim(input));
    if (url.empty()) return kPaddleOcrJobsUrl;
    if (WideEqualsNoCase(url, L"https://paddleocr.aistudio-app.com") ||
        WideEqualsNoCase(url, L"http://paddleocr.aistudio-app.com")) {
        return kPaddleOcrJobsUrl;
    }
    return url;
}

std::wstring EscapeJsonString(const std::wstring& value) {
    return WideEscapeJsonString(value);
}

std::wstring UnescapeJsonString(const std::wstring& input) {
    return WideUnescapeJsonString(input);
}

// OWN-76: thin wrappers over pure WideStringUtils JSON helpers.
size_t SkipJsonWhitespace(const std::wstring& s, size_t pos) {
    return WideSkipJsonWhitespaceBom(s, pos);
}

std::wstring ExtractJsonField(const std::wstring& objStr, const std::wstring& key) {
    return WideExtractJsonField(objStr, key);
}
