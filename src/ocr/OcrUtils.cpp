#include "OcrUtils.h"
#include "JsonUtils.h"
#include "core/WideFormatUtils.h"
#include "core/WideMarkdownUtils.h"
#include "core/WideStringUtils.h"
#include "core/NarrowStringUtils.h"
#include "core/AppDataPaths.h"
#include "AppMessages.h"
#include "MiniHttpServer.h"
#include "ocr/batch/BatchOcrImageLinks.h"
#include "HttpTransport.h"
#include "ocr/document/PaddleCloudDocumentTransport.h"
#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <winhttp.h>
#include <cstring>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")

std::wstring UrlEncode(const std::wstring& s) {
    std::wstring result;
    for (wchar_t c : s) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
            (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' ||
            c == L'.' || c == L'~' || c == L'/') {
            result += c;
        } else {
            char mb[8] = {};
            int len = WideCharToMultiByte(CP_UTF8, 0, &c, 1, mb, 8, nullptr, nullptr);
            for (int i = 0; i < len; i++) {
                // OWN-116: pure percent-hex byte (NarrowStringUtils).
                const std::string hex = NarrowFormatPercentHexByte((unsigned char)mb[i]);
                wchar_t whex[4] = {};
                MultiByteToWideChar(CP_ACP, 0, hex.c_str(), -1, whex, 4);
                result += whex;
            }
        }
    }
    return result;
}

std::wstring GetOcrImageDir() {
    const std::wstring dataPath = ZenCropAppDataFilePath(L"ocr_images");
    if (dataPath.empty()) return L"";
    std::wstring dir = WideEnsureTrailingBackslash(dataPath);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring GetOcrImageDateDir(const SYSTEMTIME& st) {
    // OWN-111: pure date parts format (WideStringUtils).
    std::wstring dateDir = WideFormatDateParts(st.wYear, st.wMonth, st.wDay);

    std::wstring dir = GetOcrImageDir();
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') {
        dir += L"\\";
    }
    dir += dateDir;
    dir += L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring GetOcrImageDateDir() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return GetOcrImageDateDir(st);
}

VlmResponse ParseVlmResponse(const std::string& jsonBody) {
    VlmResponse result;

    int len = MultiByteToWideChar(CP_UTF8, 0, jsonBody.c_str(), (int)jsonBody.length(), nullptr, 0);
    if (len <= 0) {
        result.error = L"Empty or invalid response body";
        return result;
    }
    std::wstring wbody(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, jsonBody.c_str(), (int)jsonBody.length(), &wbody[0], len);

    size_t contentPos = wbody.find(L"\"content\"");
    if (contentPos == std::wstring::npos) {
        result.error = L"No content in response";
        return result;
    }

    size_t colonPos = wbody.find(L':', contentPos);
    if (colonPos == std::wstring::npos) {
        result.error = L"Malformed response";
        return result;
    }

    size_t valStart = colonPos + 1;
    while (valStart < wbody.length() && (wbody[valStart] == L' ' || wbody[valStart] == L'\t' || wbody[valStart] == L'\n' || wbody[valStart] == L'\r'))
        valStart++;

    if (valStart >= wbody.length() || wbody[valStart] != L'\"') {
        result.error = L"Content is not a string";
        return result;
    }

    size_t strEnd = valStart + 1;
    while (strEnd < wbody.length()) {
        if (wbody[strEnd] == L'\\' && strEnd + 1 < wbody.length()) {
            strEnd += 2;
        } else if (wbody[strEnd] == L'\"') {
            break;
        } else {
            strEnd++;
        }
    }

    if (strEnd >= wbody.length()) {
        result.error = L"Unterminated content string";
        return result;
    }

    std::wstring text = wbody.substr(valStart + 1, strEnd - valStart - 1);
    result.content = UnescapeJsonString(text);
    result.finishReason = UnescapeJsonString(ExtractJsonField(wbody, L"finish_reason"));
    auto parseTokenCount = [&wbody](const wchar_t* key) {
        std::wstring raw = TrimString(ExtractJsonField(wbody, key));
        if (raw.empty()) return -1;
        // OWN-78: pure strict int parse (WideStringUtils); reject negative.
        int value = 0;
        if (!WideTryParseJsonIntToken(raw, value) || value < 0) return -1;
        return value;
    };
    result.promptTokens = parseTokenCount(L"prompt_tokens");
    result.completionTokens = parseTokenCount(L"completion_tokens");
    result.totalTokens = parseTokenCount(L"total_tokens");
    if (TrimString(result.content).empty()) {
        result.content.clear();
        result.error = L"Empty content in response";
        return result;
    }
    result.success = true;
    return result;
}

static std::wstring Utf8ToWideString(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), &result[0], len);
    return result;
}

static size_t FindJsonValueStart(const std::wstring& s, const wchar_t* key, size_t searchFrom) {
    size_t keyPos = s.find(key, searchFrom);
    if (keyPos == std::wstring::npos) return std::wstring::npos;

    size_t colonPos = s.find(L':', keyPos + wcslen(key));
    if (colonPos == std::wstring::npos) return std::wstring::npos;

    return SkipJsonWhitespace(s, colonPos + 1);
}

