#include "OcrMarkdownPreviewProtocol.h"

#include "core/WideStringUtils.h"

#include <algorithm>

namespace OcrMarkdownPreviewProtocol {

constexpr size_t kMaxPreviewBlockContentChars = 256 * 1024;
constexpr size_t kMaxPreviewBlocks = 1000;
constexpr size_t kMaxPreviewBlocksPayloadChars = 2 * 1024 * 1024;

std::wstring EscapeJson(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size() + value.size() / 16);
    const wchar_t* hex = L"0123456789ABCDEF";
    for (wchar_t ch : value) {
        switch (ch) {
        case L'"': result += L"\\\""; break;
        case L'\\': result += L"\\\\"; break;
        case L'\b': result += L"\\b"; break;
        case L'\f': result += L"\\f"; break;
        case L'\n': result += L"\\n"; break;
        case L'\r': result += L"\\r"; break;
        case L'\t': result += L"\\t"; break;
        default:
            if (ch < 0x20) { result += L"\\u00"; result += hex[(ch >> 4) & 0xF]; result += hex[ch & 0xF]; }
            else result += ch;
            break;
        }
    }
    return result;
}

void AppendJsonStringField(std::wstring& json, const wchar_t* name, const std::wstring& value) {
    json += L",\""; json += name; json += L"\":\""; json += EscapeJson(value); json += L"\"";
}

static std::wstring TruncatePreviewBlockContent(const std::wstring& content) {
    return content.size() <= kMaxPreviewBlockContentChars ? content : content.substr(0, kMaxPreviewBlockContentChars);
}

void AppendPreviewBlocksJson(std::wstring& json, const std::vector<OcrMarkdownPreviewHost::PreviewBlock>& blocks) {
    json += L",\"blocks\":[";
    size_t count = (std::min)(blocks.size(), kMaxPreviewBlocks), payloadStart = json.size();
    bool truncated = blocks.size() > count, first = true;
    for (size_t i = 0; i < count; ++i) {
        const auto& block = blocks[i]; std::wstring item = L"{\"id\":\"";
        item += EscapeJson(block.id); item += L"\",\"pageIndex\":"; item += WideFormatIntLabel(block.pageIndex);
        item += L",\"order\":"; item += WideFormatIntLabel(block.order);
        AppendJsonStringField(item, L"label", block.label); AppendJsonStringField(item, L"displayLabel", block.displayLabel);
        AppendJsonStringField(item, L"content", TruncatePreviewBlockContent(block.content));
        AppendJsonStringField(item, L"groupId", block.groupId); AppendJsonStringField(item, L"contentOwnerId", block.contentOwnerId);
        item += L",\"edited\":"; item += WideJsonBoolLiteral(block.edited);
        item += L",\"canRestoreOriginal\":"; item += WideJsonBoolLiteral(block.canRestoreOriginal);
        item += L",\"editable\":"; item += WideJsonBoolLiteral(block.editable); item += L"}";
        if (json.size() - payloadStart + (first ? 0 : 1) + item.size() > kMaxPreviewBlocksPayloadChars) { truncated = true; break; }
        if (!first) json += L","; json += item; first = false;
    }
    json += L"]"; json += L",\"blocksTruncated\":"; json += WideJsonBoolLiteral(truncated);
}

std::wstring BuildRenderMessage(int recordId, const std::wstring& markdown, const std::wstring& sourceMarkdown, const std::wstring& revisionSha256, const std::vector<OcrMarkdownPreviewHost::PreviewBlock>& blocks, const std::wstring& hoveredBlockId, const std::wstring& selectedBlockId, const std::wstring& editingBlockId, const std::wstring& renderToken, bool compactLayout) {
    std::wstring json = L"{\"type\":\"render\",\"recordId\":";
    json += WideFormatIntLabel(recordId);
    AppendJsonStringField(json, L"renderToken", renderToken);
    json += L",\"markdown\":\""; json += EscapeJson(markdown); json += L"\"";
    AppendJsonStringField(json, L"sourceMarkdown", sourceMarkdown);
    AppendJsonStringField(json, L"canonicalSource", L"markdown-body-lf");
    AppendJsonStringField(json, L"offsetUnit", L"utf16-code-unit");
    AppendJsonStringField(json, L"revisionSha256", revisionSha256);
    AppendPreviewBlocksJson(json, blocks);
    AppendJsonStringField(json, L"hoveredBlockId", hoveredBlockId);
    AppendJsonStringField(json, L"selectedBlockId", selectedBlockId);
    AppendJsonStringField(json, L"editingBlockId", editingBlockId);
    json += L",\"theme\":\"dark\",\"compactLayout\":";
    json += WideJsonBoolLiteral(compactLayout);
    json += L",\"options\":{\"enableMermaid\":true,\"enableChartJs\":true}}";
    return json;
}

std::wstring BuildTransientRenderMessage(int recordId, const std::wstring& markdown, const std::wstring& renderToken, bool compactLayout) {
    std::wstring json = L"{\"type\":\"renderTransient\",\"recordId\":";
    json += WideFormatIntLabel(recordId);
    AppendJsonStringField(json, L"renderToken", renderToken);
    AppendJsonStringField(json, L"markdown", markdown);
    json += L",\"compactLayout\":";
    json += WideJsonBoolLiteral(compactLayout);
    json += L",\"options\":{\"enableMermaid\":true,\"enableChartJs\":true}}";
    return json;
}

static std::wstring BuildResult(const wchar_t* type, const std::wstring& id, const std::wstring& token, bool success, const std::wstring& error) {
    std::wstring json = L"{\"type\":\""; json += type; json += L"\"";
    AppendJsonStringField(json, L"id", id); AppendJsonStringField(json, L"renderToken", token);
    json += L",\"success\":"; json += WideJsonBoolLiteral(success); AppendJsonStringField(json, L"errorCode", error); json += L"}";
    return json;
}

std::wstring BuildBlockState(const wchar_t* type, const std::wstring& id, bool ensureVisible) {
    std::wstring json = L"{\"type\":\""; json += type; json += L"\",\"id\":\""; json += EscapeJson(id);
    json += L"\",\"ensureVisible\":"; json += WideJsonBoolLiteral(ensureVisible); json += L"}";
    return json;
}

std::wstring BuildBlockSaveResult(const std::wstring& id, const std::wstring& token, bool success, const std::wstring& error) {
    return BuildResult(L"previewBlockSaveResult", id, token, success, error);
}

std::wstring BuildBlockRestoreResult(const std::wstring& id, const std::wstring& token, bool success, const std::wstring& error) {
    return BuildResult(L"previewBlockRestoreResult", id, token, success, error);
}

std::wstring BuildDocumentSaveResult(const std::wstring& token, bool success, const std::wstring& error) {
    return BuildResult(L"previewDocumentSaveResult", L"", token, success, error);
}

} // namespace OcrMarkdownPreviewProtocol
