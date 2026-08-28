#include "PaddleCloudDocumentNormalizer.h"

#include "OcrDocumentAlignment.h"
#include "OcrBlockJson.h"
#include "OcrPaddleVlJson.h"
#include "Sha256.h"
#include "core/WideStringUtils.h"
#include "dashboard/DashboardFileTypes.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cwctype>
#include <map>
#include <set>
#include <sstream>

namespace {

// OWN-78: thin wrapper over pure WideStringUtils (BOM + whitespace trim).
std::wstring Trim(std::wstring value) {
    return WideTrim(std::move(value));
}

bool ParseJsonSyntaxValue(
    const std::wstring& input,
    size_t& cursor,
    int depth,
    std::wstring& error);

bool ParseJsonSyntaxString(
    const std::wstring& input,
    size_t& cursor,
    std::wstring& error)
{
    if (cursor >= input.size() || input[cursor] != L'\"') return false;
    ++cursor;
    while (cursor < input.size()) {
        const wchar_t ch = input[cursor++];
        if (ch == L'\"') return true;
        if (ch < 0x20) {
            error = L"JSON string contains an unescaped control character.";
            return false;
        }
        if (ch != L'\\') continue;
        if (cursor >= input.size()) break;
        const wchar_t escaped = input[cursor++];
        if (escaped == L'\"' || escaped == L'\\' || escaped == L'/' ||
            escaped == L'b' || escaped == L'f' || escaped == L'n' ||
            escaped == L'r' || escaped == L't') {
            continue;
        }
        if (escaped != L'u' || cursor + 4 > input.size()) {
            error = L"JSON string contains an invalid escape sequence.";
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            if (!iswxdigit(input[cursor + static_cast<size_t>(i)])) {
                error = L"JSON string contains an invalid Unicode escape.";
                return false;
            }
        }
        cursor += 4;
    }
    error = L"JSON string is truncated.";
    return false;
}

bool IsJsonWhitespace(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

void SkipSyntaxWhitespace(const std::wstring& input, size_t& cursor) {
    while (cursor < input.size() && IsJsonWhitespace(input[cursor])) {
        ++cursor;
    }
}

bool ParseJsonSyntaxNumber(
    const std::wstring& input,
    size_t& cursor,
    std::wstring& error)
{
    const size_t start = cursor;
    if (cursor < input.size() && input[cursor] == L'-') ++cursor;
    if (cursor >= input.size()) return false;
    if (input[cursor] == L'0') {
        ++cursor;
        if (cursor < input.size() && iswdigit(input[cursor])) {
            error = L"JSON number has a leading zero.";
            return false;
        }
    } else if (input[cursor] >= L'1' && input[cursor] <= L'9') {
        while (cursor < input.size() && iswdigit(input[cursor])) ++cursor;
    } else {
        return false;
    }
    if (cursor < input.size() && input[cursor] == L'.') {
        ++cursor;
        const size_t fraction = cursor;
        while (cursor < input.size() && iswdigit(input[cursor])) ++cursor;
        if (cursor == fraction) {
            error = L"JSON number has an empty fractional part.";
            return false;
        }
    }
    if (cursor < input.size() && (input[cursor] == L'e' || input[cursor] == L'E')) {
        ++cursor;
        if (cursor < input.size() && (input[cursor] == L'+' || input[cursor] == L'-')) ++cursor;
        const size_t exponent = cursor;
        while (cursor < input.size() && iswdigit(input[cursor])) ++cursor;
        if (cursor == exponent) {
            error = L"JSON number has an empty exponent.";
            return false;
        }
    }
    return cursor > start;
}

bool ParseJsonSyntaxValue(
    const std::wstring& input,
    size_t& cursor,
    int depth,
    std::wstring& error)
{
    if (depth > 128) {
        error = L"JSON nesting exceeds the parser safety limit.";
        return false;
    }
    SkipSyntaxWhitespace(input, cursor);
    if (cursor >= input.size()) return false;
    if (input[cursor] == L'\"') return ParseJsonSyntaxString(input, cursor, error);
    if (input[cursor] == L'{') {
        ++cursor;
        SkipSyntaxWhitespace(input, cursor);
        if (cursor < input.size() && input[cursor] == L'}') {
            ++cursor;
            return true;
        }
        for (;;) {
            if (!ParseJsonSyntaxString(input, cursor, error)) {
                if (error.empty()) error = L"JSON object key is not a string.";
                return false;
            }
            SkipSyntaxWhitespace(input, cursor);
            if (cursor >= input.size() || input[cursor++] != L':') {
                error = L"JSON object key has no colon.";
                return false;
            }
            if (!ParseJsonSyntaxValue(input, cursor, depth + 1, error)) {
                if (error.empty()) error = L"JSON object value is invalid.";
                return false;
            }
            SkipSyntaxWhitespace(input, cursor);
            if (cursor < input.size() && input[cursor] == L'}') {
                ++cursor;
                return true;
            }
            if (cursor >= input.size() || input[cursor++] != L',') {
                error = L"JSON object members are not comma-separated.";
                return false;
            }
            SkipSyntaxWhitespace(input, cursor);
        }
    }
    if (input[cursor] == L'[') {
        ++cursor;
        SkipSyntaxWhitespace(input, cursor);
        if (cursor < input.size() && input[cursor] == L']') {
            ++cursor;
            return true;
        }
        for (;;) {
            if (!ParseJsonSyntaxValue(input, cursor, depth + 1, error)) {
                if (error.empty()) error = L"JSON array item is invalid.";
                return false;
            }
            SkipSyntaxWhitespace(input, cursor);
            if (cursor < input.size() && input[cursor] == L']') {
                ++cursor;
                return true;
            }
            if (cursor >= input.size() || input[cursor++] != L',') {
                error = L"JSON array items are not comma-separated.";
                return false;
            }
            SkipSyntaxWhitespace(input, cursor);
        }
    }
    for (const wchar_t* literal : {L"true", L"false", L"null"}) {
        const size_t length = wcslen(literal);
        if (input.compare(cursor, length, literal) == 0) {
            cursor += length;
            return true;
        }
    }
    if (input[cursor] == L'-' || iswdigit(input[cursor])) {
        return ParseJsonSyntaxNumber(input, cursor, error);
    }
    error = L"JSON value starts with an unsupported token.";
    return false;
}

bool ValidateJsonObjectSyntax(const std::wstring& object, std::wstring& error) {
    error.clear();
    size_t cursor = 0;
    if (cursor < object.size() && object[cursor] == 0xFEFF) ++cursor;
    SkipSyntaxWhitespace(object, cursor);
    if (cursor >= object.size() || object[cursor] != L'{' ||
        !ParseJsonSyntaxValue(object, cursor, 0, error)) {
        if (error.empty()) error = L"JSONL record is not a valid JSON object.";
        return false;
    }
    SkipSyntaxWhitespace(object, cursor);
    if (cursor != object.size()) {
        error = L"JSONL record contains trailing data.";
        return false;
    }
    return true;
}

bool ExtractTopLevelJsonValue(
    const std::wstring& object,
    const std::wstring& wantedKey,
    std::wstring& value)
{
    value.clear();
    size_t cursor = 0;
    SkipSyntaxWhitespace(object, cursor);
    if (cursor >= object.size() || object[cursor++] != L'{') return false;
    for (;;) {
        SkipSyntaxWhitespace(object, cursor);
        if (cursor >= object.size() || object[cursor] == L'}') return false;
        const size_t quote = cursor;
        std::wstring parseError;
        if (!ParseJsonSyntaxString(object, cursor, parseError)) return false;
        const std::wstring key = UnescapeJsonString(
            object.substr(quote + 1, cursor - quote - 2));
        SkipSyntaxWhitespace(object, cursor);
        if (cursor >= object.size() || object[cursor++] != L':') return false;
        SkipSyntaxWhitespace(object, cursor);
        const size_t valueStart = cursor;
        if (!ParseJsonSyntaxValue(object, cursor, 1, parseError)) return false;
        if (key == wantedKey) {
            value = object.substr(valueStart, cursor - valueStart);
            if (value.size() >= 2 && value.front() == L'\"' && value.back() == L'\"') {
                value = value.substr(1, value.size() - 2);
            }
            return true;
        }
        SkipSyntaxWhitespace(object, cursor);
        if (cursor >= object.size() || object[cursor] == L'}') return false;
        if (object[cursor++] != L',') return false;
    }
}

std::wstring TopLevelJsonValue(const std::wstring& object, const wchar_t* key) {
    std::wstring value;
    ExtractTopLevelJsonValue(object, key, value);
    return value;
}

bool ExtractObjectArrayStrict(
    const std::wstring& arrayText,
    std::vector<std::wstring>& objects,
    std::wstring& error)
{
    objects.clear();
    size_t cursor = 0;
    SkipSyntaxWhitespace(arrayText, cursor);
    if (cursor >= arrayText.size() || arrayText[cursor++] != L'[') {
        error = L"Provider page collection is not an array.";
        return false;
    }
    SkipSyntaxWhitespace(arrayText, cursor);
    if (cursor < arrayText.size() && arrayText[cursor] == L']') return true;
    for (;;) {
        SkipSyntaxWhitespace(arrayText, cursor);
        if (cursor >= arrayText.size() || arrayText[cursor] != L'{') {
            error = L"Provider page collection contains a non-object item.";
            return false;
        }
        const size_t end = OcrBlockJsonFindMatching(arrayText, cursor, L'{', L'}');
        if (end == std::wstring::npos) {
            error = L"Provider page collection contains a truncated object.";
            return false;
        }
        objects.push_back(arrayText.substr(cursor, end - cursor + 1));
        cursor = end + 1;
        SkipSyntaxWhitespace(arrayText, cursor);
        if (cursor < arrayText.size() && arrayText[cursor] == L']') return true;
        if (cursor >= arrayText.size() || arrayText[cursor++] != L',') {
            error = L"Provider page objects are not comma-separated.";
            return false;
        }
    }
}

bool ExtractJsonObjects(
    const std::wstring& input,
    std::vector<std::wstring>& objects,
    std::wstring& error)
{
    objects.clear();
    error.clear();
    size_t cursor = 0;
    while (cursor < input.size()) {
        while (cursor < input.size() &&
            (input[cursor] == 0xFEFF || IsJsonWhitespace(input[cursor]))) {
            ++cursor;
        }
        if (cursor >= input.size()) break;
        if (input[cursor] != L'{') {
            // OWN-126: pure UTF-16 offset error suffix (WideStringUtils).
            error = L"JSONL contains a non-object record" +
                WideFormatUtf16OffsetSuffix(static_cast<unsigned long long>(cursor));
            return false;
        }
        size_t end = OcrBlockJsonFindMatching(input, cursor, L'{', L'}');
        if (end == std::wstring::npos) {
            error = L"JSONL contains a truncated object record.";
            return false;
        }
        std::wstring object = input.substr(cursor, end - cursor + 1);
        if (!ValidateJsonObjectSyntax(object, error)) return false;
        objects.push_back(std::move(object));
        cursor = end + 1;
        while (cursor < input.size() && input[cursor] != L'{') {
            if (input[cursor] != 0xFEFF && !IsJsonWhitespace(input[cursor])) {
                error = L"JSONL contains trailing data after an object record.";
                return false;
            }
            ++cursor;
        }
    }
    if (objects.empty()) {
        error = L"JSONL contains no object records.";
        return false;
    }
    return true;
}

std::vector<std::wstring> ExtractJsonStringArray(const std::wstring& object, const wchar_t* key) {
    std::vector<std::wstring> values;
    const std::wstring array = TopLevelJsonValue(object, key);
    size_t cursor = array.find(L'[');
    if (cursor == std::wstring::npos) return values;
    ++cursor;
    while (cursor < array.size()) {
        while (cursor < array.size() && (iswspace(array[cursor]) || array[cursor] == L',')) ++cursor;
        if (cursor >= array.size() || array[cursor] == L']') break;
        if (array[cursor] != L'\"') return {};
        const size_t begin = ++cursor;
        while (cursor < array.size()) {
            if (array[cursor] == L'\\' && cursor + 1 < array.size()) {
                cursor += 2;
                continue;
            }
            if (array[cursor] == L'\"') break;
            ++cursor;
        }
        if (cursor >= array.size()) return {};
        values.push_back(UnescapeJsonString(array.substr(begin, cursor - begin)));
        ++cursor;
    }
    return values;
}

std::vector<std::wstring> ExtractHttpStringValues(const std::wstring& jsonValue) {
    std::vector<std::wstring> urls;
    size_t cursor = 0;
    while (cursor < jsonValue.size()) {
        size_t quote = jsonValue.find(L'\"', cursor);
        if (quote == std::wstring::npos) break;
        size_t end = quote + 1;
        while (end < jsonValue.size()) {
            if (jsonValue[end] == L'\\' && end + 1 < jsonValue.size()) {
                end += 2;
                continue;
            }
            if (jsonValue[end] == L'\"') break;
            ++end;
        }
        if (end >= jsonValue.size()) break;
        std::wstring value = UnescapeJsonString(jsonValue.substr(quote + 1, end - quote - 1));
        // OWN-78: pure lower (WideStringUtils).
        const std::wstring lower = WideToLower(value);
        if (lower.rfind(L"https://", 0) == 0 || lower.rfind(L"http://", 0) == 0) {
            urls.push_back(std::move(value));
        }
        cursor = end + 1;
    }
    return urls;
}

std::vector<std::pair<std::wstring, std::wstring>> ExtractJsonStringMap(
    const std::wstring& object)
{
    std::vector<std::pair<std::wstring, std::wstring>> values;
    size_t cursor = 0;
    SkipSyntaxWhitespace(object, cursor);
    if (cursor >= object.size() || object[cursor++] != L'{') return values;
    SkipSyntaxWhitespace(object, cursor);
    if (cursor < object.size() && object[cursor] == L'}') return values;
    while (cursor < object.size() && values.size() < 1000) {
        SkipSyntaxWhitespace(object, cursor);
        if (cursor >= object.size() || object[cursor] != L'"') return {};
        const size_t keyStart = ++cursor;
        while (cursor < object.size()) {
            if (object[cursor] == L'\\' && cursor + 1 < object.size()) {
                cursor += 2;
                continue;
            }
            if (object[cursor] == L'"') break;
            ++cursor;
        }
        if (cursor >= object.size()) return {};
        std::wstring key = UnescapeJsonString(object.substr(keyStart, cursor - keyStart));
        ++cursor;
        SkipSyntaxWhitespace(object, cursor);
        if (cursor >= object.size() || object[cursor++] != L':') return {};
        SkipSyntaxWhitespace(object, cursor);
        if (cursor >= object.size() || object[cursor] != L'"') return {};
        const size_t valueStart = ++cursor;
        while (cursor < object.size()) {
            if (object[cursor] == L'\\' && cursor + 1 < object.size()) {
                cursor += 2;
                continue;
            }
            if (object[cursor] == L'"') break;
            ++cursor;
        }
        if (cursor >= object.size()) return {};
        std::wstring value = UnescapeJsonString(object.substr(valueStart, cursor - valueStart));
        ++cursor;
        values.emplace_back(std::move(key), std::move(value));
        SkipSyntaxWhitespace(object, cursor);
        if (cursor < object.size() && object[cursor] == L'}') return values;
        if (cursor >= object.size() || object[cursor++] != L',') return {};
    }
    return {};
}

std::vector<std::wstring> ExtractMarkdownImageUrls(const std::wstring& markdown) {
    std::vector<std::wstring> urls;
    size_t cursor = 0;
    while (cursor < markdown.size()) {
        const size_t image = markdown.find(L"![", cursor);
        if (image == std::wstring::npos) break;
        const size_t altEnd = markdown.find(L']', image + 2);
        if (altEnd == std::wstring::npos || altEnd + 1 >= markdown.size() ||
            markdown[altEnd + 1] != L'(') {
            cursor = image + 2;
            continue;
        }
        size_t begin = altEnd + 2;
        while (begin < markdown.size() && iswspace(markdown[begin])) ++begin;
        size_t end = begin;
        while (end < markdown.size() && markdown[end] != L')' && !iswspace(markdown[end])) ++end;
        std::wstring url = markdown.substr(begin, end - begin);
        // OWN-78: pure lower (WideStringUtils).
        const std::wstring lower = WideToLower(url);
        if (lower.rfind(L"https://", 0) == 0 || lower.rfind(L"http://", 0) == 0) {
            urls.push_back(std::move(url));
        }
        cursor = end < markdown.size() ? end + 1 : markdown.size();
    }
    return urls;
}

// OWN-78: pure strict int parse (WideStringUtils) with range check.
bool ParseStrictPageInteger(
    const std::wstring& raw,
    long minimum,
    long maximum,
    long& value)
{
    int parsed = 0;
    if (!WideTryParseJsonIntToken(raw, parsed)) return false;
    if (parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}

bool ExtractExplicitOriginalPageNumber(
    const std::wstring& pageObject,
    int& pageNumber,
    bool& explicitIdentity,
    std::wstring& error)
{
    pageNumber = 0;
    explicitIdentity = false;
    for (const wchar_t* key : {
            L"pageNumber", L"pageNo", L"page_num", L"page_number", L"page"}) {
        std::wstring raw;
        if (!ExtractTopLevelJsonValue(pageObject, key, raw)) continue;
        explicitIdentity = true;
        long parsed = 0;
        if (!ParseStrictPageInteger(raw, 1, INT_MAX, parsed)) {
            error = L"Provider page result contains an invalid explicit page number.";
            return false;
        }
        pageNumber = static_cast<int>(parsed);
        return true;
    }

    std::wstring rawIndex;
    if (!ExtractTopLevelJsonValue(pageObject, L"pageIndex", rawIndex)) return true;
    explicitIdentity = true;
    long zeroBased = 0;
    if (!ParseStrictPageInteger(rawIndex, 0, INT_MAX - 1, zeroBased)) {
        error = L"Provider page result contains an invalid zero-based pageIndex.";
        return false;
    }
    pageNumber = static_cast<int>(zeroBased + 1);
    return true;
}

std::wstring ExtractMarkdown(const std::wstring& pageObject, const std::wstring& pruned) {
    auto fromObject = [](const std::wstring& object) -> std::wstring {
        std::wstring markdown = TopLevelJsonValue(object, L"markdown");
        if (markdown.empty()) markdown = TopLevelJsonValue(object, L"markdownText");
        if (markdown.empty()) markdown = TopLevelJsonValue(object, L"markdown_text");
        if (markdown.empty()) return L"";
        std::wstring trimmed = Trim(markdown);
        if (!trimmed.empty() && trimmed.front() == L'{') {
            std::wstring text = OcrBlockJsonText(trimmed, L"text");
            if (text.empty()) text = OcrBlockJsonText(trimmed, L"markdown");
            return text;
        }
        return UnescapeJsonString(markdown);
    };

    std::wstring markdown = fromObject(pageObject);
    if (markdown.empty()) markdown = fromObject(pruned);
    return markdown;
}

std::wstring ExtractPlainText(const std::wstring& pageObject, const std::wstring& pruned) {
    const wchar_t* keys[] = {L"plainText", L"plain_text", L"text"};
    for (const wchar_t* key : keys) {
        std::wstring value = UnescapeJsonString(TopLevelJsonValue(pageObject, key));
        if (!value.empty()) return value;
        value = UnescapeJsonString(TopLevelJsonValue(pruned, key));
        if (!value.empty()) return value;
    }
    for (const std::wstring* object : {&pageObject, &pruned}) {
        std::vector<std::wstring> lines = ExtractJsonStringArray(*object, L"rec_texts");
        if (lines.empty()) lines = ExtractJsonStringArray(*object, L"recTexts");
        if (!lines.empty()) {
            std::wstring text;
            for (const auto& line : lines) {
                if (!text.empty()) text += L"\n";
                text += line;
            }
            return text;
        }
    }
    return L"";
}

std::wstring ExtractRemoteUrl(
    const std::wstring& pageObject,
    std::initializer_list<const wchar_t*> keys)
{
    for (const wchar_t* key : keys) {
        std::wstring value = TopLevelJsonValue(pageObject, key);
        if (value.empty()) continue;
        std::wstring trimmed = Trim(value);
        if (!trimmed.empty() && trimmed.front() == L'{') {
            std::wstring url = OcrBlockJsonText(trimmed, L"url");
            if (url.empty()) url = OcrBlockJsonText(trimmed, L"fileUrl");
            if (!url.empty()) return url;
        } else {
            return UnescapeJsonString(value);
        }
    }
    return L"";
}

// OWN-73: thin wrapper over pure DashboardFormatPageIndexName.
std::wstring StablePageId(int pageNumber) {
    return DashboardFormatPageIndexName(pageNumber);
}

void AppendResource(
    DocumentOcrPageResult& page,
    const std::wstring& kind,
    const std::wstring& url,
    bool required,
    const std::wstring& providerPath = L"")
{
    if (url.empty()) return;
    for (const auto& existing : page.resources) {
        if (existing.kind == kind && existing.remoteUrl == url &&
            existing.localPath == providerPath) return;
    }
    DocumentOcrResourceDescriptor resource;
    resource.kind = kind;
    resource.remoteUrl = url;
    // During normalization this field preserves the provider-side relative
    // Markdown reference (for example imgs/figure.jpg). Materialization
    // replaces it with a real local asset path before anything is persisted.
    resource.localPath = providerPath;
    resource.required = required;
    page.resources.push_back(std::move(resource));
}

bool ValidateRequestedPages(
    const std::vector<int>& requested,
    std::wstring& error)
{
    if (requested.empty()) {
        error = L"Requested page list is empty.";
        return false;
    }
    if (requested.size() > 100) {
        error = L"Requested page list exceeds the conservative 100-page document limit.";
        return false;
    }
    int previous = 0;
    for (int page : requested) {
        if (page <= 0 || page <= previous) {
            error = L"Requested page numbers must be positive, unique, and ascending.";
            return false;
        }
        previous = page;
    }
    return true;
}

bool AppendPageObject(
    const std::wstring& pageObject,
    int ordinal,
    DocumentOcrPageResult& page,
    std::wstring& error)
{
    page = DocumentOcrPageResult{};
    page.resultOrdinal = ordinal;
    page.rawJson = pageObject;

    int explicitPage = 0;
    if (!ExtractExplicitOriginalPageNumber(
            pageObject,
            explicitPage,
            page.originalPageNumberExplicit,
            error)) {
        return false;
    }
    if (page.originalPageNumberExplicit) {
        page.originalPageNumber = explicitPage;
    }

    std::wstring pruned = TopLevelJsonValue(pageObject, L"prunedResult");
    if (pruned.empty()) pruned = TopLevelJsonValue(pageObject, L"pruned_result");
    if (pruned.empty()) pruned = pageObject;

    page.coordinateSpace.recognitionImageWidth = static_cast<uint32_t>((std::max)(
        0,
        OcrBlockJsonInt(pruned, L"width", OcrBlockJsonInt(pageObject, L"width", 0))));
    page.coordinateSpace.recognitionImageHeight = static_cast<uint32_t>((std::max)(
        0,
        OcrBlockJsonInt(pruned, L"height", OcrBlockJsonInt(pageObject, L"height", 0))));
    page.coordinateSpace.coordinateSpaceKind = L"paddleocr_pruned_result_pixels";

    page.markdown = ExtractMarkdown(pageObject, pruned);
    page.plainText = ExtractPlainText(pageObject, pruned);
    if (page.markdown.empty()) page.markdown = page.plainText;
    if (page.plainText.empty()) page.plainText = page.markdown;
    page.canonicalSourceMarkdown = CanonicalizeOcrMarkdownSource(page.markdown);

    std::wstring list = TopLevelJsonValue(pruned, L"parsing_res_list");
    if (list.empty()) list = TopLevelJsonValue(pageObject, L"parsing_res_list");
    if (!list.empty()) AppendPaddleVlBlocksFromList(list, 0, page.blocks);

    std::wstring inputImage = ExtractRemoteUrl(
        pageObject,
        {L"inputImage", L"input_image", L"inputImageUrl", L"input_image_url"});
    if (inputImage.empty()) {
        inputImage = ExtractRemoteUrl(
            pruned,
            {L"inputImage", L"input_image", L"inputImageUrl", L"input_image_url"});
    }
    if (!inputImage.empty()) {
        AppendResource(page, L"recognition_image", inputImage, true);
    }

    std::wstring preprocessingImage = ExtractRemoteUrl(
        pageObject,
        {L"docPreprocessingImage", L"doc_preprocessing_image"});
    if (preprocessingImage.empty()) {
        preprocessingImage = ExtractRemoteUrl(
            pruned,
            {L"docPreprocessingImage", L"doc_preprocessing_image"});
    }
    AppendResource(page, L"recognition_image", preprocessingImage, true);
    AppendResource(
        page,
        L"ocr_output_image",
        ExtractRemoteUrl(pageObject, {L"ocrImage", L"ocr_image"}),
        false);
    std::wstring outputImages = TopLevelJsonValue(pageObject, L"outputImages");
    if (outputImages.empty()) outputImages = TopLevelJsonValue(pageObject, L"output_images");
    for (const auto& url : ExtractHttpStringValues(outputImages)) {
        AppendResource(page, L"output_image", url, false);
    }
    std::wstring markdownObject = TopLevelJsonValue(pageObject, L"markdown");
    if (!markdownObject.empty() && markdownObject.front() == L'{') {
        const std::wstring markdownImages = TopLevelJsonValue(markdownObject, L"images");
        for (const auto& entry : ExtractJsonStringMap(markdownImages)) {
            // OWN-78: pure lower (WideStringUtils).
            const std::wstring lowerUrl = WideToLower(entry.second);
            if (lowerUrl.rfind(L"https://", 0) == 0 || lowerUrl.rfind(L"http://", 0) == 0) {
                AppendResource(page, L"markdown_image", entry.second, false, entry.first);
            }
        }
    }
    for (const auto& url : ExtractMarkdownImageUrls(page.markdown)) {
        AppendResource(page, L"markdown_image", url, false);
    }

    if (page.markdown.empty() && page.blocks.empty() && page.resources.empty()) {
        error = L"Provider page result contains no text, layout blocks, or recognized resources.";
        return false;
    }
    return true;
}

void AddPageWarning(DocumentOcrPageResult& page, const std::wstring& warning) {
    if (warning.empty()) return;
    if (!page.warning.empty()) page.warning += L" ";
    page.warning += warning;
}

} // namespace

bool ValidatePaddleCloudJsonObjectSyntax(
    const std::wstring& json,
    std::wstring& error)
{
    return ValidateJsonObjectSyntax(json, error);
}

bool NormalizePaddleCloudDocumentJsonl(
    const std::wstring& jsonl,
    const std::vector<int>& requestedOriginalPageNumbers,
    const PaddleCloudDocumentNormalizeOptions& options,
    DocumentOcrResult& result)
{
    result = DocumentOcrResult{};
    result.provider = L"paddleocr_official_api";
    result.model = options.model.empty() ? L"PaddleOCR-VL-1.6" : options.model;
    result.requestedOriginalPageNumbers = requestedOriginalPageNumbers;

    if (jsonl.size() > 64ull * 1024ull * 1024ull) {
        result.error = L"Decoded JSONL exceeds the 64 Mi UTF-16 code-unit safety limit.";
        return false;
    }

    if (!ValidateRequestedPages(requestedOriginalPageNumbers, result.error)) return false;
    std::wstring hashError;
    if (!ComputeUtf8Sha256Hex(jsonl, result.rawJsonlSha256, hashError)) {
        result.error = hashError;
        return false;
    }

    std::vector<std::wstring> records;
    if (!ExtractJsonObjects(jsonl, records, result.error)) return false;

    int ordinal = 0;
    for (const auto& record : records) {
        std::wstring payload = TopLevelJsonValue(record, L"result");
        if (payload.empty() || payload.front() != L'{') payload = record;
        std::wstring layout = TopLevelJsonValue(payload, L"layoutParsingResults");
        if (layout.empty()) layout = TopLevelJsonValue(payload, L"layout_parsing_results");
        std::wstring ocrResults;
        if (layout.empty()) {
            ocrResults = TopLevelJsonValue(payload, L"ocrResults");
            if (ocrResults.empty()) ocrResults = TopLevelJsonValue(payload, L"ocr_results");
        }
        const std::wstring& collection = layout.empty() ? ocrResults : layout;
        std::vector<std::wstring> pageObjects;
        if (!collection.empty()) {
            if (!ExtractObjectArrayStrict(collection, pageObjects, result.error)) return false;
            if (pageObjects.empty()) {
                result.error = L"Provider page collection is empty.";
                return false;
            }
        } else {
            const bool directPage = !TopLevelJsonValue(payload, L"prunedResult").empty() ||
                !TopLevelJsonValue(payload, L"pruned_result").empty() ||
                !TopLevelJsonValue(payload, L"markdown").empty() ||
                !TopLevelJsonValue(payload, L"ocrImage").empty();
            if (!directPage) {
                result.error = L"JSONL record has no supported document page payload.";
                return false;
            }
            pageObjects.push_back(payload);
        }

        for (const auto& pageObject : pageObjects) {
            DocumentOcrPageResult page;
            if (!AppendPageObject(pageObject, ordinal++, page, result.error)) return false;
            result.pages.push_back(std::move(page));
        }
    }

    if (options.serverRestructureEnabled &&
        result.pages.size() != requestedOriginalPageNumbers.size()) {
        result.error = L"Server restructure changed page cardinality; page-specific normalization is unsafe.";
        return false;
    }
    if (result.pages.size() != requestedOriginalPageNumbers.size()) {
        result.error = L"Provider page count does not match the requested page count.";
        return false;
    }

    size_t explicitCount = 0;
    for (const auto& page : result.pages) {
        if (page.originalPageNumberExplicit) ++explicitCount;
    }
    if (explicitCount != 0 && explicitCount != result.pages.size()) {
        result.error = L"Provider mixed explicit and implicit page identities.";
        return false;
    }
    if (explicitCount == 0 && !options.allowStrictOrdinalFallback) {
        result.error = L"Provider omitted explicit page identities and ordinal fallback is disabled.";
        return false;
    }

    std::set<int> requestedSet(
        requestedOriginalPageNumbers.begin(),
        requestedOriginalPageNumbers.end());
    std::set<int> returnedSet;
    for (size_t i = 0; i < result.pages.size(); ++i) {
        auto& page = result.pages[i];
        if (!page.originalPageNumberExplicit) {
            page.originalPageNumber = requestedOriginalPageNumbers[i];
        } else if (page.originalPageNumber != requestedOriginalPageNumbers[i]) {
            // OWN-126: pure int-dot error suffix (WideStringUtils).
            result.error = WideFormatIntDotSuffix(
                L"Provider explicit page number conflicts with result ordinal ",
                static_cast<int>(i));
            return false;
        }
        if (requestedSet.find(page.originalPageNumber) == requestedSet.end()) {
            // OWN-126: pure int-dot error suffix (WideStringUtils).
            result.error = WideFormatIntDotSuffix(
                L"Provider returned an unrequested original page number: ",
                page.originalPageNumber);
            return false;
        }
        if (!returnedSet.insert(page.originalPageNumber).second) {
            // OWN-126: pure int-dot error suffix (WideStringUtils).
            result.error = WideFormatIntDotSuffix(
                L"Provider returned a duplicate original page number: ",
                page.originalPageNumber);
            return false;
        }

        page.stablePageId = StablePageId(page.originalPageNumber);
        page.blocks = OcrLayoutBlocksForPage(page.blocks, page.originalPageNumber);
        page.alignment.pageIdentity = OcrAlignmentState::Verified;

        std::wstring semanticError;
        if (!BuildVerifiedBlockSourceMap(
                page.canonicalSourceMarkdown,
                page.blocks,
                page.blockSourceMap,
                page.sourceRevisionSha256,
                page.alignment.semantic,
                semanticError)) {
            // OWN-126: pure int-colon detail (WideStringUtils).
            result.error = WideFormatIntColonDetail(
                L"Failed to build block source map for page ",
                page.originalPageNumber,
                semanticError);
            return false;
        }
        if (!semanticError.empty()) AddPageWarning(page, semanticError);

        std::wstring geometryError;
        ValidateDocumentPageGeometry(
            page.coordinateSpace,
            page.blocks,
            page.alignment.geometry,
            geometryError);
        if (!geometryError.empty()) AddPageWarning(page, geometryError);
        RefreshDocumentPageOverallAlignment(page);
    }

    if (returnedSet != requestedSet) {
        result.error = L"Provider page identities do not exactly match the requested page set.";
        return false;
    }

    result.success = true;
    return true;
}

std::wstring BuildCanonicalCloudPageRanges(const std::vector<int>& pages) {
    if (pages.empty()) return L"";
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i] <= 0 || (i > 0 && pages[i] <= pages[i - 1])) return L"";
    }

    std::wstringstream ss;
    size_t i = 0;
    while (i < pages.size()) {
        size_t end = i;
        while (end + 1 < pages.size() && pages[end + 1] == pages[end] + 1) ++end;
        if (i > 0) ss << L",";
        if (end == i) {
            ss << pages[i];
        } else {
            ss << pages[i] << L"-" << pages[end];
        }
        i = end + 1;
    }
    return ss.str();
}