static size_t FindMatchingJsonChar(const std::wstring& s, size_t openPos, wchar_t openChar, wchar_t closeChar) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = openPos; i < s.length(); i++) {
        wchar_t c = s[i];

        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == L'\\') {
                escaped = true;
            } else if (c == L'\"') {
                inString = false;
            }
            continue;
        }

        if (c == L'\"') {
            inString = true;
        } else if (c == openChar) {
            depth++;
        } else if (c == closeChar) {
            depth--;
            if (depth == 0) return i;
        }
    }

    return std::wstring::npos;
}

static bool ReadJsonStringAt(const std::wstring& s, size_t quotePos, std::wstring& value, size_t* endAfter = nullptr) {
    if (quotePos >= s.length() || s[quotePos] != L'\"') return false;

    size_t strEnd = quotePos + 1;
    while (strEnd < s.length()) {
        if (s[strEnd] == L'\\' && strEnd + 1 < s.length()) {
            strEnd += 2;
        } else if (s[strEnd] == L'\"') {
            break;
        } else {
            strEnd++;
        }
    }

    if (strEnd >= s.length()) return false;

    value = UnescapeJsonString(s.substr(quotePos + 1, strEnd - quotePos - 1));
    if (endAfter) *endAfter = strEnd + 1;
    return true;
}

static bool FindJsonStringValue(const std::wstring& s, const wchar_t* key, size_t searchFrom,
    size_t searchLimit, std::wstring& value, size_t* endAfter = nullptr)
{
    size_t keyPos = s.find(key, searchFrom);
    while (keyPos != std::wstring::npos && keyPos < searchLimit) {
        size_t valStart = FindJsonValueStart(s, key, keyPos);
        if (valStart != std::wstring::npos && valStart < searchLimit && s[valStart] == L'\"') {
            return ReadJsonStringAt(s, valStart, value, endAfter);
        }
        keyPos = s.find(key, keyPos + 1);
    }
    return false;
}

static void AppendRecognizedText(std::wstring& result, const std::wstring& text) {
    if (text.empty()) return;
    if (!result.empty()) result += L"\r\n";
    result += text;
}

static void AppendJsonStringArrayValues(const std::wstring& s, size_t arrayStart, size_t arrayEnd,
    std::wstring& result)
{
    size_t pos = arrayStart + 1;
    while (pos < arrayEnd) {
        size_t quotePos = s.find(L'\"', pos);
        if (quotePos == std::wstring::npos || quotePos >= arrayEnd) break;

        std::wstring item;
        size_t endAfter = quotePos + 1;
        if (ReadJsonStringAt(s, quotePos, item, &endAfter)) {
            AppendRecognizedText(result, item);
            pos = endAfter;
        } else {
            pos = quotePos + 1;
        }
    }
}

std::wstring ParsePaddleTextResponse(const std::string& json) {
    std::wstring result;

    std::wstring wjson = Utf8ToWideString(json);
    if (wjson.empty()) return result;

    size_t valStart = FindJsonValueStart(wjson, L"\"rec_texts\"", 0);
    if (valStart != std::wstring::npos && valStart < wjson.length() && wjson[valStart] == L'[') {
        OutputDebugStringA("[OCR] Found rec_texts (standard format)\n");
        size_t arrayEnd = FindMatchingJsonChar(wjson, valStart, L'[', L']');
        if (arrayEnd != std::wstring::npos) {
            AppendJsonStringArrayValues(wjson, valStart, arrayEnd, result);
            if (!result.empty()) return result;
        }
    }

    for (const auto& key : {L"\"text\"", L"\"rec_text\"", L"\"transcription\"", L"\"words\""}) {
        std::wstring text;
        if (FindJsonStringValue(wjson, key, 0, wjson.length(), text)) {
            return text;
        }
    }

    return result;
}

std::wstring ParsePaddleVlResponse(const std::string& json) {
    std::wstring result;

    std::wstring wjson = Utf8ToWideString(json);
    if (wjson.empty()) return result;

    size_t layoutStart = FindJsonValueStart(wjson, L"\"layoutParsingResults\"", 0);
    size_t searchStart = 0;
    size_t searchEnd = wjson.length();

    if (layoutStart != std::wstring::npos && layoutStart < wjson.length()) {
        OutputDebugStringA("[OCR] Found layoutParsingResults (VL format)\n");
        if (wjson[layoutStart] == L'[') {
            size_t layoutEnd = FindMatchingJsonChar(wjson, layoutStart, L'[', L']');
            if (layoutEnd != std::wstring::npos) {
                searchStart = layoutStart;
                searchEnd = layoutEnd;
            }
        }
    }

    size_t mdSearch = searchStart;
    while (mdSearch < searchEnd) {
        size_t mdValue = FindJsonValueStart(wjson, L"\"markdown\"", mdSearch);
        if (mdValue == std::wstring::npos || mdValue >= searchEnd) break;

        if (wjson[mdValue] == L'{') {
            size_t mdEnd = FindMatchingJsonChar(wjson, mdValue, L'{', L'}');
            if (mdEnd == std::wstring::npos || mdEnd > searchEnd) break;

            std::wstring text;
            if (FindJsonStringValue(wjson, L"\"text\"", mdValue, mdEnd, text)) {
                AppendRecognizedText(result, text);
            }
            mdSearch = mdEnd + 1;
            continue;
        }

        if (wjson[mdValue] == L'\"') {
            std::wstring text;
            size_t endAfter = mdValue + 1;
            if (ReadJsonStringAt(wjson, mdValue, text, &endAfter)) {
                AppendRecognizedText(result, text);
                mdSearch = endAfter;
                continue;
            }
        }

        mdSearch = mdValue + 1;
    }

    return result;
}

