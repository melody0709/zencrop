#include "SelectionStructuredContent.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace selection {
namespace {

bool Utf8ToWide(std::string_view value, std::wstring& result) {
    result.clear();
    if (value.empty()) return true;
    if (value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return false;
    result.resize(static_cast<size_t>(required));
    return MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required) == required;
}

bool WideToUtf8(const std::wstring& value, std::string& result) {
    result.clear();
    if (value.empty()) return true;
    if (value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return false;
    result.resize(static_cast<size_t>(required));
    return WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required,
        nullptr, nullptr) == required;
}

bool IsSafeRequestToken(const std::wstring& token) {
    return token.size() == 32 && std::all_of(token.begin(), token.end(),
        [](wchar_t ch) {
            return (ch >= L'0' && ch <= L'9') ||
                (ch >= L'a' && ch <= L'f') ||
                (ch >= L'A' && ch <= L'F');
        });
}

bool HeaderValue(
    std::string_view bytes, std::string_view name, std::string_view& value) {
    size_t lineStart = 0;
    while (lineStart < bytes.size() && lineStart < 64 * 1024) {
        size_t lineEnd = bytes.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) lineEnd = bytes.size();
        std::string_view line = bytes.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() > name.size() &&
            _strnicmp(line.data(), name.data(), name.size()) == 0 &&
            line[name.size()] == ':') {
            value = line.substr(name.size() + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                value.remove_suffix(1);
            }
            return true;
        }
        if (lineEnd == bytes.size()) break;
        lineStart = lineEnd + 1;
    }
    return false;
}

bool HeaderOffset(
    std::string_view bytes, std::string_view name, size_t& value) {
    std::string_view text;
    if (!HeaderValue(bytes, name, text) || text.empty() || text.front() == '-') {
        return false;
    }
    unsigned long long parsed = 0;
    const auto converted = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (converted.ec != std::errc() || converted.ptr != text.data() + text.size() ||
        parsed > (std::numeric_limits<size_t>::max)()) {
        return false;
    }
    value = static_cast<size_t>(parsed);
    return true;
}

std::wstring JsonWideString(const nlohmann::json& value) {
    if (!value.is_string()) return {};
    std::wstring result;
    const std::string utf8 = value.get<std::string>();
    return Utf8ToWide(utf8, result) ? result : std::wstring();
}

std::wstring EscapeMarkdownText(const std::wstring& value) {
    if (value.empty()) return {};
    std::wstring escaped;
    escaped.reserve(value.size() + value.size() / 4);
    for (const wchar_t ch : value) {
        if (ch == L'\\' || ch == L'*' || ch == L'`' ||
            ch == L'[' || ch == L']' || ch == L'_') {
            escaped.push_back(L'\\');
        }
        escaped.push_back(ch);
    }
    if (!escaped.empty() && escaped.front() == L'-') {
        escaped.insert(escaped.begin(), L'\\');
    } else if (escaped.starts_with(L"+ ")) {
        escaped.insert(escaped.begin(), L'\\');
    } else if (escaped.front() == L'=') {
        escaped.insert(escaped.begin(), L'\\');
    } else if (escaped.front() == L'#') {
        size_t count = 0;
        while (count < escaped.size() && count < 6 && escaped[count] == L'#') {
            ++count;
        }
        if (count < escaped.size() && escaped[count] == L' ') {
            escaped.insert(escaped.begin(), L'\\');
        }
    } else if (escaped.starts_with(L"~~~") || escaped.front() == L'>') {
        escaped.insert(escaped.begin(), L'\\');
    } else {
        size_t digits = 0;
        while (digits < escaped.size() && escaped[digits] >= L'0' &&
               escaped[digits] <= L'9') {
            ++digits;
        }
        if (digits != 0 && digits + 1 < escaped.size() &&
            escaped[digits] == L'.' && escaped[digits + 1] == L' ') {
            escaped.insert(escaped.begin() + digits, L'\\');
        }
    }
    return escaped;
}

