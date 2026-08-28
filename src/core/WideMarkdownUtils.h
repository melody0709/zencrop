#pragma once
#include "core/WideStringUtils.h"
// Markdown image scan and committed-text projection; base text stays in WideStringUtils.
inline bool WideFindNextMarkdownImage(
    const std::wstring& markdown,
    size_t start,
    size_t& marker, size_t& altClose, size_t maxAltChars = 256)
{
    size_t search = start;
    while (search < markdown.size()) {
        size_t candidate = markdown.find(L"![", search);
        if (candidate == std::wstring::npos) return false;
        size_t close = markdown.find(L"](", candidate + 2);
        if (close != std::wstring::npos && close - (candidate + 2) <= maxAltChars) {
            size_t lineBreak = markdown.find_first_of(L"\r\n", candidate + 2);
            if (lineBreak == std::wstring::npos || lineBreak > close) {
                marker = candidate;
                altClose = close;
                return true;
            }
        }
        search = candidate + 2;
    }
    return false;
}
inline std::wstring WideDeriveCommittedPlainText(const std::wstring& markdown)
{
    std::wstring text = markdown;
    for (;;) {
        size_t start = text.find(L"<!--");
        if (start == std::wstring::npos) break;
        size_t end = text.find(L"-->", start + 4);
        if (end == std::wstring::npos) break;
        text.erase(start, end + 3 - start);
    }
    for (;;) {
        size_t start = text.find(L"<img");
        if (start == std::wstring::npos) break;
        size_t end = text.find(L'>', start + 4);
        if (end == std::wstring::npos) break;
        text.erase(start, end + 1 - start);
    }
    for (;;) {
        size_t start = text.find(L"![");
        if (start == std::wstring::npos) break;
        size_t middle = text.find(L"](", start + 2);
        if (middle == std::wstring::npos) break;
        size_t end = text.find(L')', middle + 2);
        if (end == std::wstring::npos) break;
        text.erase(start, end + 1 - start);
    }
    return WideNormalizeNewlines(text);
}

// Trim trailing CR/LF only (not other whitespace).
inline std::wstring WideTrimTrailingLineBreaks(std::wstring text) {
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    return text;
}

// Markdown format markers stripped by plain-text export.
inline bool WideIsFormatMarker(wchar_t ch) {
    return ch == L'`' || ch == L'*' || ch == L'_' || ch == L'~';
}

// True when HTML tag name is a block-level break tag used by plain-text strip.
inline bool WideIsHtmlBlockTagName(const std::wstring& name) {
    return name == L"br" || name == L"p" || name == L"div" || name == L"tr" ||
        name == L"li" || name == L"h1" || name == L"h2" || name == L"h3" ||
        name == L"h4" || name == L"h5" || name == L"h6";
}

// Append CRLF if out non-empty and does not already end with CRLF.
inline void WideAppendPlainLineBreak(std::wstring& out) {
    if (out.empty()) return;
    if (out.size() >= 2 && out[out.size() - 2] == L'\r' && out[out.size() - 1] == L'\n') return;
    out += L"\r\n";
}

// Strip markdown/HTML to plain text for history export (matches historical StripMarkdownToPlainText).
// Input is normalized to CRLF first (matches NormalizeEditText -> strip pipeline).
inline std::wstring WideStripMarkdownToPlainText(const std::wstring& input) {
    std::wstring text = WideNormalizeNewlines(input);
    std::wstring out;
    out.reserve(text.size());

    bool atLineStart = true;
    for (size_t i = 0; i < text.size();) {
        wchar_t ch = text[i];

        if (ch == L'\r' || ch == L'\n') {
            WideAppendPlainLineBreak(out);
            if (ch == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') i++;
            i++;
            atLineStart = true;
            continue;
        }

        if (atLineStart) {
            size_t marker = i;
            while (marker < text.size() && (text[marker] == L' ' || text[marker] == L'\t')) marker++;
            if (marker < text.size()) {
                if (text[marker] == L'#') {
                    while (marker < text.size() && text[marker] == L'#') marker++;
                    while (marker < text.size() && iswspace(text[marker]) &&
                           text[marker] != L'\r' && text[marker] != L'\n') {
                        marker++;
                    }
                    i = marker;
                    atLineStart = false;
                    continue;
                }
                if (text[marker] == L'>') {
                    marker++;
                    while (marker < text.size() &&
                           (text[marker] == L' ' || text[marker] == L'\t')) {
                        marker++;
                    }
                    i = marker;
                    atLineStart = false;
                    continue;
                }
                if ((text[marker] == L'-' || text[marker] == L'*' || text[marker] == L'+') &&
                    marker + 1 < text.size() && iswspace(text[marker + 1])) {
                    out += L"- ";
                    i = marker + 2;
                    atLineStart = false;
                    continue;
                }
            }
        }

        if (ch == L'<' && i + 1 < text.size()) {
            size_t tagEnd = text.find(L'>', i + 1);
            if (tagEnd != std::wstring::npos) {
                std::wstring tag = WideToLower(
                    text.substr(i + 1, (std::min)(tagEnd - i - 1, static_cast<size_t>(16))));
                size_t nameStart = 0;
                while (nameStart < tag.size() &&
                       (tag[nameStart] == L'/' || iswspace(tag[nameStart]))) {
                    nameStart++;
                }
                std::wstring name;
                while (nameStart < tag.size() && iswalnum(tag[nameStart])) {
                    name += tag[nameStart++];
                }
                if (WideIsHtmlBlockTagName(name)) {
                    WideAppendPlainLineBreak(out);
                    atLineStart = true;
                }
                i = tagEnd + 1;
                continue;
            }
        }

        if (ch == L'!' && i + 1 < text.size() && text[i + 1] == L'[') {
            size_t close = text.find(L']', i + 2);
            if (close != std::wstring::npos && close + 1 < text.size() && text[close + 1] == L'(') {
                size_t end = text.find(L')', close + 2);
                if (end != std::wstring::npos) {
                    std::wstring alt = text.substr(i + 2, close - i - 2);
                    out += alt.empty() ? L"[Image]" : alt;
                    i = end + 1;
                    atLineStart = false;
                    continue;
                }
            }
        }

        if (ch == L'[') {
            size_t close = text.find(L']', i + 1);
            if (close != std::wstring::npos && close + 1 < text.size() && text[close + 1] == L'(') {
                size_t end = text.find(L')', close + 2);
                if (end != std::wstring::npos) {
                    out += text.substr(i + 1, close - i - 1);
                    i = end + 1;
                    atLineStart = false;
                    continue;
                }
            }
        }

        if (WideIsFormatMarker(ch)) {
            i++;
            continue;
        }

        out += ch;
        atLineStart = false;
        i++;
    }

    return WideTrimTrailingLineBreaks(out);
}

// Normalize edit text: convert all newlines to CRLF (historical NormalizeEditText).
// Alias of WideNormalizeNewlines kept for call-site clarity.
inline std::wstring WideNormalizeEditText(const std::wstring& text) {
    return WideNormalizeNewlines(text);
}

// Join normalized edit text + trailing line-break trim (History export pipeline).
inline std::wstring WideNormalizeAndTrimEditText(const std::wstring& text) {
    return WideTrimTrailingLineBreaks(WideNormalizeEditText(text));
}