std::wstring ParsePaddleResponse(const std::string& json) {
    std::wstring text = ParsePaddleVlResponse(json);
    if (!text.empty()) return text;
    return ParsePaddleTextResponse(json);
}

namespace {

bool IsAcceptableProviderImageContentType(std::wstring contentType) {
    // OWN-79: pure lower + trim (WideStringUtils).
    contentType = WideToLower(std::move(contentType));
    size_t semi = contentType.find(L';');
    if (semi != std::wstring::npos) contentType.resize(semi);
    contentType = WideTrim(std::move(contentType));
    if (contentType.empty()) return true;
    if (contentType.rfind(L"image/", 0) == 0) return true;
    if (contentType.rfind(L"application/octet-stream", 0) == 0) return true;
    if (contentType.rfind(L"binary/octet-stream", 0) == 0) return true;
    return false;
}

OcrEmbeddedAssetEncodedFormat DetectProviderImageFormat(
    const std::vector<unsigned char>& bytes)
{
    static const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() >= sizeof(png) &&
        memcmp(bytes.data(), png, sizeof(png)) == 0) {
        return OcrEmbeddedAssetEncodedFormat::Png;
    }
    if (bytes.size() >= 12 && memcmp(bytes.data(), "RIFF", 4) == 0 &&
        memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
        return OcrEmbeddedAssetEncodedFormat::WebP;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        return OcrEmbeddedAssetEncodedFormat::Jpeg;
    }
    return OcrEmbeddedAssetEncodedFormat::Unknown;
}

bool IsTransientProviderImageFailure(int statusCode, const std::wstring& error) {
    if (statusCode == 408 || statusCode == 425 || statusCode == 429 ||
        statusCode == 500 || statusCode == 502 || statusCode == 503 ||
        statusCode == 504) {
        return true;
    }
    if (statusCode != 0) return false;
    return !error.empty();
}

void ReplaceAllInScope(
    std::wstring& text,
    const std::wstring& oldRef,
    const std::wstring& newRef)
{
    if (oldRef.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(oldRef, pos)) != std::wstring::npos) {
        text.replace(pos, oldRef.size(), newRef);
        pos += newRef.size();
    }
}

struct ProviderImageMapEntry {
    std::wstring key;
    std::wstring value;
};

// Parse one "images": { "imgs/...": "url-or-base64", ... } object into entries.
bool ParseProviderImagesObject(
    const std::wstring& wjson,
    size_t objStart,
    size_t objEnd,
    std::vector<ProviderImageMapEntry>& out)
{
    out.clear();
    size_t pos = objStart + 1;
    while (pos < objEnd) {
        size_t keyStart = wjson.find(L"\"imgs", pos);
        if (keyStart == std::wstring::npos || keyStart >= objEnd) break;

        std::wstring imageKey;
        size_t keyEndAfter = keyStart + 1;
        if (!ReadJsonStringAt(wjson, keyStart, imageKey, &keyEndAfter) ||
            keyEndAfter > objEnd + 1) {
            pos = keyStart + 5;
            continue;
        }
        if (imageKey.rfind(L"imgs/", 0) != 0 &&
            imageKey.rfind(L"imgs\\", 0) != 0) {
            pos = keyEndAfter;
            continue;
        }
        for (wchar_t& ch : imageKey) {
            if (ch == L'\\') ch = L'/';
        }

        size_t colonPos = wjson.find(L':', keyEndAfter - 1);
        if (colonPos == std::wstring::npos || colonPos >= objEnd) break;
        size_t valStart = colonPos + 1;
        while (valStart < objEnd &&
               (wjson[valStart] == L' ' || wjson[valStart] == L'\t' ||
                wjson[valStart] == L'\r' || wjson[valStart] == L'\n')) {
            valStart++;
        }
        if (valStart >= objEnd || wjson[valStart] != L'\"') {
            pos = keyEndAfter;
            continue;
        }

        std::wstring imageValue;
        size_t endAfter = valStart + 1;
        if (!ReadJsonStringAt(wjson, valStart, imageValue, &endAfter) ||
            endAfter > objEnd + 1) {
            pos = keyEndAfter;
            continue;
        }
        out.push_back({std::move(imageKey), std::move(imageValue)});
        pos = endAfter;
    }
    return !out.empty();
}