std::wstring EscapeMarkdownTableCell(const std::wstring& value) {
    const std::wstring markdown = EscapeMarkdownText(value);
    std::wstring escaped;
    escaped.reserve(markdown.size() + markdown.size() / 8);
    for (size_t index = 0; index < markdown.size(); ++index) {
        const wchar_t ch = markdown[index];
        if (ch == L'|') {
            escaped += L"\\|";
        } else if (ch == L'\r' || ch == L'\n') {
            if (ch == L'\r' && index + 1 < markdown.size() &&
                markdown[index + 1] == L'\n') {
                ++index;
            }
            escaped += L"<br>";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

std::wstring EscapeHtmlText(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() + value.size() / 8);
    for (const wchar_t ch : value) {
        switch (ch) {
        case L'&': escaped += L"&amp;"; break;
        case L'<': escaped += L"&lt;"; break;
        case L'>': escaped += L"&gt;"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

bool ParseProjection(
    const nlohmann::json& item,
    StructuredSelectionProjection& projection) {
    projection = StructuredSelectionProjection::Raw;
    if (!item.contains("projection")) return true;
    if (!item["projection"].is_string()) return false;
    const std::string value = item["projection"].get<std::string>();
    if (value == "markdown") {
        projection = StructuredSelectionProjection::Markdown;
    } else if (value == "markdownTableCell") {
        projection = StructuredSelectionProjection::MarkdownTableCell;
    } else if (value == "htmlText") {
        projection = StructuredSelectionProjection::HtmlText;
    } else if (value != "raw") {
        return false;
    }
    return true;
}

} // namespace

std::wstring MakeSelectionRequestToken() {
    GUID id = {};
    if (FAILED(CoCreateGuid(&id))) {
        const uint64_t tick = GetTickCount64();
        wchar_t fallback[33] = {};
        swprintf_s(fallback, L"%016llx%016llx",
            static_cast<unsigned long long>(tick),
            static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(&id) ^ tick));
        return fallback;
    }
    wchar_t token[33] = {};
    swprintf_s(token,
        L"%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
        id.Data1, id.Data2, id.Data3,
        id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3],
        id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
    return token;
}

std::wstring BuildCodeSelectionMarkdown(
    const std::wstring& text, const std::wstring& language) {
    size_t longestRun = 0;
    size_t run = 0;
    for (const wchar_t ch : text) {
        if (ch == L'`') {
            longestRun = (std::max)(longestRun, ++run);
        } else {
            run = 0;
        }
    }
    const std::wstring fence((std::max)(size_t{3}, longestRun + 1), L'`');
    std::wstring safeLanguage;
    for (const wchar_t ch : language) {
        if (safeLanguage.size() >= 32) break;
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'+' ||
            ch == L'-' || ch == L'.') {
            safeLanguage.push_back(ch);
        }
    }
    std::wstring markdown = fence + safeLanguage + L"\n" + text;
    if (markdown.empty() || markdown.back() != L'\n') markdown.push_back(L'\n');
    markdown += fence;
    return markdown;
}

bool ParseCfHtmlSelection(
    const std::string& input,
    const std::wstring& requestToken,
    CfHtmlSelection& selection,
    std::wstring* diagnostic) {
    selection = {};
    const auto fail = [&](const wchar_t* code) {
        if (diagnostic) *diagnostic = code;
        return false;
    };
    if (!IsSafeRequestToken(requestToken)) return fail(L"CFHTML_TOKEN_INVALID");
    if (input.empty() || input.size() > kMaxSelectionHtmlBytes + 1) {
        return fail(input.size() > kMaxSelectionHtmlBytes + 1
            ? L"CFHTML_TOO_LARGE" : L"CFHTML_EMPTY");
    }
    std::string bytes = input;
    while (!bytes.empty() && bytes.back() == '\0') bytes.pop_back();
    if (bytes.empty() || bytes.size() > kMaxSelectionHtmlBytes ||
        bytes.find('\0') != std::string::npos) {
        return fail(bytes.size() > kMaxSelectionHtmlBytes
            ? L"CFHTML_TOO_LARGE" : L"CFHTML_INVALID_BYTES");
    }

    size_t startHtml = 0;
    size_t endHtml = 0;
    size_t startFragment = 0;
    size_t endFragment = 0;
    if (!HeaderOffset(bytes, "StartHTML", startHtml) ||
        !HeaderOffset(bytes, "EndHTML", endHtml) ||
        !HeaderOffset(bytes, "StartFragment", startFragment) ||
        !HeaderOffset(bytes, "EndFragment", endFragment) ||
        startHtml >= endHtml || endHtml > bytes.size() ||
        startFragment < startHtml || endFragment > endHtml ||
        startFragment >= endFragment) {
        return fail(L"CFHTML_OFFSETS_INVALID");
    }

    size_t selectionStart = 0;
    size_t selectionEnd = 0;
    const bool hasSelection = HeaderOffset(bytes, "StartSelection", selectionStart) &&
        HeaderOffset(bytes, "EndSelection", selectionEnd) &&
        selectionStart >= startFragment && selectionEnd <= endFragment &&
        selectionStart < selectionEnd;
    const size_t markedStart = hasSelection ? selectionStart : startFragment;
    const size_t markedEnd = hasSelection ? selectionEnd : endFragment;

    std::string html = bytes.substr(startHtml, endHtml - startHtml);
    std::string tokenAscii;
    tokenAscii.reserve(requestToken.size());
    for (const wchar_t ch : requestToken) {
        tokenAscii.push_back(static_cast<char>(ch));
    }
    const std::string startMarker = "<!--ZENCROP_SELECTION_START_" +
        tokenAscii + "-->";
    const std::string endMarker = "<!--ZENCROP_SELECTION_END_" +
        tokenAscii + "-->";
    html.insert(markedEnd - startHtml, endMarker);
    html.insert(markedStart - startHtml, startMarker);
    if (!Utf8ToWide(html, selection.markedHtml)) {
        return fail(L"CFHTML_UTF8_INVALID");
    }

    std::string_view source;
    if (HeaderValue(bytes, "SourceURL", source) && source.size() <= 4096) {
        Utf8ToWide(source, selection.sourceUrl);
    }
    if (diagnostic) *diagnostic = hasSelection
        ? L"CFHTML_SELECTION_SUCCESS" : L"CFHTML_FRAGMENT_SUCCESS";
    return true;
}

bool ParseStructuredSelectionPlan(
    const std::wstring& planJson,
    const std::wstring& expectedToken,
    uint64_t expectedGeneration,
    SelectionContentKind sourceKind,
    SelectionFidelity fidelity,
    StructuredSelectionPlan& plan,
    std::wstring* diagnostic) {
    plan = {};
    const auto fail = [&](const wchar_t* code) {
        if (diagnostic) *diagnostic = code;
        plan = {};
        return false;
    };
    if (!IsSafeRequestToken(expectedToken) || planJson.empty() ||
        planJson.size() > kMaxStructuredSelectionPlanChars) {
        return fail(L"STRUCTURED_PLAN_ENVELOPE_INVALID");
    }
    std::string utf8;
    if (!WideToUtf8(planJson, utf8)) return fail(L"STRUCTURED_PLAN_UTF8_INVALID");
    try {
        const nlohmann::json root = nlohmann::json::parse(utf8);
        std::string expectedTokenUtf8;
        expectedTokenUtf8.reserve(expectedToken.size());
        for (const wchar_t ch : expectedToken) {
            expectedTokenUtf8.push_back(static_cast<char>(ch));
        }
        if (!root.is_object() || root.value("version", 0) != 1 ||
            root.value("token", std::string()) != expectedTokenUtf8 ||
            root.value("generation", uint64_t{0}) != expectedGeneration ||
            !root.contains("sourceMarkdown") || !root.contains("parts") ||
            !root.contains("leaves") || !root["parts"].is_array() ||
            !root["leaves"].is_array() || root["parts"].size() > 20000 ||
            root["leaves"].size() > 5000) {
            return fail(L"STRUCTURED_PLAN_SCHEMA_INVALID");
        }
        plan.version = 1;
        plan.requestGeneration = expectedGeneration;
        plan.sourceKind = sourceKind;
        plan.fidelity = fidelity;
        plan.requestToken = expectedToken;
        plan.sourceMarkdown = JsonWideString(root["sourceMarkdown"]);
        if (plan.sourceMarkdown.empty() ||
            plan.sourceMarkdown.size() > kMaxSelectionTextUnits) {
            return fail(L"STRUCTURED_PLAN_SOURCE_INVALID");
        }

        std::unordered_set<std::wstring> ids;
        for (const auto& item : root["leaves"]) {
            if (!item.is_object()) return fail(L"STRUCTURED_PLAN_LEAF_INVALID");
            StructuredSelectionLeaf leaf;
            leaf.id = JsonWideString(item.value("id", nlohmann::json()));
            leaf.blockId = JsonWideString(item.value("blockId", nlohmann::json()));
            leaf.text = JsonWideString(item.value("text", nlohmann::json()));
            if (!ParseProjection(item, leaf.projection)) {
                return fail(L"STRUCTURED_PLAN_LEAF_INVALID");
            }
            if (leaf.id.empty() || leaf.id.size() > 64 ||
                leaf.blockId.empty() || leaf.blockId.size() > 64 ||
                leaf.text.empty() || leaf.text.size() > 4000 ||
                !ids.insert(leaf.id).second || !IsValidSelectionUtf16(leaf.text)) {
                return fail(L"STRUCTURED_PLAN_LEAF_INVALID");
            }
            plan.leaves.push_back(std::move(leaf));
        }
        std::unordered_map<std::wstring, size_t> references;
        size_t literalUnits = 0;
        for (const auto& item : root["parts"]) {
            if (!item.is_object()) return fail(L"STRUCTURED_PLAN_PART_INVALID");
            StructuredSelectionPart part;
            if (item.contains("literal")) {
                part.literal = JsonWideString(item["literal"]);
                literalUnits += part.literal.size();
            } else if (item.contains("segmentId")) {
                part.segmentId = JsonWideString(item["segmentId"]);
                if (!ids.contains(part.segmentId)) {
                    return fail(L"STRUCTURED_PLAN_REFERENCE_INVALID");
                }
                ++references[part.segmentId];
            } else {
                return fail(L"STRUCTURED_PLAN_PART_INVALID");
            }
            if (literalUnits > kMaxSelectionTextUnits * 2) {
                return fail(L"STRUCTURED_PLAN_LITERAL_TOO_LARGE");
            }
            plan.parts.push_back(std::move(part));
        }
        for (const auto& leaf : plan.leaves) {
            if (references[leaf.id] != 1) {
                return fail(L"STRUCTURED_PLAN_REFERENCE_COUNT_INVALID");
            }
        }
        std::unordered_map<std::wstring, std::wstring> source;
        for (const auto& leaf : plan.leaves) source.emplace(leaf.id, leaf.text);
        if (ProjectStructuredSelection(plan, source) != plan.sourceMarkdown) {
            return fail(L"STRUCTURED_PLAN_PROJECTION_MISMATCH");
        }
    } catch (const nlohmann::json::exception&) {
        return fail(L"STRUCTURED_PLAN_JSON_INVALID");
    }
    if (diagnostic) *diagnostic = L"STRUCTURED_PLAN_SUCCESS";
    return true;
}

std::wstring ProjectStructuredSelection(
    const StructuredSelectionPlan& plan,
    const std::unordered_map<std::wstring, std::wstring>& translations) {
    std::wstring result;
    result.reserve(plan.sourceMarkdown.size() + plan.sourceMarkdown.size() / 2);
    std::unordered_map<std::wstring, StructuredSelectionProjection> projections;
    projections.reserve(plan.leaves.size());
    for (const auto& leaf : plan.leaves) {
        projections.emplace(leaf.id, leaf.projection);
    }
    for (const auto& part : plan.parts) {
        if (!part.segmentId.empty()) {
            const auto found = translations.find(part.segmentId);
            if (found == translations.end()) return {};
            const auto projection = projections.find(part.segmentId);
            if (projection == projections.end()) return {};
            switch (projection->second) {
            case StructuredSelectionProjection::Markdown:
                result += EscapeMarkdownText(found->second);
                break;
            case StructuredSelectionProjection::MarkdownTableCell:
                result += EscapeMarkdownTableCell(found->second);
                break;
            case StructuredSelectionProjection::HtmlText:
                result += EscapeHtmlText(found->second);
                break;
            default:
                result += found->second;
                break;
            }
        } else {
            result += part.literal;
        }
        if (result.size() > kMaxSelectionTextUnits * 2) return {};
    }
    return result;
}

} // namespace selection
