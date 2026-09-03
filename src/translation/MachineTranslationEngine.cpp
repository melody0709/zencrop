#define WIN32_LEAN_AND_MEAN
#include "MachineTranslationEngine.h"

#include "TranslationProviderCatalog.h"

#include <nlohmann/json.hpp>
#include <objbase.h>

#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace translation {
namespace {

using json = nlohmann::json;

constexpr int kTimeoutMs = 30000;
constexpr int kDeadlineMs = 60000;
constexpr size_t kMaxInputChars = 200000;
constexpr size_t kMaxResponseBytes = 8u * 1024u * 1024u;

// Community Google protocol parameters are implementation constants rather
// than user credentials. Keep the single copy in this private implementation
// unit and never persist or include them in diagnostics/cache fingerprints.
constexpr wchar_t kGoogleCommunityApiKey[] = L"AIzaSyATBXajvzQLTDHEQbcpq0Ihe0vWDHmO520";
constexpr char kGoogleCommunityClient[] = "wt_lib";

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string output(static_cast<size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            output.data(), required, nullptr, nullptr) != required) {
        return {};
    }
    return output;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (required <= 0) return {};
    std::wstring output(static_cast<size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            output.data(), required) != required) {
        return {};
    }
    return output;
}

std::wstring NewRequestId() {
    GUID guid = {};
    if (FAILED(CoCreateGuid(&guid))) return L"translation-request";
    wchar_t text[40] = {};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    return text;
}

void SecureClear(std::wstring& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    value.clear();
}

void SecureClearHeaders(std::vector<std::wstring>& headers) {
    for (auto& header : headers) SecureClear(header);
    headers.clear();
}

TranslationResult Failure(
    ErrorCode code, const std::wstring& message, const std::wstring& requestId) {
    TranslationResult result;
    result.code = code;
    result.error = message;
    result.requestId = requestId;
    return result;
}

ErrorCode TransportError(const std::wstring& message) {
    std::wstring lower = message;
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    if (lower.find(L"cancel") != std::wstring::npos) return ErrorCode::Cancelled;
    if (lower.find(L"timeout") != std::wstring::npos ||
        lower.find(L"deadline") != std::wstring::npos) return ErrorCode::Timeout;
    return ErrorCode::Network;
}

ErrorCode HttpError(int status) {
    if (status == 401 || status == 403) return ErrorCode::Authentication;
    if (status == 429) return ErrorCode::RateLimited;
    if (status == 408 || status == 504) return ErrorCode::Timeout;
    if (status >= 500) return ErrorCode::Server;
    return ErrorCode::InvalidRequest;
}

bool IsJsonContentType(const std::wstring& value) {
    std::wstring lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    return lower.find(L"application/json") != std::wstring::npos ||
        lower.find(L"+json") != std::wstring::npos;
}

std::string UrlEncode(const std::wstring& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    const std::string utf8 = WideToUtf8(value);
    std::string encoded;
    encoded.reserve(utf8.size() * 3);
    for (const unsigned char ch : utf8) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 0x0f]);
        }
    }
    return encoded;
}

std::wstring LanguageFor(
    MachineTranslationProtocol protocol,
    const std::wstring& language,
    bool source) {
    if (language == L"auto") {
        return protocol == MachineTranslationProtocol::GoogleCommunity ||
                protocol == MachineTranslationProtocol::DeepLX
            ? L"auto" : L"";
    }
    switch (protocol) {
    case MachineTranslationProtocol::DeepLJson:
        if (language == L"zh-Hans") return source ? L"ZH" : L"ZH-HANS";
        if (language == L"zh-Hant") return source ? L"ZH" : L"ZH-HANT";
        if (language == L"en") return L"EN";
        if (language == L"ja") return L"JA";
        if (language == L"ko") return L"KO";
        return {};
    case MachineTranslationProtocol::GoogleCloudV2:
    case MachineTranslationProtocol::GoogleCommunity:
        if (language == L"zh-Hans") return L"zh-CN";
        if (language == L"zh-Hant") return L"zh-TW";
        return language;
    case MachineTranslationProtocol::DeepLX: {
        if (language == L"zh-Hans") return L"ZH";
        if (language == L"zh-Hant") return L"ZH-HANT";
        std::wstring upper = language;
        std::transform(upper.begin(), upper.end(), upper.begin(), towupper);
        return upper;
    }
    case MachineTranslationProtocol::MicrosoftCommunity:
    case MachineTranslationProtocol::AzureV3:
    default:
        return language;
    }
}

