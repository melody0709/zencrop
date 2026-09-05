#include "ClipboardStructuredContentReader.h"

#include "SelectionStructuredContent.h"
#include "core/JsonUtils.h"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <string_view>

namespace selection {
namespace {

bool ReadClipboardRawBytesOpen(
    UINT format, size_t maximumBytes, std::string& bytes) {
    bytes.clear();
    if (!format) return false;
    HANDLE handle = GetClipboardData(format);
    if (!handle) return false;
    const SIZE_T size = GlobalSize(handle);
    if (size == 0 || size > maximumBytes) return false;
    const char* data = static_cast<const char*>(GlobalLock(handle));
    if (!data) return false;
    bytes.assign(data, data + size);
    GlobalUnlock(handle);
    return true;
}

bool ReadClipboardBytesOpen(
    UINT format, size_t maximumBytes, std::string& bytes) {
    if (!ReadClipboardRawBytesOpen(format, maximumBytes, bytes)) return false;
    while (!bytes.empty() && bytes.back() == '\0') bytes.pop_back();
    return !bytes.empty() && bytes.find('\0') == std::string::npos;
}

bool DecodeClipboardUtf8(const std::string& bytes, std::wstring& text) {
    text.clear();
    if (bytes.empty() ||
        bytes.size() > kMaxSelectionTextUnits * 4 + 4 ||
        bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    std::string_view source(bytes);
    if (source.size() >= 3 &&
        static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF) {
        source.remove_prefix(3);
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
        static_cast<int>(source.size()), nullptr, 0);
    if (required <= 0 ||
        static_cast<size_t>(required) > kMaxSelectionTextUnits) {
        return false;
    }
    text.resize(static_cast<size_t>(required));
    return MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
        static_cast<int>(source.size()), text.data(), required) == required &&
        IsValidSelectionUtf16(text) && HasNonWhitespace(text);
}

bool DecodeClipboardMetadata(
    const std::string& bytes, std::wstring& text) {
    text.clear();
    const bool utf16Le = bytes.size() >= 2 &&
        ((static_cast<unsigned char>(bytes[0]) == 0xFF &&
          static_cast<unsigned char>(bytes[1]) == 0xFE) ||
         (bytes[0] == '{' && bytes[1] == '\0'));
    if (utf16Le) {
        if (bytes.size() % 2 != 0) return false;
        size_t offset = static_cast<unsigned char>(bytes[0]) == 0xFF ? 2 : 0;
        bool terminated = false;
        for (; offset + 1 < bytes.size(); offset += 2) {
            const wchar_t ch = static_cast<wchar_t>(
                static_cast<unsigned char>(bytes[offset]) |
                (static_cast<unsigned int>(
                    static_cast<unsigned char>(bytes[offset + 1])) << 8));
            if (ch == L'\0') {
                terminated = true;
                break;
            }
            text.push_back(ch);
        }
        if (terminated && std::any_of(
                bytes.begin() + offset, bytes.end(),
                [](char ch) { return ch != '\0'; })) {
            return false;
        }
        return !text.empty() && text.size() <= 64 * 1024 &&
            IsValidSelectionUtf16(text) && HasNonWhitespace(text);
    }

    const size_t terminator = bytes.find('\0');
    const std::string utf8 = terminator == std::string::npos
        ? bytes : bytes.substr(0, terminator);
    if (terminator != std::string::npos &&
        std::any_of(bytes.begin() + terminator, bytes.end(),
            [](char ch) { return ch != '\0'; })) {
        return false;
    }
    return DecodeClipboardUtf8(utf8, text);
}

} // namespace

bool ApplyVsCodeClipboardMetadata(
    const std::string& metadataBytes,
    SelectionContent& content) {
    std::wstring json;
    if (!HasNonWhitespace(content.plainText) ||
        !DecodeClipboardMetadata(metadataBytes, json)) {
        return false;
    }
    const std::wstring language =
        UnescapeJsonString(ExtractJsonField(json, L"mode"));
    if (language.empty()) return false;
    content.codeLanguage = language;
    if (_wcsicmp(language.c_str(), L"markdown") == 0) {
        content.markdown = content.plainText;
        content.kind = SelectionContentKind::Markdown;
    } else {
        content.markdown = BuildCodeSelectionMarkdown(
            content.plainText, content.codeLanguage);
        content.kind = SelectionContentKind::Code;
    }
    content.fidelity = SelectionFidelity::Exact;
    return true;
}

bool ApplyPreformattedSourceClipboardHtml(SelectionContent& content) {
    if (content.kind != SelectionContentKind::Html ||
        !HasNonWhitespace(content.plainText) ||
        content.html.find(L"white-space: pre;") == std::wstring::npos ||
        content.html.find(L"<div><span style=") == std::wstring::npos) {
        return false;
    }
    content.html.clear();
    content.markdown = content.plainText;
    content.codeLanguage = L"markdown";
    content.kind = SelectionContentKind::Markdown;
    content.fidelity = SelectionFidelity::Exact;
    return true;
}

bool TryReadStructuredClipboardContentOpen(
    const std::wstring& requestToken,
    SelectionContent& content) {
    content.requestToken = requestToken;
    const UINT markdownFormat = RegisterClipboardFormatW(L"text/markdown");
    const UINT alternateMarkdownFormat = RegisterClipboardFormatW(L"Markdown");
    const UINT vsCodeFormat = RegisterClipboardFormatW(L"vscode-editor-data");
    const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");

    std::string bytes;
    std::wstring markdown;
    if ((ReadClipboardBytesOpen(markdownFormat,
             kMaxSelectionTextUnits * 4 + 4, bytes) ||
         ReadClipboardBytesOpen(alternateMarkdownFormat,
             kMaxSelectionTextUnits * 4 + 4, bytes)) &&
        DecodeClipboardUtf8(bytes, markdown)) {
        content.markdown = std::move(markdown);
        content.kind = SelectionContentKind::Markdown;
        content.fidelity = SelectionFidelity::Exact;
        return true;
    }

    if (ReadClipboardRawBytesOpen(vsCodeFormat, 64 * 1024, bytes) &&
        ApplyVsCodeClipboardMetadata(bytes, content)) {
        return true;
    }

    if (ReadClipboardBytesOpen(
            htmlFormat, kMaxSelectionHtmlBytes + 1, bytes)) {
        CfHtmlSelection parsed;
        if (ParseCfHtmlSelection(bytes, requestToken, parsed)) {
            content.html = std::move(parsed.markedHtml);
            content.sourceUrl = std::move(parsed.sourceUrl);
            content.kind = SelectionContentKind::Html;
            content.fidelity = SelectionFidelity::Semantic;
            ApplyPreformattedSourceClipboardHtml(content);
            return true;
        }
    }
    return false;
}

} // namespace selection