bool DownloadProviderImageHttp(
    const std::wstring& url,
    std::vector<unsigned char>& bytes,
    size_t maxBytes,
    ULONGLONG jobDeadlineTick,
    ProviderImageHttpGetFn httpGetOverride)
{
    bytes.clear();

    // The injected transport replaces I/O only; it must not bypass the same
    // provider URL policy used in production.
    std::wstring urlError;
    if (!IsSafePaddleCloudResourceUrl(url, urlError)) {
        // OWN-117: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatOcrRejectedProviderUrl(urlError.c_str()).c_str());
        return false;
    }

    constexpr int kMaxAttempts = 3;
    constexpr int kPerAttemptTimeoutMs = 8000;
    constexpr ULONGLONG kMaxPerImageDeadlineMs = 12000;
    const ULONGLONG imageStarted = GetTickCount64();

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        const ULONGLONG now = GetTickCount64();
        if (now >= jobDeadlineTick ||
            (now - imageStarted) >= kMaxPerImageDeadlineMs) {
            OutputDebugStringA("[OCR] Provider image download deadline exceeded\n");
            return false;
        }

        std::wstring contentType;
        int statusCode = 0;
        std::wstring error;
        std::vector<unsigned char> body;

        if (httpGetOverride) {
            // Injectible path still shares retry/deadline policy so contracts can
            // exercise 429 -> 200 without hitting the network.
            if (!httpGetOverride(url, body, contentType, statusCode, error)) {
                body.clear();
            }
        } else {
            const ULONGLONG remainingJob = jobDeadlineTick - now;
            const ULONGLONG remainingImage =
                kMaxPerImageDeadlineMs - (now - imageStarted);
            const int remainingMs = static_cast<int>((std::min)(remainingJob, remainingImage));
            HttpRequestOptions options;
            // Keep WinHTTP's timeout positive without extending the absolute
            // per-image/job deadline when less than one second remains.
            options.timeoutMs = (std::max)(
                1,
                (std::min)(kPerAttemptTimeoutMs, remainingMs));
            options.maxResponseBytes = maxBytes;
            options.allowRedirects = false;
            HttpResponse response = HttpGet(url, {}, options);
            statusCode = response.statusCode;
            contentType = response.contentType;
            error = response.error;
            if (response.error.empty() && response.statusCode == 200 &&
                (response.finalUrl.empty() ||
                    IsSamePaddleCloudUrlTarget(url, response.finalUrl)) &&
                !response.body.empty() && response.body.size() <= maxBytes) {
                body.assign(response.body.begin(), response.body.end());
            }
        }

        if (statusCode != 200 || body.empty() || body.size() > maxBytes || !error.empty()) {
            // OWN-117: pure narrow debug (NarrowStringUtils).
            OutputDebugStringA(NarrowFormatOcrProviderDownloadFailed(
                attempt, kMaxAttempts, statusCode, body.size(), error.c_str()).c_str());
            if (attempt < kMaxAttempts &&
                IsTransientProviderImageFailure(statusCode, error) &&
                GetTickCount64() < jobDeadlineTick &&
                (GetTickCount64() - imageStarted) < kMaxPerImageDeadlineMs) {
                if (!httpGetOverride) {
                    Sleep(100u * static_cast<DWORD>(attempt));
                }
                continue;
            }
            return false;
        }
        if (!IsAcceptableProviderImageContentType(contentType)) {
            // OWN-117: pure narrow debug (NarrowStringUtils).
            OutputDebugStringA(
                NarrowFormatOcrProviderContentTypeRejected(contentType.c_str()).c_str());
            return false;
        }
        if (DetectProviderImageFormat(body) == OcrEmbeddedAssetEncodedFormat::Unknown) {
            OutputDebugStringA("[OCR] Provider image body is not PNG/JPEG/WebP\n");
            return false;
        }
        bytes = std::move(body);
        return true;
    }
    return false;
}