std::wstring NormalizeDetected(const std::string& language) {
    std::wstring value = Utf8ToWide(language);
    if (value == L"zh" || value == L"zh-CN" || value == L"ZH") value = L"zh-Hans";
    if (value == L"zh-TW") value = L"zh-Hant";
    if (value == L"EN") value = L"en";
    if (value == L"JA") value = L"ja";
    if (value == L"KO") value = L"ko";
    return NormalizeDetectedLanguageCode(value);
}

void AppendCodePoint(std::wstring& output, unsigned long value) {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return;
    if (value <= 0xffff) {
        output.push_back(static_cast<wchar_t>(value));
        return;
    }
    value -= 0x10000;
    output.push_back(static_cast<wchar_t>(0xd800 + (value >> 10)));
    output.push_back(static_cast<wchar_t>(0xdc00 + (value & 0x3ff)));
}

std::wstring DecodeHtmlEntitiesOnce(const std::wstring& input) {
    std::wstring output;
    output.reserve(input.size());
    for (size_t index = 0; index < input.size();) {
        if (input[index] != L'&') {
            output.push_back(input[index++]);
            continue;
        }
        const size_t semicolon = input.find(L';', index + 1);
        if (semicolon == std::wstring::npos || semicolon - index > 12) {
            output.push_back(input[index++]);
            continue;
        }
        const std::wstring entity = input.substr(index + 1, semicolon - index - 1);
        if (entity == L"amp") output.push_back(L'&');
        else if (entity == L"lt") output.push_back(L'<');
        else if (entity == L"gt") output.push_back(L'>');
        else if (entity == L"quot") output.push_back(L'"');
        else if (entity == L"apos" || entity == L"#39") output.push_back(L'\'');
        else if (entity.size() > 1 && entity[0] == L'#') {
            wchar_t* end = nullptr;
            const bool hex = entity.size() > 2 && (entity[1] == L'x' || entity[1] == L'X');
            const wchar_t* start = entity.c_str() + (hex ? 2 : 1);
            const unsigned long value = wcstoul(start, &end, hex ? 16 : 10);
            if (!end || *end != L'\0') {
                output.append(input, index, semicolon - index + 1);
            } else {
                AppendCodePoint(output, value);
            }
        } else {
            output.append(input, index, semicolon - index + 1);
        }
        index = semicolon + 1;
    }
    return output;
}

std::wstring EscapeHtmlText(const std::wstring& input) {
    std::wstring output;
    output.reserve(input.size());
    for (const wchar_t ch : input) {
        switch (ch) {
        case L'&': output += L"&amp;"; break;
        case L'<': output += L"&lt;"; break;
        case L'>': output += L"&gt;"; break;
        case L'"': output += L"&quot;"; break;
        case L'\'': output += L"&#39;"; break;
        default: output.push_back(ch); break;
        }
    }
    return output;
}

void MergeDetected(std::wstring& aggregate, const std::wstring& detected) {
    if (detected == L"und") return;
    if (aggregate == L"und") aggregate = detected;
    else if (aggregate != detected) aggregate = L"mul";
}

json BuildBody(
    MachineTranslationProtocol protocol,
    const TranslationRequest& request) {
    json body;
    const std::wstring source = LanguageFor(protocol, request.sourceLanguage, true);
    const std::wstring target = LanguageFor(protocol, request.targetLanguage, false);
    switch (protocol) {
    case MachineTranslationProtocol::GoogleCloudV2:
        body = {{"q", json::array()}, {"target", WideToUtf8(target)}, {"format", "text"}};
        for (const auto& segment : request.segments) {
            body["q"].push_back(WideToUtf8(segment.text));
        }
        if (!source.empty()) body["source"] = WideToUtf8(source);
        return body;
    case MachineTranslationProtocol::DeepLJson:
        body = {{"text", json::array()}, {"target_lang", WideToUtf8(target)}};
        for (const auto& segment : request.segments) {
            body["text"].push_back(WideToUtf8(segment.text));
        }
        if (!source.empty()) body["source_lang"] = WideToUtf8(source);
        return body;
    case MachineTranslationProtocol::AzureV3:
        body = json::array();
        for (const auto& segment : request.segments) {
            body.push_back({{"Text", WideToUtf8(segment.text)}});
        }
        return body;
    case MachineTranslationProtocol::MicrosoftCommunity:
        body = json::array();
        for (const auto& segment : request.segments) {
            body.push_back(WideToUtf8(EscapeHtmlText(segment.text)));
        }
        return body;
    case MachineTranslationProtocol::GoogleCommunity: {
        json texts = json::array();
        for (const auto& segment : request.segments) {
            texts.push_back(WideToUtf8(EscapeHtmlText(segment.text)));
        }
        return json::array({
            json::array({texts, WideToUtf8(source), WideToUtf8(target)}),
            kGoogleCommunityClient,
        });
    }
    case MachineTranslationProtocol::DeepLX:
        if (request.segments.size() != 1) return json();
        return {
            {"text", WideToUtf8(request.segments.front().text)},
            {"source_lang", source.empty() ? "auto" : WideToUtf8(source)},
            {"target_lang", WideToUtf8(target)},
        };
    default:
        return json();
    }
}

