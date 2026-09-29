#include "TranslationTextUtils.h"

#include "core/HttpTransport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>

namespace translation {
namespace {

using json = nlohmann::json;

std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length) != length) {
        return {};
    }
    return result;
}

// Provider messages can be long prose; the result window shows one line and the
// settings status label shows three, so a detail beyond this cannot be read
// anyway. Measured: OpenRouter's guardrail refusal is ~300 characters.
constexpr size_t kMaxProviderDetailChars = 200;

} // namespace

bool IsJsonContentType(std::wstring_view contentType) {
    if (contentType.empty()) return false;
    std::wstring lower(contentType);
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
    // Parameters start at the first ';', and servers pad the value with spaces.
    const size_t semicolon = lower.find(L';');
    if (semicolon != std::wstring::npos) lower.resize(semicolon);
    const size_t first = lower.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return false;
    const size_t last = lower.find_last_not_of(L" \t\r\n");
    lower = lower.substr(first, last - first + 1);
    if (lower == L"application/json") return true;
    if (lower.rfind(L"application/", 0) != 0) return false;
    // Structured suffixes: application/vnd.api+json, application/problem+json.
    if (lower.size() > 5 && lower.compare(lower.size() - 5, 5, L"+json") == 0) {
        return true;
    }
    // `application/json+<something>`: Google's community translateHtml answers
    // `application/json+protobuf`. The `+` is load-bearing -- the machine
    // translation engine's old substring test also accepted `application/jsonp`
    // (which the DeepSeek contract requires to be rejected, and JSONP is not
    // parseable JSON anyway), so this matches the JSON type plus a subtype
    // suffix, not the bare token as a prefix.
    return lower.rfind(L"application/json+", 0) == 0;
}

std::wstring ProviderErrorDetail(const HttpResponse& response) {
    if (response.body.empty() || !IsJsonContentType(response.contentType)) {
        return {};
    }
    try {
        const json outer = json::parse(response.body);
        if (!outer.is_object() || !outer.contains("error") ||
            !outer["error"].is_object()) {
            return {};
        }
        const std::string message = outer["error"].value("message", std::string{});
        if (message.empty()) return {};
        std::wstring collapsed;
        bool pendingSpace = false;
        for (const wchar_t character : Wide(message)) {
            if (character == L' ' || character == L'\t' ||
                character == L'\r' || character == L'\n') {
                pendingSpace = !collapsed.empty();
                continue;
            }
            if (pendingSpace) {
                collapsed.push_back(L' ');
                pendingSpace = false;
            }
            collapsed.push_back(character);
        }
        if (collapsed.size() > kMaxProviderDetailChars) {
            TruncateUtf16Safe(collapsed, kMaxProviderDetailChars);
            collapsed += L"...";
        }
        return collapsed;
    } catch (const json::exception&) {
        return {};
    }
}

} // namespace translation