// Materialize one markdown fragment against its own images map only.
// Shared relative keys across pages therefore cannot cross-contaminate.
bool MaterializeMarkdownWithImageMap(
    std::wstring& markdown,
    const std::vector<ProviderImageMapEntry>& imageMap,
    int pageOrdinal,
    std::vector<OcrEmbeddedAssetSpec>* embeddedAssets,
    size_t& providerAssetBytes,
    int& attemptedImageCount,
    int& imageCount,
    size_t maxBytesPerAsset,
    size_t maxBytesTotal,
    int maxImagesTotal,
    ULONGLONG jobDeadlineTick,
    ProviderImageHttpGetFn httpGetOverride)
{
    bool rewroteAnyReference = false;
    for (const auto& entry : imageMap) {
        if (markdown.find(entry.key) == std::wstring::npos) continue;
        rewroteAnyReference = true;
        if (attemptedImageCount >= maxImagesTotal) {
            OutputDebugStringA("[OCR] Provider image count budget exceeded\n");
            ReplaceAllInScope(markdown, entry.key, L"");
            continue;
        }
        attemptedImageCount++;
        if (GetTickCount64() >= jobDeadlineTick) {
            OutputDebugStringA("[OCR] Provider image job deadline exceeded\n");
            ReplaceAllInScope(markdown, entry.key, L"");
            continue;
        }

        if (!embeddedAssets || providerAssetBytes >= maxBytesTotal) {
            OutputDebugStringA("[OCR] Provider image byte budget exhausted\n");
            ReplaceAllInScope(markdown, entry.key, L"");
            continue;
        }
        const size_t remainingAssetBytes = maxBytesTotal - providerAssetBytes;
        const size_t maxCandidateBytes =
            (std::min)(maxBytesPerAsset, remainingAssetBytes);

        // OWN-117: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(
            NarrowFormatOcrFoundImageKey(pageOrdinal, entry.key.c_str()).c_str());

        std::vector<unsigned char> bytes;
        if (entry.value.rfind(L"http://", 0) == 0 ||
            entry.value.rfind(L"https://", 0) == 0) {
            // OWN-117: pure narrow debug (NarrowStringUtils).
            OutputDebugStringA(NarrowOcrImageIsUrl());
            if (!DownloadProviderImageHttp(
                    entry.value, bytes, maxCandidateBytes, jobDeadlineTick, httpGetOverride)) {
                ReplaceAllInScope(markdown, entry.key, L"");
                continue;
            }
        } else {
            // OWN-117: pure narrow debug (NarrowStringUtils).
            OutputDebugStringA(NarrowOcrImageIsBase64());
            const size_t maxBase64Chars = ((maxCandidateBytes + 2) / 3) * 4 + 4;
            if (entry.value.empty() || entry.value.size() > maxBase64Chars) {
                ReplaceAllInScope(markdown, entry.key, L"");
                continue;
            }
            int mbLen = WideCharToMultiByte(
                CP_UTF8, 0, entry.value.c_str(), (int)entry.value.length(),
                nullptr, 0, nullptr, nullptr);
            if (mbLen <= 0) {
                ReplaceAllInScope(markdown, entry.key, L"");
                continue;
            }
            std::string mbBase64(mbLen, '\0');
            if (WideCharToMultiByte(
                CP_UTF8, 0, entry.value.c_str(), (int)entry.value.length(),
                mbBase64.data(), mbLen, nullptr, nullptr) != mbLen) {
                ReplaceAllInScope(markdown, entry.key, L"");
                continue;
            }
            bytes = DecodeBase64Image(mbBase64);
        }

        const auto format = DetectProviderImageFormat(bytes);
        if (bytes.empty() || format == OcrEmbeddedAssetEncodedFormat::Unknown ||
            bytes.size() > maxCandidateBytes) {
            ReplaceAllInScope(markdown, entry.key, L"");
            continue;
        }

        OcrEmbeddedAssetSpec asset;
        asset.sourceKind = OcrEmbeddedAssetSourceKind::ProviderEncodedBytes;
        asset.localOrder = static_cast<int>(embeddedAssets->size()) + 1;
        // OWN-126: pure page provider asset id/uri (WideStringUtils).
        asset.id = WideFormatPageProviderAssetId((std::max)(1, pageOrdinal), asset.localOrder);
        asset.semanticClass = L"provider_image";
        asset.placeholderUri = L"zencrop-asset://provider/image_" +
            WideFormatIntLabel(asset.localOrder);
        asset.providerBytes = std::move(bytes);
        asset.providerFormat = format;
        ReplaceAllInScope(markdown, entry.key, asset.placeholderUri);
        providerAssetBytes += asset.providerBytes.size();
        embeddedAssets->push_back(std::move(asset));
        imageCount++;
    }
    return rewroteAnyReference;
}

void AppendMarkdownFragment(std::wstring& dest, const std::wstring& fragment) {
    if (fragment.empty()) return;
    // Match ParsePaddleVlResponse/AppendRecognizedText exactly so image
    // materialization does not otherwise reformat OCR output.
    if (!dest.empty()) dest += L"\r\n";
    dest += fragment;
}

} // namespace