TranslationResult ParseResponse(
    MachineTranslationProtocol protocol,
    const TranslationRequest& request,
    const HttpResponse& response) {
    if (!response.error.empty()) {
        return Failure(TransportError(response.error), response.error, request.requestId);
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return Failure(HttpError(response.statusCode),
            L"Translation provider request failed (" +
                std::to_wstring(response.statusCode) + L").",
            request.requestId);
    }
    if (!IsJsonContentType(response.contentType)) {
        return Failure(ErrorCode::SchemaMismatch,
            L"Translation provider response is not JSON.", request.requestId);
    }
    try {
        const json root = json::parse(response.body);
        const json* translations = nullptr;
        json normalizedTranslations;
        if (protocol == MachineTranslationProtocol::GoogleCloudV2) {
            if (!root.is_object() || !root.contains("data") ||
                !root["data"].is_object() ||
                !root["data"].contains("translations")) {
                return Failure(ErrorCode::SchemaMismatch,
                    L"Google Cloud translation response schema is invalid.", request.requestId);
            }
            translations = &root["data"]["translations"];
        } else if (protocol == MachineTranslationProtocol::DeepLJson) {
            if (!root.is_object() || !root.contains("translations")) {
                return Failure(ErrorCode::SchemaMismatch,
                    L"DeepL translation response schema is invalid.", request.requestId);
            }
            translations = &root["translations"];
        } else if (protocol == MachineTranslationProtocol::AzureV3) {
            translations = &root;
        } else if (protocol == MachineTranslationProtocol::MicrosoftCommunity) {
            translations = &root;
        } else if (protocol == MachineTranslationProtocol::GoogleCommunity) {
            if (!root.is_array() || root.empty() || !root[0].is_array()) {
                return Failure(ErrorCode::SchemaMismatch,
                    L"Google Community translation response schema is invalid.",
                    request.requestId);
            }
            translations = &root[0];
        } else if (protocol == MachineTranslationProtocol::DeepLX) {
            if (!root.is_object() || !root.contains("data") ||
                !root["data"].is_string()) {
                return Failure(ErrorCode::SchemaMismatch,
                    L"DeepLX translation response schema is invalid.", request.requestId);
            }
            normalizedTranslations = json::array({root["data"]});
            translations = &normalizedTranslations;
        }
        if (!translations || !translations->is_array() ||
            translations->size() != request.segments.size()) {
            return Failure(ErrorCode::ContentContract,
                L"Translation result count does not match the request.", request.requestId);
        }
        TranslationResult result;
        result.success = true;
        result.requestId = request.requestId;
        result.detectedSourceLanguage = L"und";
        result.translations.reserve(request.segments.size());
        for (size_t index = 0; index < request.segments.size(); ++index) {
            const auto& item = (*translations)[index];
            std::string text;
            std::string detected;
            if (protocol == MachineTranslationProtocol::GoogleCloudV2) {
                if (!item.is_object() || !item.contains("translatedText") ||
                    !item["translatedText"].is_string()) {
                    return Failure(ErrorCode::SchemaMismatch,
                        L"Google Cloud translation item is invalid.", request.requestId);
                }
                text = item["translatedText"].get<std::string>();
                detected = item.value("detectedSourceLanguage", std::string{});
            } else if (protocol == MachineTranslationProtocol::DeepLJson) {
                if (!item.is_object() || !item.contains("text") || !item["text"].is_string()) {
                    return Failure(ErrorCode::SchemaMismatch,
                        L"DeepL translation item is invalid.", request.requestId);
                }
                text = item["text"].get<std::string>();
                detected = item.value("detected_source_language", std::string{});
            } else if (protocol == MachineTranslationProtocol::AzureV3 ||
                       protocol == MachineTranslationProtocol::MicrosoftCommunity) {
                if (!item.is_object() || !item.contains("translations") ||
                    !item["translations"].is_array() || item["translations"].size() != 1 ||
                    !item["translations"][0].is_object() ||
                    !item["translations"][0].contains("text") ||
                    !item["translations"][0]["text"].is_string()) {
                    return Failure(ErrorCode::SchemaMismatch,
                        L"Azure translation item is invalid.", request.requestId);
                }
                text = item["translations"][0]["text"].get<std::string>();
                if (item.contains("detectedLanguage") && item["detectedLanguage"].is_object()) {
                    detected = item["detectedLanguage"].value("language", std::string{});
                }
            } else if (protocol == MachineTranslationProtocol::GoogleCommunity ||
                       protocol == MachineTranslationProtocol::DeepLX) {
                if (!item.is_string()) {
                    return Failure(ErrorCode::SchemaMismatch,
                        L"Community translation item is invalid.", request.requestId);
                }
                text = item.get<std::string>();
            }
            std::wstring translated = Utf8ToWide(text);
            if (protocol == MachineTranslationProtocol::GoogleCloudV2 ||
                protocol == MachineTranslationProtocol::GoogleCommunity ||
                protocol == MachineTranslationProtocol::MicrosoftCommunity) {
                translated = DecodeHtmlEntitiesOnce(translated);
            }
            if (translated.empty()) {
                return Failure(ErrorCode::ContentContract,
                    L"Translation provider returned empty text.", request.requestId);
            }
            result.translations.push_back({request.segments[index].id, translated});
            result.inputCharacters += request.segments[index].text.size();
            result.outputCharacters += translated.size();
            MergeDetected(result.detectedSourceLanguage, NormalizeDetected(detected));
        }
        return result;
    } catch (const json::parse_error&) {
        return Failure(ErrorCode::InvalidJson,
            L"Translation provider returned invalid JSON.", request.requestId);
    } catch (const json::exception&) {
        return Failure(ErrorCode::SchemaMismatch,
            L"Translation provider response schema is invalid.", request.requestId);
    }
}

} // namespace

