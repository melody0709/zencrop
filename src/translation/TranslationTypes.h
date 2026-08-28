#pragma once

#include <string>
#include <vector>

namespace translation {

enum class ErrorCode {
    None,
    Configuration,
    Authentication,
    Balance,
    InvalidRequest,
    RateLimited,
    Server,
    Network,
    Cancelled,
    Timeout,
    InvalidJson,
    SchemaMismatch,
    EmptyContent,
    ContentContract,
    OutputTruncated,
    IncompleteCompletion,
};

struct TranslationSegment {
    std::wstring id;
    std::wstring text;
};

struct TranslationRequest {
    std::wstring requestId;
    std::wstring sourceLanguage = L"auto";
    // `auto` is the user-facing smart Chinese/English mode. It is resolved
    // to a concrete backend target after OCR and before a request is sent.
    std::wstring targetLanguage = L"auto";
    bool preserveParagraphs = true;
    std::vector<TranslationSegment> segments;
};

struct TranslationResult {
    bool success = false;
    ErrorCode code = ErrorCode::None;
    std::wstring error;
    std::wstring requestId;
    std::wstring detectedSourceLanguage = L"und";
    std::vector<TranslationSegment> translations;
    std::wstring model;
    size_t inputCharacters = 0;
    size_t outputCharacters = 0;
};

inline const wchar_t* ErrorCodeName(ErrorCode code) {
    switch (code) {
    case ErrorCode::Configuration: return L"configuration";
    case ErrorCode::Authentication: return L"authentication";
    case ErrorCode::Balance: return L"balance";
    case ErrorCode::InvalidRequest: return L"invalid_request";
    case ErrorCode::RateLimited: return L"rate_limited";
    case ErrorCode::Server: return L"server";
    case ErrorCode::Network: return L"network";
    case ErrorCode::Cancelled: return L"cancelled";
    case ErrorCode::Timeout: return L"timeout";
    case ErrorCode::InvalidJson: return L"invalid_json";
    case ErrorCode::SchemaMismatch: return L"schema_mismatch";
    case ErrorCode::EmptyContent: return L"deepseek_empty_content";
    case ErrorCode::ContentContract: return L"content_contract";
    case ErrorCode::OutputTruncated: return L"output_truncated";
    case ErrorCode::IncompleteCompletion: return L"incomplete_completion";
    default: return L"none";
    }
}

inline bool IsSupportedSourceLanguage(const std::wstring& code) {
    return code == L"auto" || code == L"zh-Hans" || code == L"en" ||
        code == L"zh-Hant" || code == L"ja" || code == L"ko";
}

inline bool IsSupportedTargetLanguage(const std::wstring& code) {
    // Target `auto` is a valid persisted/UI choice. Translation engines only
    // receive a concrete target; see IsConcreteTargetLanguage below.
    return IsSupportedSourceLanguage(code);
}

inline bool IsConcreteTargetLanguage(const std::wstring& code) {
    return code != L"auto" && IsSupportedTargetLanguage(code);
}

inline std::wstring NormalizeLanguageCode(const std::wstring& code, bool /*source*/) {
    if (code == L"zh-Hans-CN") return L"zh-Hans";
    if (code == L"zh-Hant-CN") return L"zh-Hant";
    if (IsSupportedSourceLanguage(code)) {
        return code;
    }
    return L"auto";
}

// Resolve the smart target mode deterministically before the text reaches the
// backend. Chinese (simplified or traditional) goes to English; English goes
// to simplified Chinese. Japanese/Korean and ambiguous text fall back to
// simplified Chinese, which keeps non-Chinese OCR readable for the default
// Chinese-oriented workflow.
inline std::wstring ResolveTargetLanguageForText(
    const std::wstring& selectedTargetLanguage,
    const std::wstring& sourceLanguage,
    const std::wstring& text) {
    const std::wstring target = NormalizeLanguageCode(selectedTargetLanguage, false);
    if (target != L"auto") return target;

    const std::wstring source = NormalizeLanguageCode(sourceLanguage, true);
    if (source == L"zh-Hans" || source == L"zh-Hant") return L"en";
    if (source == L"en" || source == L"ja" || source == L"ko") return L"zh-Hans";

    size_t hanCharacters = 0;
    size_t latinWordRuns = 0;
    bool inLatinWord = false;
    bool hasJapaneseKana = false;
    bool hasKoreanHangul = false;
    for (const wchar_t character : text) {
        const unsigned int code = static_cast<unsigned int>(character);
        if ((code >= 0x3040 && code <= 0x30ff) ||
            (code >= 0x31f0 && code <= 0x31ff)) {
            hasJapaneseKana = true;
        } else if (code >= 0xac00 && code <= 0xd7af) {
            hasKoreanHangul = true;
        } else if ((code >= 0x3400 && code <= 0x4dbf) ||
                   (code >= 0x4e00 && code <= 0x9fff) ||
                   (code >= 0xf900 && code <= 0xfaff)) {
            ++hanCharacters;
            inLatinWord = false;
        } else if ((code >= L'a' && code <= L'z') ||
                   (code >= L'A' && code <= L'Z')) {
            if (!inLatinWord) ++latinWordRuns;
            inLatinWord = true;
        } else {
            inLatinWord = false;
        }
    }
    if (hasJapaneseKana || hasKoreanHangul) return L"zh-Hans";
    // Count ASCII runs rather than individual letters. Product names,
    // URLs, model identifiers, and code tokens embedded in Chinese OCR
    // should not outweigh the surrounding Chinese prose.
    return hanCharacters > 0 && hanCharacters >= latinWordRuns * 2
        ? L"en" : L"zh-Hans";
}

// Detection output is not a user preference: unknown/ambiguous values such
// as `und` and `mul` must remain visible to callers instead of being silently
// converted to the request sentinel `auto`.
inline std::wstring NormalizeDetectedLanguageCode(const std::wstring& code) {
    if (code == L"zh-Hans-CN" || code == L"zh-CN") return L"zh-Hans";
    if (code == L"zh-Hant-CN" || code == L"zh-TW" || code == L"zh-HK") return L"zh-Hant";
    if (code == L"zh-Hans" || code == L"zh-Hant" || code == L"en" ||
        code == L"ja" || code == L"ko" || code == L"und" || code == L"mul") {
        return code;
    }
    return L"und";
}

} // namespace translation