std::wstring ProcessImagesInResponse(
    const std::string& json,
    std::wstring& processedText,
    std::vector<OcrEmbeddedAssetSpec>* embeddedAssets,
    ProviderImageHttpGetFn httpGetOverride)
{
    std::wstring wjson = Utf8ToWideString(json);
    if (wjson.empty()) return processedText;

    int attemptedImageCount = 0;
    int imageCount = 0;
    size_t providerAssetBytes = 0;
    constexpr size_t kMaxProviderAssetBytes = 32ull * 1024ull * 1024ull;
    constexpr size_t kMaxProviderAssetSetBytes = 64ull * 1024ull * 1024ull;
    constexpr int kMaxProviderImagesTotal = 32;
    // Absolute wall-clock budget for the whole response (all pages/images).
    constexpr ULONGLONG kJobDeadlineMs = 45000;
    const ULONGLONG jobDeadlineTick = GetTickCount64() + kJobDeadlineMs;

    // Prefer scoped rewrite: each layoutParsingResult owns its markdown.text
    // and images map. Shared relative keys (imgs/shared.jpg) therefore cannot
    // steal another page's bytes.
    size_t layoutStart = FindJsonValueStart(wjson, L"\"layoutParsingResults\"", 0);
    if (layoutStart != std::wstring::npos &&
        layoutStart < wjson.length() &&
        wjson[layoutStart] == L'[') {
        size_t layoutEnd = FindMatchingJsonChar(wjson, layoutStart, L'[', L']');
        if (layoutEnd != std::wstring::npos) {
            std::wstring rebuilt;
            int pageOrdinal = 0;
            size_t cursor = layoutStart + 1;
            bool rebuiltAny = false;
            bool rewroteAnyScopedReference = false;
            while (cursor < layoutEnd) {
                while (cursor < layoutEnd &&
                       (iswspace(wjson[cursor]) || wjson[cursor] == L',')) {
                    cursor++;
                }
                if (cursor >= layoutEnd || wjson[cursor] != L'{') break;
                size_t resultEnd = FindMatchingJsonChar(wjson, cursor, L'{', L'}');
                if (resultEnd == std::wstring::npos || resultEnd > layoutEnd) break;
                pageOrdinal++;

                std::wstring pageMarkdown;
                size_t mdValue = FindJsonValueStart(wjson, L"\"markdown\"", cursor);
                if (mdValue != std::wstring::npos && mdValue < resultEnd) {
                    if (wjson[mdValue] == L'{') {
                        size_t mdEnd = FindMatchingJsonChar(wjson, mdValue, L'{', L'}');
                        if (mdEnd != std::wstring::npos && mdEnd <= resultEnd) {
                            FindJsonStringValue(
                                wjson, L"\"text\"", mdValue, mdEnd, pageMarkdown);
                        }
                    } else if (wjson[mdValue] == L'\"') {
                        ReadJsonStringAt(wjson, mdValue, pageMarkdown);
                    }
                }

                std::vector<ProviderImageMapEntry> imageMap;
                size_t imagesValue = FindJsonValueStart(wjson, L"\"images\"", cursor);
                // Prefer images nested under markdown when present.
                size_t mdObj = FindJsonValueStart(wjson, L"\"markdown\"", cursor);
                if (mdObj != std::wstring::npos && mdObj < resultEnd &&
                    wjson[mdObj] == L'{') {
                    size_t mdEnd = FindMatchingJsonChar(wjson, mdObj, L'{', L'}');
                    if (mdEnd != std::wstring::npos && mdEnd <= resultEnd) {
                        size_t nested = FindJsonValueStart(wjson, L"\"images\"", mdObj);
                        if (nested != std::wstring::npos && nested < mdEnd &&
                            wjson[nested] == L'{') {
                            imagesValue = nested;
                        }
                    }
                }
                if (imagesValue != std::wstring::npos && imagesValue < resultEnd &&
                    wjson[imagesValue] == L'{') {
                    size_t imagesEnd =
                        FindMatchingJsonChar(wjson, imagesValue, L'{', L'}');
                    if (imagesEnd != std::wstring::npos && imagesEnd <= resultEnd) {
                        ParseProviderImagesObject(
                            wjson, imagesValue, imagesEnd, imageMap);
                    }
                }

                if (!pageMarkdown.empty() && !imageMap.empty()) {
                    rewroteAnyScopedReference =
                        MaterializeMarkdownWithImageMap(
                        pageMarkdown,
                        imageMap,
                        pageOrdinal,
                        embeddedAssets,
                        providerAssetBytes,
                        attemptedImageCount,
                        imageCount,
                        kMaxProviderAssetBytes,
                        kMaxProviderAssetSetBytes,
                        kMaxProviderImagesTotal,
                        jobDeadlineTick,
                        httpGetOverride) || rewroteAnyScopedReference;
                }
                if (!pageMarkdown.empty()) {
                    AppendMarkdownFragment(rebuilt, pageMarkdown);
                    rebuiltAny = true;
                }
                cursor = resultEnd + 1;
            }

            if (rebuiltAny) {
                if (rewroteAnyScopedReference) {
                    processedText = std::move(rebuilt);
                }
                // OWN-117: pure narrow debug (NarrowStringUtils).
                OutputDebugStringA(NarrowFormatOcrSavedImagesScoped(imageCount).c_str());
                return processedText;
            }
        }
    }

    // Fallback: single top-level / first images object against the whole text
    // (legacy PP-OCR style responses without layoutParsingResults pages).
    size_t imagesSearch = 0;
    bool foundAny = false;
    for (;;) {
        size_t valueStart = FindJsonValueStart(wjson, L"\"images\"", imagesSearch);
        if (valueStart == std::wstring::npos) break;
        imagesSearch = valueStart + 1;
        if (valueStart >= wjson.length() || wjson[valueStart] != L'{') continue;
        size_t objEnd = FindMatchingJsonChar(wjson, valueStart, L'{', L'}');
        if (objEnd == std::wstring::npos) objEnd = wjson.length();
        std::vector<ProviderImageMapEntry> imageMap;
        if (!ParseProviderImagesObject(wjson, valueStart, objEnd, imageMap)) continue;
        foundAny = true;
        MaterializeMarkdownWithImageMap(
            processedText,
            imageMap,
            1,
            embeddedAssets,
            providerAssetBytes,
            attemptedImageCount,
            imageCount,
            kMaxProviderAssetBytes,
            kMaxProviderAssetSetBytes,
            kMaxProviderImagesTotal,
            jobDeadlineTick,
            httpGetOverride);
    }

    if (!foundAny) {
        OutputDebugStringA("[OCR] No images object found\n");
        return processedText;
    }

    // OWN-117: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatOcrSavedImages(imageCount).c_str());
    return processedText;
}

