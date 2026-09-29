#pragma once

// Text helpers shared by the translation engines and the translation settings
// pages. Kept inside the translation module on purpose: the truncation rule
// below only exists because of how this module converts wide strings, and the
// provider-error parser is engine protocol, not general UI text handling.

#include <cstddef>
#include <string>
#include <string_view>

struct HttpResponse;

namespace translation {

// True when a response declares a JSON body. Accepted, after lowercasing, cutting
// the parameter list at the first `;` and trimming whitespace, all under
// `application/`:
//   - `application/json` (with or without parameters),
//   - `application/*+json` (structured suffix),
//   - `application/json+*` -- Google's community translateHtml answers
//     `application/json+protobuf`.
// Rejected: anything not under `application/`; the bare token as a prefix
// (`application/jsonp` is JSONP, not JSON); and any value that merely *contains*
// the token (`text/plain; note=application/json` was a false positive of the
// machine translation engine's old substring test).
//
// One definition for the whole module (the three engines plus the provider-error
// parser), and the two engines pinned opposite ends of it: the DeepSeek contract
// requires `application/jsonp` to be rejected, while the machine translation
// contract requires `application/json+protobuf` (Google community translateHtml)
// to be accepted -- the engine copy it replaces matched the token as a substring
// anywhere, which satisfied the second but not the first. `TestJsonContentTypeContract`
// pins both ends plus the false positive.
bool IsJsonContentType(std::wstring_view contentType);

// Truncate to at most `maxLength` UTF-16 code units, and never leave a lone high
// surrogate at the end.
//
// Windows wide strings are UTF-16, so cutting at an arbitrary index can split a
// surrogate pair. WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS) then fails,
// and the module's WideToUtf8() helpers return an empty string as a result --
// i.e. the truncated value silently becomes nothing at all (a prompt name that
// can never be saved, an empty log line). Emoji and rare CJK characters are the
// common cases; the same defect class was already fixed once in
// OcrMarkdownPreviewHost's URL encoder.
//
// The tail check runs even when no truncation happens, because callers also hand
// in strings they have already cut (`text.substr(0, cut)`): such a string is
// exactly `maxLength` long, so a size-only guard (`if (size <= maxLength)
// return;`) passed it through unsanitised -- the status preview was silently
// left with a split pair until this was written as one unconditional pass.
inline void TruncateUtf16Safe(std::wstring& text, std::size_t maxLength) {
    if (text.size() > maxLength) text.resize(maxLength);
    if (!text.empty()) {
        const wchar_t last = text.back();
        if (last >= 0xD800 && last <= 0xDBFF) {
            // The pair's low half is gone and cannot be restored, so the stranded
            // high half goes too.
            text.pop_back();
        }
    }
}

// The provider's own error text is what distinguishes "response_format is not
// supported" from "reasoning is mandatory for this endpoint" from "this :free
// slug is gone"; the status code alone cannot, and every one of those cases has
// an actionable answer that the user can only act on if it is visible.
//
// Reads `{"error":{"message":"..."}}` when the body is JSON, collapses
// whitespace (provider messages are prose and sometimes multi-line: OpenRouter
// emits numbered guardrail reasons) and truncates surrogate-safely. Returns an
// empty string when the body carries no usable message.
std::wstring ProviderErrorDetail(const HttpResponse& response);

} // namespace translation
