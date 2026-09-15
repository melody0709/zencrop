#include "TranslationUntranslatable.h"

#include <algorithm>
#include <cwctype>

namespace translation {
namespace {

// Longer "single tokens" are almost always prose that happens to lack a space.
constexpr size_t kMaxUntranslatableChars = 512;
constexpr size_t kMaxIdentifierLetters = 48;
constexpr size_t kMaxFileExtensionChars = 12;

bool IsAsciiLetter(wchar_t value) {
    return (value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z');
}

bool IsAsciiDigit(wchar_t value) {
    return value >= L'0' && value <= L'9';
}

bool IsHexDigit(wchar_t value) {
    return IsAsciiDigit(value) || (value >= L'a' && value <= L'f') ||
        (value >= L'A' && value <= L'F');
}

bool IsCjkText(wchar_t value) {
    const unsigned int code = static_cast<unsigned int>(value);
    return (code >= 0x1100 && code <= 0x11FF) || // Hangul Jamo
        (code >= 0x2E80 && code <= 0x2FFF) ||    // CJK radicals / Kangxi
        (code >= 0x3000 && code <= 0x303F) ||    // CJK symbols and punctuation
        (code >= 0x3040 && code <= 0x30FF) ||    // Hiragana / Katakana
        (code >= 0x3130 && code <= 0x318F) ||    // Hangul compatibility Jamo
        (code >= 0x3400 && code <= 0x4DBF) ||    // CJK extension A
        (code >= 0x4E00 && code <= 0x9FFF) ||    // CJK unified ideographs
        (code >= 0xA960 && code <= 0xA97F) ||    // Hangul Jamo extension A
        (code >= 0xAC00 && code <= 0xD7AF) ||    // Hangul syllables
        (code >= 0xF900 && code <= 0xFAFF) ||    // CJK compatibility ideographs
        (code >= 0xFF00 && code <= 0xFFEF);      // Halfwidth / fullwidth forms
}

bool IsWhitespace(wchar_t value) {
    if (value == L' ' || value == L'\t' || value == L'\r' || value == L'\n') {
        return true;
    }
    return std::iswspace(static_cast<wint_t>(value)) != 0;
}

bool HasWhitespace(const std::wstring& text) {
    return std::any_of(text.begin(), text.end(),
        [](wchar_t value) { return IsWhitespace(value); });
}

// Removes one layer of a matching quote/bracket pair and trailing sentence
// punctuation in place, so a URL quoted as "(https://a.b)" or written as
// "https://a.b." is still recognized. It also drops an unbalanced trailing
// closer ("...file.cpp]" from a wrapped line) and an unbalanced leading opener.
// Never turns a non-empty string into an empty one.
void StripWrappersAndTrailingPunctuation(std::wstring& text) {
    for (;;) {
        if (text.size() < 2) return;
        const wchar_t first = text.front();
        const wchar_t last = text.back();
        const bool paired = (first == L'"' && last == L'"') ||
            (first == L'\'' && last == L'\'') ||
            (first == L'`' && last == L'`') ||
            (first == L'(' && last == L')') ||
            (first == L'[' && last == L']') ||
            (first == L'{' && last == L'}') ||
            (first == L'<' && last == L'>') ||
            (first == 0x201C && last == 0x201D) ||
            (first == 0x2018 && last == 0x2019);
        if (paired) {
            text = text.substr(1, text.size() - 2);
            continue;
        }
        if (last == L'.' || last == L',' || last == L';' || last == L':' ||
            last == 0x3002 || last == 0xFF0C || last == 0xFF1B) {
            text.pop_back();
            continue;
        }
        // A wrapped or truncated line can end with a closer that has no opener.
        const auto unmatched = [&text](wchar_t closer, wchar_t opener) {
            return static_cast<size_t>(std::count(text.begin(), text.end(), closer)) >
                static_cast<size_t>(std::count(text.begin(), text.end(), opener));
        };
        if ((last == L']' && unmatched(L']', L'[')) ||
            (last == L')' && unmatched(L')', L'(')) ||
            (last == L'}' && unmatched(L'}', L'{'))) {
            text.pop_back();
            continue;
        }
        if ((first == L'[' && unmatched(L']', L'[')) ||
            (first == L'(' && unmatched(L')', L'(')) ||
            (first == L'{' && unmatched(L'}', L'{'))) {
            text.erase(0, 1);
            continue;
        }
        return;
    }
}

// Decorative lines ("---------- divider ----------", "* * *") carry no prose:
// at most a third of their non-space characters are letters. This has to be
// evaluated before the whitespace gate, because such lines do contain spaces.
bool IsDecorativeSymbolLine(const std::wstring& text) {
    size_t nonSpace = 0;
    size_t letters = 0;
    for (const wchar_t value : text) {
        if (IsWhitespace(value)) continue;
        ++nonSpace;
        if (IsAsciiLetter(value)) ++letters;
    }
    if (nonSpace < 2) return false;
    return letters * 3 <= nonSpace;
}

bool IsUrlLike(const std::wstring& text) {
    const size_t schemeEnd = text.find(L"://");
    if (schemeEnd == std::wstring::npos || schemeEnd == 0 ||
        schemeEnd + 3 >= text.size()) {
        return false;
    }
    if (!IsAsciiLetter(text[0])) return false;
    for (size_t index = 1; index < schemeEnd; ++index) {
        const wchar_t value = text[index];
        if (!IsAsciiLetter(value) && !IsAsciiDigit(value) && value != L'+' &&
            value != L'.' && value != L'-') {
            return false;
        }
    }
    return true;
}

bool HasInvalidPathCharacter(const std::wstring& text) {
    return text.find_first_of(L"?*\"<>|") != std::wstring::npos;
}

bool IsWindowsDrivePath(const std::wstring& text) {
    if (text.size() < 4) return false;
    if (!IsAsciiLetter(text[0]) || text[1] != L':' ||
        (text[2] != L'\\' && text[2] != L'/')) {
        return false;
    }
    return !HasInvalidPathCharacter(text);
}

bool IsUncPath(const std::wstring& text) {
    if (text.size() < 6 || text.compare(0, 2, L"\\\\") != 0) return false;
    const size_t shareEnd = text.find(L'\\', 2);
    if (shareEnd == std::wstring::npos || shareEnd == 2) return false;
    return !HasInvalidPathCharacter(text);
}

bool IsPosixPath(const std::wstring& text) {
    if (text.size() < 4 || text[0] != L'/') return false;
    const size_t separators = static_cast<size_t>(
        std::count(text.begin(), text.end(), L'/'));
    if (separators < 2) return false;
    return text.find_first_not_of(L'/') != std::wstring::npos;
}

// Relative paths are only recognized when they end in a filename-like
// extension, which keeps "and/or" and dates such as "2024/01/02" out.
bool IsRelativePath(const std::wstring& text) {
    const size_t separators = static_cast<size_t>(
        std::count_if(text.begin(), text.end(),
            [](wchar_t value) { return value == L'\\' || value == L'/'; }));
    if (separators < 2) return false;
    const std::wstring tail = text.substr(text.find_last_of(L"\\/") + 1);
    const size_t dot = tail.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0) return false;
    const size_t extensionLength = tail.size() - dot - 1;
    if (extensionLength == 0 || extensionLength > kMaxFileExtensionChars) {
        return false;
    }
    return std::all_of(tail.begin() + static_cast<ptrdiff_t>(dot) + 1, tail.end(),
        [](wchar_t value) { return IsAsciiLetter(value) || IsAsciiDigit(value); });
}

bool IsBareHost(const std::wstring& text) {
    const size_t lastDot = text.rfind(L'.');
    if (lastDot == std::wstring::npos || lastDot == 0 || lastDot + 1 >= text.size()) {
        return false;
    }
    const std::wstring tld = text.substr(lastDot + 1);
    if (tld.size() < 2 || tld.size() > 24) return false;
    if (!std::all_of(tld.begin(), tld.end(),
            [](wchar_t value) { return IsAsciiLetter(value); })) {
        return false;
    }
    if (text.find(L"..") != std::wstring::npos) return false;
    return std::all_of(text.begin(), text.end(), [](wchar_t value) {
        return IsAsciiLetter(value) || IsAsciiDigit(value) || value == L'.' ||
            value == L'-';
    });
}

bool IsBareHostOrEmail(const std::wstring& text) {
    const size_t at = text.find(L'@');
    if (at == std::wstring::npos) return IsBareHost(text);
    if (at == 0 || at + 1 >= text.size()) return false;
    if (!IsBareHost(text.substr(at + 1))) return false;
    return std::all_of(text.begin(), text.begin() + static_cast<ptrdiff_t>(at),
        [](wchar_t value) {
            return IsAsciiLetter(value) || IsAsciiDigit(value) || value == L'.' ||
                value == L'_' || value == L'%' || value == L'+' || value == L'-';
        });
}

bool IsHashOrUuid(const std::wstring& text) {
    // Canonical UUID: 8-4-4-4-12 hex groups.
    if (text.size() == 36 && text[8] == L'-' && text[13] == L'-' &&
        text[18] == L'-' && text[23] == L'-') {
        const bool hex = std::all_of(text.begin(), text.end(), [](wchar_t value) {
            return IsHexDigit(value) || value == L'-';
        });
        if (hex) return true;
    }
    if (text.size() < 8 || text.size() > 64) return false;
    if (!std::all_of(text.begin(), text.end(),
            [](wchar_t value) { return IsHexDigit(value); })) {
        return false;
    }
    // Digits or length keep hex-looking words ("defaced", "deadbeef") out.
    const bool hasDigit = std::any_of(text.begin(), text.end(),
        [](wchar_t value) { return IsAsciiDigit(value); });
    return hasDigit || text.size() >= 12;
}

bool IsVersionLiteral(const std::wstring& text) {
    size_t start = 0;
    while (start < text.size() && (text[start] == L'=' || text[start] == L'<' ||
        text[start] == L'>' || text[start] == L'~' || text[start] == L'^')) {
        ++start;
    }
    if (start == text.size()) return false;
    if (text[start] == L'v' || text[start] == L'V') ++start;
    if (start == text.size() || !IsAsciiDigit(text[start])) return false;
    size_t digits = 0;
    size_t dots = 0;
    for (size_t index = start; index < text.size(); ++index) {
        const wchar_t value = text[index];
        if (IsAsciiDigit(value)) {
            ++digits;
            continue;
        }
        if (value == L'.') {
            ++dots;
            continue;
        }
        if (IsAsciiLetter(value) || value == L'-' || value == L'+') continue;
        return false;
    }
    return digits >= 1 && dots >= 1;
}

// A bare "." is intentionally not a marker: after wrapper stripping it would
// also match sentence-final words such as "Hello.".
bool IsCodeIdentifierToken(const std::wstring& text) {
    static const wchar_t* const kMarkers[] = {
        L"_", L"::", L"->", L"=>", L"(", L")", L"[", L"]", L"{", L"}",
        L"<", L">", L"#", L"--", L"++", L"+=", L"-=", L"*=", L"/=", L"&&",
        L"||", L"$",
    };
    size_t letters = 0;
    for (const wchar_t value : text) {
        if (IsAsciiLetter(value)) ++letters;
    }
    if (letters == 0 || letters > kMaxIdentifierLetters) return false;
    return std::any_of(std::begin(kMarkers), std::end(kMarkers),
        [&text](const wchar_t* marker) {
            return text.find(marker) != std::wstring::npos;
        });
}

} // namespace

bool IsUntranslatableSegment(const std::wstring& text) {
    if (text.empty() || text.size() > kMaxUntranslatableChars) return false;
    for (const wchar_t value : text) {
        if (value == L'\r' || value == L'\n' || IsCjkText(value)) return false;
    }
    std::wstring core = text;
    StripWrappersAndTrailingPunctuation(core);
    // Two characters are enough for the decorative rule below ("42"); every
    // shape rule needs more structure than that to match.
    if (core.size() < 2 || core.size() > kMaxUntranslatableChars) return false;
    // Checked before the whitespace gate: a decorative line has spaces but no
    // content a model could translate. Measured 2026-09-15: such lines were the
    // shape that produced empty "text" answers most often (3 of 6 live runs for
    // a single "----- divider -----" segment).
    if (IsDecorativeSymbolLine(core)) return true;
    // Every remaining recognized shape is a single token. Whitespace means a
    // phrase or a sentence, so a path that contains spaces deliberately stays
    // translatable.
    if (HasWhitespace(core)) return false;
    return IsUrlLike(core) || IsWindowsDrivePath(core) || IsUncPath(core) ||
        IsPosixPath(core) || IsRelativePath(core) || IsBareHostOrEmail(core) ||
        IsHashOrUuid(core) || IsVersionLiteral(core) ||
        IsCodeIdentifierToken(core);
}

} // namespace translation