BatchOcrImageLinkRewriteResult MaterializeTransientOcrEmbeddedAssets(
    const std::wstring& markdown,
    const std::wstring& canonicalSourceImagePath,
    const std::vector<OcrEmbeddedAssetSpec>& specs)
{
    BatchOcrImageLinkRewriteResult emptyResult;
    emptyResult.markdown = markdown;
    if (specs.empty()) return emptyResult;

    wchar_t sourcePath[MAX_PATH] = {};
    if (GetFullPathNameW(
            canonicalSourceImagePath.c_str(), MAX_PATH, sourcePath, nullptr) == 0) {
        emptyResult.error = L"Could not resolve transient OCR source image path.";
        return emptyResult;
    }
    // OWN-95: pure file stem (WideStringUtils).
    std::wstring sourceName = WideStripFinalExtension(WideFileNameFromPath(sourcePath));

    wchar_t cacheRoot[MAX_PATH] = {};
    GetFullPathNameW(GetOcrImageDir().c_str(), MAX_PATH, cacheRoot, nullptr);
    // OWN-74/75: pure lower + pure path-under via WideStringUtils.
    std::wstring sourceLower = WideToLower(sourcePath);
    std::wstring rootLower = WideToLower(cacheRoot);

    std::wstring assetsDir;
    // Historical: ensure trailing '\\' then prefix match (strictly under dir).
    if (WideIsPathStrictlyUnderDirectory(sourceLower, rootLower)) {
        // OWN-96: pure parent dir (WideStringUtils).
        assetsDir = WideParentDirFromPath(sourcePath);
    } else {
        assetsDir = GetOcrImageDateDir();
    }

    OcrOutputArtifactOptions transientOptions;
    transientOptions.embeddedAssetFormat = PdfRenderImageFormat::Auto;
    transientOptions.embeddedAssetQuality = 90;
    BatchOcrImageLinkRewriteResult result = MaterializeOcrEmbeddedAssets(
        markdown,
        canonicalSourceImagePath,
        specs,
        assetsDir,
        1,
        transientOptions,
        OcrEmbeddedAssetReferenceKind::LocalhostCache,
        sourceName);
    if (!result.error.empty()) return result;

    for (const auto& ownedPath : result.ownedFiles) {
        std::wstring forward = ownedPath;
        for (wchar_t& ch : forward) if (ch == L'\\') ch = L'/';
        // OWN-121: pure localhost base URL (WideStringUtils).
        const std::wstring url = WideFormatLocalhostBase(
            MiniHttpServer::Instance().GetPort()) +
            L"?path=" + UrlEncode(forward);
        size_t pos = 0;
        while ((pos = result.markdown.find(ownedPath, pos)) != std::wstring::npos) {
            result.markdown.replace(pos, ownedPath.size(), url);
            pos += url.size();
        }
    }
    // Transient History owns these files directly, so there is no later
    // metadata commit to coordinate with. Finalize its local transaction now.
    CommitOcrAssetTransaction(result.transaction);
    result.assets.clear();
    return result;
}

