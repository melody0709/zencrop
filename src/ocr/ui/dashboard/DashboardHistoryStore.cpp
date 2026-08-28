#include "ocr/ui/dashboard/DashboardHistoryStore.h"
#include "JsonUtils.h"
#include "OcrBlockJson.h"
#include "ocr/batch/BatchOcrTypes.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <cwctype>
#include <vector>

static std::vector<std::wstring> ParseHistoryStringArray(const std::wstring& arrayText) {
    std::vector<std::wstring> values;
    size_t pos = arrayText.find(L'[');
    if (pos == std::wstring::npos) return values;
    ++pos;
    while (pos < arrayText.size()) {
        size_t start = arrayText.find(L'\"', pos);
        if (start == std::wstring::npos) break;
        size_t end = start + 1;
        while (end < arrayText.size()) {
            if (arrayText[end] == L'\\') {
                end += 2;
                continue;
            }
            if (arrayText[end] == L'\"') break;
            ++end;
        }
        if (end >= arrayText.size()) break;
        values.push_back(UnescapeJsonString(arrayText.substr(start + 1, end - start - 1)));
        pos = end + 1;
    }
    return values;
}

static bool IsDismissedManifestJsonWhitespace(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

static void SkipDismissedManifestJsonWhitespace(
    const std::wstring& json,
    size_t& pos)
{
    while (pos < json.size() && IsDismissedManifestJsonWhitespace(json[pos])) ++pos;
}

static int DismissedManifestJsonHexValue(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
    if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
    return -1;
}

static bool ParseDismissedManifestJsonHex4(
    const std::wstring& json,
    size_t pos,
    wchar_t& value)
{
    if (pos + 4 > json.size()) return false;
    unsigned int code = 0;
    for (size_t i = 0; i < 4; ++i) {
        int digit = DismissedManifestJsonHexValue(json[pos + i]);
        if (digit < 0) return false;
        code = (code << 4) | static_cast<unsigned int>(digit);
    }
    value = static_cast<wchar_t>(code);
    return true;
}

static bool ParseDismissedManifestJsonString(
    const std::wstring& json,
    size_t& pos,
    std::wstring& value)
{
    value.clear();
    if (pos >= json.size() || json[pos] != L'"') return false;
    ++pos;
    while (pos < json.size()) {
        wchar_t ch = json[pos++];
        if (ch == L'"') return true;
        if (ch < 0x20) return false;
        if (ch != L'\\') {
            value.push_back(ch);
            continue;
        }
        if (pos >= json.size()) return false;
        wchar_t escaped = json[pos++];
        switch (escaped) {
        case L'"': value.push_back(L'"'); break;
        case L'\\': value.push_back(L'\\'); break;
        case L'/': value.push_back(L'/'); break;
        case L'b': value.push_back(L'\b'); break;
        case L'f': value.push_back(L'\f'); break;
        case L'n': value.push_back(L'\n'); break;
        case L'r': value.push_back(L'\r'); break;
        case L't': value.push_back(L'\t'); break;
        case L'u': {
            wchar_t codeUnit = 0;
            if (!ParseDismissedManifestJsonHex4(json, pos, codeUnit)) return false;
            pos += 4;
            if (codeUnit >= 0xD800 && codeUnit <= 0xDBFF) {
                if (pos + 6 > json.size() || json[pos] != L'\\' || json[pos + 1] != L'u') {
                    return false;
                }
                wchar_t lowSurrogate = 0;
                if (!ParseDismissedManifestJsonHex4(json, pos + 2, lowSurrogate) ||
                    lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF) {
                    return false;
                }
                value.push_back(codeUnit);
                value.push_back(lowSurrogate);
                pos += 6;
            } else if (codeUnit >= 0xDC00 && codeUnit <= 0xDFFF) {
                return false;
            } else {
                value.push_back(codeUnit);
            }
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

bool DashboardHistoryIsEmptyJson(const std::wstring& json) {
    // OWN-80: pure outer trim (WideStringUtils); allow whitespace inside [].
    const std::wstring trimmed = WideTrim(json);
    if (trimmed.size() < 2 || trimmed.front() != L'[' || trimmed.back() != L']') {
        return false;
    }
    for (size_t i = 1; i + 1 < trimmed.size(); ++i) {
        if (!iswspace(trimmed[i])) return false;
    }
    return true;
}

bool DashboardHistoryIsStructurallyCompleteJson(const std::wstring& json) {
    // OWN-80: pure trim (WideStringUtils) for outer bounds, then structural scan.
    const std::wstring trimmed = WideTrim(json);
    if (trimmed.size() < 2 || trimmed.front() != L'[' || trimmed.back() != L']') {
        return false;
    }

    std::vector<wchar_t> stack;
    bool inString = false;
    bool escaped = false;
    bool rootClosed = false;
    for (size_t i = 0; i < trimmed.size(); ++i) {
        wchar_t ch = trimmed[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == L'\\') {
                escaped = true;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }

        if (ch == L'"') {
            inString = true;
            continue;
        }
        if (rootClosed) {
            // After root closes, only whitespace may remain (should not after trim,
            // but keep defensive).
            if (!iswspace(ch)) return false;
            continue;
        }
        switch (ch) {
        case L'[':
            stack.push_back(ch);
            break;
        case L']':
            if (stack.empty() || stack.back() != L'[') return false;
            stack.pop_back();
            if (stack.empty()) rootClosed = true;
            break;
        case L'{':
            if (stack.empty()) return false;
            stack.push_back(ch);
            break;
        case L'}':
            if (stack.empty() || stack.back() != L'{') return false;
            stack.pop_back();
            break;
        default:
            break;
        }
    }
    return rootClosed && !inString && !escaped && stack.empty();
}

std::vector<OcrDashboardHistoryItem> DashboardHistoryParseJson(const std::wstring& json) {
    std::vector<OcrDashboardHistoryItem> items;
    size_t pos = 0;
    while (true) {
        size_t startObj = json.find(L"{", pos);
        if (startObj == std::wstring::npos) break;

        size_t endObj = startObj + 1;
        int depth = 1;
        bool inStr = false;
        while (endObj < json.length() && depth > 0) {
            if (inStr) {
                if (json[endObj] == L'\\' && endObj + 1 < json.length()) endObj++;
                else if (json[endObj] == L'"') inStr = false;
            } else {
                if (json[endObj] == L'"') inStr = true;
                else if (json[endObj] == L'{') depth++;
                else if (json[endObj] == L'}') depth--;
            }
            endObj++;
        }

        std::wstring objStr = json.substr(startObj, endObj - startObj);
        pos = endObj;
        if (objStr.find(L"\"timestamp\"") == std::wstring::npos) continue;

        OcrDashboardHistoryItem item;
        std::wstring sourceInstanceId = UnescapeJsonString(ExtractJsonField(objStr, L"sourceInstanceId"));
        if (IsValidBatchOcrSourceInstanceId(sourceInstanceId)) {
            item.sourceInstanceId = std::move(sourceInstanceId);
        }
        item.recordKind = UnescapeJsonString(ExtractJsonField(objStr, L"recordKind"));
        item.originKind = UnescapeJsonString(ExtractJsonField(objStr, L"originKind"));
        if (item.originKind != L"ImportedImage" && item.originKind != L"Capture") {
            item.originKind.clear();
        }
        item.originManifestPath = UnescapeJsonString(ExtractJsonField(objStr, L"originManifestPath"));
        item.engineMode = UnescapeJsonString(ExtractJsonField(objStr, L"engineMode"));
        item.timestamp = UnescapeJsonString(ExtractJsonField(objStr, L"timestamp"));
        item.imagePath = UnescapeJsonString(ExtractJsonField(objStr, L"imagePath"));
        item.text = UnescapeJsonString(ExtractJsonField(objStr, L"text"));
        item.rawOcrJson = UnescapeJsonString(ExtractJsonField(objStr, L"rawOcrJson"));
        item.debugOutputImagesJson = UnescapeJsonString(ExtractJsonField(objStr, L"debugOutputImagesJson"));
        item.ownedCacheFiles = ParseHistoryStringArray(
            ExtractJsonField(objStr, L"ownedCacheFiles"));
        // OWN-77: pure int parse (WideStringUtils).
        item.elapsedMs = WideParseJsonIntToken(ExtractJsonField(objStr, L"elapsedMs"));

        std::wstring bboxesStr = ExtractJsonField(objStr, L"bboxes");
        if (!bboxesStr.empty() && bboxesStr[0] == L'[') {
            size_t bp = 1;
            while (true) {
                size_t bStart = bboxesStr.find(L"{", bp);
                if (bStart == std::wstring::npos) break;
                size_t bEnd = bboxesStr.find(L"}", bStart);
                if (bEnd == std::wstring::npos) break;
                std::wstring bStr = bboxesStr.substr(bStart, bEnd - bStart + 1);
                bp = bEnd + 1;

                RECT r = {};
                // OWN-77: pure int parse (WideStringUtils).
                r.left = WideParseJsonIntToken(ExtractJsonField(bStr, L"left"));
                r.top = WideParseJsonIntToken(ExtractJsonField(bStr, L"top"));
                r.right = WideParseJsonIntToken(ExtractJsonField(bStr, L"right"));
                r.bottom = WideParseJsonIntToken(ExtractJsonField(bStr, L"bottom"));
                std::wstring cls = UnescapeJsonString(ExtractJsonField(bStr, L"class"));
                if (cls.empty()) cls = L"text";

                item.bboxes.push_back(r);
                item.bboxClasses.push_back(cls);
            }
        }

        item.blocks = ParseOcrLayoutBlocks(objStr, 0);

        if (!item.imagePath.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

std::wstring DashboardHistorySerializeJson(const std::vector<OcrDashboardHistoryItem>& items) {
    std::wstring json = L"[\n";
    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        json += L"  {\n";
        if (IsValidBatchOcrSourceInstanceId(item.sourceInstanceId)) {
            json += L"    \"sourceInstanceId\": \"" + EscapeJsonString(item.sourceInstanceId) + L"\",\n";
        }
        if (!item.recordKind.empty()) {
            json += L"    \"recordKind\": \"" + EscapeJsonString(item.recordKind) + L"\",\n";
        }
        if (!item.originKind.empty()) {
            json += L"    \"originKind\": \"" + EscapeJsonString(item.originKind) + L"\",\n";
        }
        if (!item.originManifestPath.empty()) {
            json += L"    \"originManifestPath\": \"" + EscapeJsonString(item.originManifestPath) + L"\",\n";
        }
        if (!item.engineMode.empty()) {
            json += L"    \"engineMode\": \"" + EscapeJsonString(item.engineMode) + L"\",\n";
        }
        json += L"    \"timestamp\": \"" + EscapeJsonString(item.timestamp) + L"\",\n";
        json += L"    \"imagePath\": \"" + EscapeJsonString(item.imagePath) + L"\",\n";
        json += L"    \"text\": \"" + EscapeJsonString(item.text) + L"\",\n";
        // OWN-126: pure int label for JSON field (WideStringUtils).
        json += L"    \"elapsedMs\": " + WideFormatIntLabel(item.elapsedMs) + L",\n";
        json += L"    \"bboxes\": [\n";
        for (size_t j = 0; j < item.bboxes.size(); j++) {
            RECT r = item.bboxes[j];
            std::wstring cls = j < item.bboxClasses.size() ? item.bboxClasses[j] : L"text";
            // OWN-126: pure compact bbox JSON (WideStringUtils).
            json += L"      {" + WideJsonFieldIntCompact(L"left", r.left) +
                    L"," + WideJsonFieldIntCompact(L"top", r.top) +
                    L"," + WideJsonFieldIntCompact(L"right", r.right) +
                    L"," + WideJsonFieldIntCompact(L"bottom", r.bottom) +
                    L",\"class\":\"" + EscapeJsonString(cls) + L"\"}";
            json += (j + 1 < item.bboxes.size()) ? L",\n" : L"\n";
        }
        json += L"    ],\n";
        json += L"    \"blocks\": " + OcrLayoutBlocksToJson(item.blocks, 4) + L",\n";
        json += L"    \"rawOcrJson\": \"" + EscapeJsonString(item.rawOcrJson) + L"\",\n";
        json += L"    \"debugOutputImagesJson\": \"" + EscapeJsonString(item.debugOutputImagesJson) + L"\",\n";
        json += L"    \"ownedCacheFiles\": [";
        for (size_t j = 0; j < item.ownedCacheFiles.size(); ++j) {
            if (j > 0) json += L", ";
            json += L"\"" + EscapeJsonString(item.ownedCacheFiles[j]) + L"\"";
        }
        json += L"]\n";
        json += L"  }";
        json += (i + 1 < items.size()) ? L",\n" : L"\n";
    }
    json += L"]";
    return json;
}

std::wstring DashboardHistorySerializeDismissedManifests(
    const std::set<std::wstring>& manifestKeys)
{
    std::wstring json = L"[\n";
    size_t index = 0;
    for (const auto& manifestKey : manifestKeys) {
        json += L"  \"" + EscapeJsonString(manifestKey) + L"\"";
        json += (++index < manifestKeys.size()) ? L",\n" : L"\n";
    }
    json += L"]";
    return json;
}

bool DashboardHistoryParseDismissedManifestKeys(
    const std::wstring& json,
    std::set<std::wstring>& manifestKeys)
{
    manifestKeys.clear();
    size_t pos = 0;
    if (!json.empty() && json.front() == 0xFEFF) ++pos;
    SkipDismissedManifestJsonWhitespace(json, pos);
    if (pos >= json.size() || json[pos++] != L'[') return false;
    SkipDismissedManifestJsonWhitespace(json, pos);
    if (pos < json.size() && json[pos] == L']') {
        ++pos;
        SkipDismissedManifestJsonWhitespace(json, pos);
        return pos == json.size();
    }

    while (pos < json.size()) {
        std::wstring key;
        if (!ParseDismissedManifestJsonString(json, pos, key)) return false;
        if (key.empty() || key.rfind(L"manifest:", 0) != 0 ||
            std::any_of(key.begin(), key.end(), [](wchar_t ch) { return ch < 0x20; })) {
            return false;
        }
        key = WideToLower(std::move(key)); // OWN-79
        manifestKeys.insert(std::move(key));

        SkipDismissedManifestJsonWhitespace(json, pos);
        if (pos >= json.size()) return false;
        if (json[pos] == L']') {
            ++pos;
            SkipDismissedManifestJsonWhitespace(json, pos);
            return pos == json.size();
        }
        if (json[pos++] != L',') return false;
        SkipDismissedManifestJsonWhitespace(json, pos);
        if (pos >= json.size() || json[pos] == L']') return false;
    }
    return false;
}


std::wstring DashboardHistoryNormalizePath(std::wstring path) {
    for (auto& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
    path = WideToLower(std::move(path)); // OWN-79
    return path;
}

std::wstring DashboardHistoryDismissalBaseKey(const std::wstring& manifestPath) {
    if (manifestPath.empty()) return L"";
    return L"manifest:" + DashboardHistoryNormalizePath(manifestPath);
}

std::wstring DashboardHistoryNormalizeDismissalKey(std::wstring key) {
    key = WideToLower(std::move(key)); // OWN-79
    return key;
}

std::wstring DashboardHistoryBuildImageDismissalKey(
    const std::wstring& manifestPath,
    const std::wstring& sourceInstanceId,
    const std::wstring& createdAt,
    const std::wstring& sourcePath)
{
    std::wstring key = DashboardHistoryDismissalBaseKey(manifestPath);
    if (key.empty()) return L"";
    if (IsValidBatchOcrSourceInstanceId(sourceInstanceId)) {
        return DashboardHistoryNormalizeDismissalKey(
            key + L"|image:id:" + sourceInstanceId);
    }
    if (!createdAt.empty() || !sourcePath.empty()) {
        const std::wstring sourceKey = sourcePath.empty()
            ? L""
            : DashboardHistoryNormalizePath(sourcePath);
        return DashboardHistoryNormalizeDismissalKey(
            key + L"|image:created:" + createdAt +
            L"|source:" + sourceKey);
    }
    return DashboardHistoryNormalizeDismissalKey(key);
}

std::wstring DashboardHistoryBuildPdfDismissalKey(
    const std::wstring& manifestPath,
    const std::wstring& createdAt,
    const std::wstring& sourcePath)
{
    std::wstring key = DashboardHistoryDismissalBaseKey(manifestPath);
    if (key.empty()) return L"";
    if (!createdAt.empty() || !sourcePath.empty()) {
        const std::wstring sourceKey = sourcePath.empty()
            ? L""
            : DashboardHistoryNormalizePath(sourcePath);
        return DashboardHistoryNormalizeDismissalKey(
            key + L"|pdf:created:" + createdAt +
            L"|source:" + sourceKey);
    }
    return DashboardHistoryNormalizeDismissalKey(key);
}

std::wstring DashboardHistoryBuildHistoryItemDismissalKey(
    const std::wstring& originManifestPath,
    const std::wstring& sourceInstanceId)
{
    std::wstring key = DashboardHistoryDismissalBaseKey(originManifestPath);
    if (key.empty()) return L"";
    if (IsValidBatchOcrSourceInstanceId(sourceInstanceId)) {
        return DashboardHistoryNormalizeDismissalKey(
            key + L"|image:id:" + sourceInstanceId);
    }
    return DashboardHistoryNormalizeDismissalKey(key);
}

// D-C-S4: pure dismissed-key membership (primary key, else legacy path-wide base key).
bool DashboardHistoryIsDismissalKeyPresent(
    const std::vector<std::wstring>& dismissedKeys,
    const std::wstring& key)
{
    if (key.empty()) return false;
    return std::find(dismissedKeys.begin(), dismissedKeys.end(), key) != dismissedKeys.end();
}

bool DashboardHistoryIsImageJobDismissed(
    const std::vector<std::wstring>& dismissedKeys,
    const std::wstring& manifestPath,
    const std::wstring& sourceInstanceId,
    const std::wstring& createdAt,
    const std::wstring& sourcePath)
{
    const std::wstring key = DashboardHistoryBuildImageDismissalKey(
        manifestPath, sourceInstanceId, createdAt, sourcePath);
    if (key.empty()) return false;
    if (DashboardHistoryIsDismissalKeyPresent(dismissedKeys, key)) return true;
    const std::wstring legacyKey = DashboardHistoryNormalizeDismissalKey(
        DashboardHistoryDismissalBaseKey(manifestPath));
    return legacyKey != key &&
        DashboardHistoryIsDismissalKeyPresent(dismissedKeys, legacyKey);
}

bool DashboardHistoryIsPdfJobDismissed(
    const std::vector<std::wstring>& dismissedKeys,
    const std::wstring& manifestPath,
    const std::wstring& createdAt,
    const std::wstring& sourcePath)
{
    const std::wstring key = DashboardHistoryBuildPdfDismissalKey(
        manifestPath, createdAt, sourcePath);
    if (key.empty()) return false;
    if (DashboardHistoryIsDismissalKeyPresent(dismissedKeys, key)) return true;
    const std::wstring legacyKey = DashboardHistoryNormalizeDismissalKey(
        DashboardHistoryDismissalBaseKey(manifestPath));
    return legacyKey != key &&
        DashboardHistoryIsDismissalKeyPresent(dismissedKeys, legacyKey);
}