MachineTranslationEngine::MachineTranslationEngine(
    const TranslationSettings& settings,
    std::shared_ptr<IAsyncHttpTransport> transport,
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider)
    : settings_(settings),
      transport_(transport ? std::move(transport) : CreateDefaultAsyncHttpTransport()),
      credentialProvider_(credentialProvider
          ? std::move(credentialProvider)
          : CreateDefaultTranslationCredentialProvider()) {}

std::wstring MachineTranslationEngine::Name() const {
    const auto* profile = FindActiveTranslationProvider(settings_);
    return profile ? profile->displayName : L"Machine translation";
}

std::shared_ptr<AsyncHttpRequest> MachineTranslationEngine::Translate(
    const TranslationRequest& request,
    Callback callback) {
    const auto* profile = FindActiveTranslationProvider(settings_);
    if (!profile) {
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::Configuration, L"Active translation provider is missing.", request.requestId));
        return {};
    }
    std::wstring profileError;
    if (!IsSupportedProviderProfile(*profile, &profileError)) {
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::Configuration, profileError, request.requestId));
        return {};
    }
    const auto capabilities = GetCapabilities(*profile);
    if (capabilities.family != TranslationProviderFamily::DirectMt ||
        capabilities.machineProtocol == MachineTranslationProtocol::None) {
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::Configuration, L"Machine translation protocol is missing.", request.requestId));
        return {};
    }
    TranslationRequest normalized = request;
    normalized.sourceLanguage = NormalizeLanguageCode(normalized.sourceLanguage, true);
    normalized.targetLanguage = NormalizeLanguageCode(normalized.targetLanguage, false);
    if (normalized.requestId.empty()) normalized.requestId = NewRequestId();
    if (!IsSupportedSourceLanguage(normalized.sourceLanguage) ||
        !IsConcreteTargetLanguage(normalized.targetLanguage) ||
        normalized.sourceLanguage == normalized.targetLanguage) {
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::Configuration, L"Source and target languages are invalid or identical.",
            normalized.requestId));
        return {};
    }
    size_t characters = 0;
    std::unordered_set<std::wstring> ids;
    for (const auto& segment : normalized.segments) {
        if (segment.id.empty() || segment.text.empty() || !ids.insert(segment.id).second) {
            InvokeTranslationCallbackSafely(callback, Failure(
                ErrorCode::ContentContract,
                L"Translation segments must have unique ids and non-empty text.",
                normalized.requestId));
            return {};
        }
        characters += segment.text.size();
    }
    if (normalized.segments.empty() || characters > kMaxInputChars) {
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::ContentContract, L"Translation text is empty or too long.",
            normalized.requestId));
        return {};
    }
    if (capabilities.maxSegmentsPerRequest > 0 &&
        normalized.segments.size() > capabilities.maxSegmentsPerRequest) {
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::ContentContract,
            L"This translation provider accepts fewer segments per request.",
            normalized.requestId));
        return {};
    }
    std::wstring endpoint = ResolveProviderEndpoint(*profile, &profileError);
    if (endpoint.empty()) {
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::Configuration, profileError, normalized.requestId));
        return {};
    }
    if (capabilities.machineProtocol == MachineTranslationProtocol::AzureV3) {
        const std::wstring target = LanguageFor(
            capabilities.machineProtocol, normalized.targetLanguage, false);
        endpoint += L"&to=" + Utf8ToWide(UrlEncode(target));
        const std::wstring source = LanguageFor(
            capabilities.machineProtocol, normalized.sourceLanguage, true);
        if (!source.empty()) endpoint += L"&from=" + Utf8ToWide(UrlEncode(source));
    } else if (capabilities.machineProtocol ==
               MachineTranslationProtocol::MicrosoftCommunity) {
        const std::wstring source = LanguageFor(
            capabilities.machineProtocol, normalized.sourceLanguage, true);
        const std::wstring target = LanguageFor(
            capabilities.machineProtocol, normalized.targetLanguage, false);
        endpoint += L"?from=" + Utf8ToWide(UrlEncode(source)) +
            L"&to=" + Utf8ToWide(UrlEncode(target)) +
            L"&isEnterpriseClient=false";
    }
    std::wstring key;
    std::wstring credentialError;
    if (TranslationAuthUsesCredential(profile->authMode) &&
        (!credentialProvider_ || !credentialProvider_->ReadCredential(
            profile->credentialRef, key, credentialError))) {
        SecureClear(key);
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::Configuration, credentialError, normalized.requestId));
        return {};
    }
    std::vector<std::wstring> headers = {
        capabilities.machineProtocol == MachineTranslationProtocol::GoogleCommunity
            ? L"Content-Type: application/json+protobuf"
            : L"Content-Type: application/json",
        L"Accept: application/json"};
    switch (capabilities.machineProtocol) {
    case MachineTranslationProtocol::GoogleCloudV2:
        headers.insert(headers.begin(), L"X-Goog-Api-Key: " + key);
        break;
    case MachineTranslationProtocol::DeepLJson:
        headers.insert(headers.begin(), L"Authorization: DeepL-Auth-Key " + key);
        break;
    case MachineTranslationProtocol::AzureV3:
        headers.insert(headers.begin(), L"Ocp-Apim-Subscription-Key: " + key);
        if (!profile->region.empty()) {
            headers.insert(headers.begin() + 1,
                L"Ocp-Apim-Subscription-Region: " + profile->region);
        }
        break;
    case MachineTranslationProtocol::GoogleCommunity:
        headers.insert(headers.begin(),
            std::wstring(L"X-Goog-API-Key: ") + kGoogleCommunityApiKey);
        break;
    case MachineTranslationProtocol::DeepLX:
        if (profile->authMode == TranslationAuthMode::BearerApiKey) {
            headers.insert(headers.begin(), L"Authorization: Bearer " + key);
        }
        break;
    default:
        break;
    }
    const json requestBody = BuildBody(capabilities.machineProtocol, normalized);
    if (requestBody.is_null()) {
        SecureClear(key);
        SecureClearHeaders(headers);
        InvokeTranslationCallbackSafely(callback, Failure(
            ErrorCode::Configuration, L"Machine translation protocol is unsupported.",
            normalized.requestId));
        return {};
    }
    std::string body = requestBody.dump();
    HttpRequestOptions options;
    options.timeoutMs = kTimeoutMs;
    options.deadlineMs = kDeadlineMs;
    options.maxResponseBytes = kMaxResponseBytes;
    options.allowRedirects = false;
    const auto protocol = capabilities.machineProtocol;
    auto operation = transport_->StartPost(
        endpoint, body, headers, options,
        [normalized, protocol, callback = std::move(callback)](HttpResponse response) mutable {
            InvokeTranslationCallbackSafely(
                callback, ParseResponse(protocol, normalized, response));
        });
    SecureClear(key);
    if (!body.empty()) SecureZeroMemory(body.data(), body.size());
    body.clear();
    SecureClearHeaders(headers);
    return operation;
}

std::shared_ptr<AsyncHttpRequest> MachineTranslationEngine::TestConnection(
    Callback callback) {
    TranslationRequest request;
    request.sourceLanguage = L"en";
    request.targetLanguage = L"zh-Hans";
    request.segments.push_back({L"test", L"Hello world."});
    return Translate(request, std::move(callback));
}

} // namespace translation