bool HttpDownloadFile(const std::wstring& url, const std::wstring& savePath) {
    HINTERNET hSession = WinHttpOpen(L"ZenCrop/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = 1;
    urlComp.dwHostNameLength = 1;
    urlComp.dwUrlPathLength = 1;
    urlComp.dwExtraInfoLength = 1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        isHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
        if (statusCode != 200) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        std::ofstream file(savePath, std::ios::binary);
        if (file.is_open()) {
            DWORD bytesAvailable = 0;
            while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
                std::vector<BYTE> buf(bytesAvailable);
                DWORD bytesRead = 0;
                if (WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead)) {
                    file.write((char*)buf.data(), bytesRead);
                } else {
                    break;
                }
                bytesAvailable = 0;
            }
            file.close();
            ok = true;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

static const LayoutClassInfo g_layoutClassInfo[] = {
    { 0,  L"abstract",          L"OCR:",                0, false, false, false },
    { 1,  L"algorithm",         L"OCR:",                0, false, false, false },
    { 2,  L"aside_text",        L"OCR:",                0, true,  false, true  },
    { 3,  L"chart",             L"Chart Recognition:",  0, true,  true,  false },
    { 4,  L"content",           L"OCR:",                0, true,  false, false },
    { 5,  L"display_formula",   L"Formula Recognition:",0, false, false, false },
    { 6,  L"doc_title",         L"OCR:",                1, false, false, false },
    { 7,  L"figure_title",      L"OCR:",                0, false, false, false },
    { 8,  L"footer",            L"OCR:",                0, true,  false, true  },
    { 9,  L"footer_image",      L"OCR:",                0, true,  false, true  },
    { 10, L"footnote",          L"OCR:",                0, false, false, false },
    { 11, L"formula_number",    L"OCR:",                0, false, false, false },
    { 12, L"header",            L"OCR:",                0, true,  false, true  },
    { 13, L"header_image",      L"OCR:",                0, true,  false, true  },
    { 14, L"image",             L"OCR:",                0, true,  true,  false },
    { 15, L"inline_formula",    L"Formula Recognition:",0, false, false, false },
    { 16, L"number",            L"OCR:",                0, true,  false, true  },
    { 17, L"paragraph_title",   L"OCR:",                2, false, false, false },
    { 18, L"reference",         L"OCR:",                0, false, false, false },
    { 19, L"reference_content", L"OCR:",                0, false, false, false },
    { 20, L"seal",              L"Seal Recognition:",   0, false, true,  false },
    { 21, L"table",             L"Table Recognition:",  0, false, false, false },
    { 22, L"text",              L"OCR:",                0, false, false, false },
    { 23, L"vertical_text",     L"OCR:",                0, false, false, false },
    { 24, L"vision_footnote",   L"OCR:",                0, false, false, false },
};

const LayoutClassInfo* GetLayoutClassInfo(int classId) {
    if (classId < 0 || classId >= LAYOUT_NUM_CLASSES) return nullptr;
    return &g_layoutClassInfo[classId];
}

void CleanOcrImageDir() {
    // Auto-cleanup disabled: cropped OCR images are kept until the user
    // manually removes them from the ocr_images directory.
}

// OWN-75: thin wrapper over pure WideNormalizeEditText.
std::wstring NormalizeEditText(const std::wstring& text) {
    return WideNormalizeEditText(text);
}

std::wstring StripOcrEmbeddedAssetMarkup(
    const std::wstring& text,
    const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets)
{
    std::wstring stripped = text;
    for (const auto& asset : embeddedAssets) {
        const std::wstring& placeholder = asset.placeholderUri;
        if (placeholder.empty()) continue;

        size_t placeholderPos = stripped.find(placeholder);
        while (placeholderPos != std::wstring::npos) {
            size_t eraseStart = placeholderPos;
            size_t eraseEnd = placeholderPos + placeholder.size();

            // Local document crops are emitted as a centered div containing
            // one img tag. Remove that complete block when it owns this URI.
            size_t divStart = stripped.rfind(L"<div", placeholderPos);
            size_t divEnd = stripped.find(L"</div>", eraseEnd);
            constexpr const wchar_t* kLocalAssetDiv =
                L"<div style=\"text-align: center;\">";
            if (divStart != std::wstring::npos && divEnd != std::wstring::npos &&
                stripped.compare(divStart, wcslen(kLocalAssetDiv), kLocalAssetDiv) == 0 &&
                stripped.find(L'>', divStart) < placeholderPos) {
                eraseStart = divStart;
                eraseEnd = divEnd + 6;
            } else {
                // Provider Markdown can use either HTML img or Markdown image
                // syntax. Remove the smallest complete image expression.
                size_t imgStart = stripped.rfind(L"<img", placeholderPos);
                size_t imgEnd = stripped.find(L'>', eraseEnd);
                const size_t priorImgEnd = imgStart == std::wstring::npos
                    ? std::wstring::npos
                    : stripped.find(L'>', imgStart + 4);
                if (imgStart != std::wstring::npos && imgEnd != std::wstring::npos &&
                    priorImgEnd != std::wstring::npos && priorImgEnd >= placeholderPos) {
                    eraseStart = imgStart;
                    eraseEnd = imgEnd + 1;
                } else {
                    size_t markdownStart = stripped.rfind(L"![", placeholderPos);
                    const size_t markdownCloseBracket = markdownStart == std::wstring::npos
                        ? std::wstring::npos
                        : stripped.find(L']', markdownStart + 2);
                    const size_t markdownOpenParen = markdownCloseBracket == std::wstring::npos
                        ? std::wstring::npos
                        : stripped.find(L'(', markdownCloseBracket + 1);
                    const size_t markdownEnd = markdownOpenParen == std::wstring::npos
                        ? std::wstring::npos
                        : stripped.find(L')', markdownOpenParen + 1);
                    if (markdownStart != std::wstring::npos && markdownEnd != std::wstring::npos &&
                        markdownCloseBracket != std::wstring::npos &&
                        markdownOpenParen != std::wstring::npos &&
                        markdownOpenParen < placeholderPos &&
                        placeholderPos < markdownEnd &&
                        markdownCloseBracket < placeholderPos) {
                        eraseStart = markdownStart;
                        eraseEnd = markdownEnd + 1;
                    } else {
                        // A bare provider result URI occupies its line. Drop
                        // the line instead of copying an internal placeholder.
                        size_t lineStart = stripped.rfind(L'\n', placeholderPos);
                        lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
                        size_t lineEnd = stripped.find(L'\n', eraseEnd);
                        eraseStart = lineStart;
                        eraseEnd = lineEnd == std::wstring::npos ? stripped.size() : lineEnd + 1;
                    }
                }
            }

            stripped.erase(eraseStart, eraseEnd - eraseStart);
            placeholderPos = stripped.find(placeholder, eraseStart);
        }
    }

    // Avoid leaving large blank runs after removing adjacent image blocks.
    for (;;) {
        size_t blankRun = stripped.find(L"\r\n\r\n\r\n");
        if (blankRun == std::wstring::npos) break;
        stripped.erase(blankRun, 2);
    }
    for (;;) {
        size_t blankRun = stripped.find(L"\n\n\n");
        if (blankRun == std::wstring::npos) break;
        stripped.erase(blankRun, 1);
    }
    return stripped;
}
