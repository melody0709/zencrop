#include "OcrDocumentTypes.h"

#include "JsonUtils.h"
#include "OcrBlockJson.h"
#include "core/WideJsonUtils.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace {

// OWN-78: thin wrappers over pure WideStringUtils.
std::wstring Lower(std::wstring value) {
    return WideToLower(std::move(value));
}

std::wstring Indent(int count) {
    return WideJsonIndent(count);
}

std::wstring JsonString(const std::wstring& value) {
    return L"\"" + EscapeJsonString(value) + L"\"";
}

int64_t JsonInt64(const std::wstring& object, const wchar_t* key, int64_t fallback) {
    const std::wstring raw = OcrBlockJsonExtractValue(object, key);
    long long value = 0;
    // OWN-78: pure strict int64 parse (WideStringUtils).
    if (!WideTryParseJsonInt64Token(raw, value)) return fallback;
    return static_cast<int64_t>(value);
}

bool IsTokenTerminator(wchar_t ch) {
    return iswspace(ch) || ch == L'\"' || ch == L'\'' || ch == L',' ||
        ch == L'}' || ch == L']' || ch == L'<' || ch == L'>';
}

void RedactPrefixedToken(
    std::wstring& value,
    const std::wstring& lowerPrefix,
    const std::wstring& replacement)
{
    std::wstring lower = Lower(value);
    size_t search = 0;
    while (search < lower.size()) {
        size_t begin = lower.find(lowerPrefix, search);
        if (begin == std::wstring::npos) break;
        size_t secretBegin = begin + lowerPrefix.size();
        while (secretBegin < value.size() && iswspace(value[secretBegin])) ++secretBegin;
        size_t end = secretBegin;
        while (end < value.size() && !IsTokenTerminator(value[end])) ++end;
        if (end == secretBegin) {
            search = secretBegin;
            continue;
        }
        value.replace(secretBegin, end - secretBegin, replacement);
        lower = Lower(value);
        search = secretBegin + replacement.size();
    }
}

void RedactAuthorizationHeaders(std::wstring& value) {
    std::wstring lower = Lower(value);
    size_t search = 0;
    while (search < lower.size()) {
        size_t begin = lower.find(L"authorization:", search);
        if (begin == std::wstring::npos) break;
        size_t secretBegin = begin + wcslen(L"authorization:");
        while (secretBegin < value.size() &&
            (value[secretBegin] == L' ' || value[secretBegin] == L'\t')) {
            ++secretBegin;
        }
        size_t end = secretBegin;
        while (end < value.size() && value[end] != L'\r' && value[end] != L'\n') ++end;
        static const std::wstring kRedacted = L"<redacted>";
        value.replace(secretBegin, end - secretBegin, kRedacted);
        lower = Lower(value);
        search = secretBegin + kRedacted.size();
    }
}

void RedactJsonStringValue(std::wstring& value, const std::wstring& key) {
    std::wstring lower = Lower(value);
    const std::wstring pattern = L"\"" + Lower(key) + L"\"";
    size_t search = 0;
    while (search < lower.size()) {
        size_t keyPos = lower.find(pattern, search);
        if (keyPos == std::wstring::npos) break;
        size_t colon = value.find(L':', keyPos + pattern.size());
        if (colon == std::wstring::npos) break;
        size_t quote = colon + 1;
        while (quote < value.size() && iswspace(value[quote])) ++quote;
        if (quote >= value.size() || value[quote] != L'\"') {
            search = colon + 1;
            continue;
        }
        size_t end = quote + 1;
        while (end < value.size()) {
            if (value[end] == L'\\' && end + 1 < value.size()) {
                end += 2;
                continue;
            }
            if (value[end] == L'\"') break;
            ++end;
        }
        if (end >= value.size()) break;
        static const std::wstring kRedacted = L"<redacted>";
        value.replace(quote + 1, end - quote - 1, kRedacted);
        lower = Lower(value);
        search = quote + 1 + kRedacted.size();
    }
}

void RedactUrlQueries(std::wstring& value) {
    std::wstring lower = Lower(value);
    size_t search = 0;
    while (search < lower.size()) {
        size_t http = lower.find(L"http", search);
        if (http == std::wstring::npos) break;
        if (lower.compare(http, 7, L"http://") != 0 &&
            lower.compare(http, 8, L"https://") != 0) {
            search = http + 4;
            continue;
        }
        size_t query = value.find(L'?', http);
        size_t urlEnd = http;
        while (urlEnd < value.size() && !IsTokenTerminator(value[urlEnd])) ++urlEnd;
        if (query != std::wstring::npos && query < urlEnd) {
            static const std::wstring kRedactedUrl = L"<remote-url>";
            value.replace(http, urlEnd - http, kRedactedUrl);
            lower = Lower(value);
            search = http + kRedactedUrl.size();
        } else {
            search = urlEnd;
        }
    }
}

void RedactLocalPaths(std::wstring& value) {
    size_t i = 0;
    while (i < value.size()) {
        const bool drivePath = i + 2 < value.size() && iswalpha(value[i]) &&
            (i == 0 || !iswalpha(value[i - 1])) &&
            value[i + 1] == L':' && (value[i + 2] == L'\\' || value[i + 2] == L'/') &&
            !(i + 3 < value.size() && value[i + 3] == value[i + 2]);
        const bool uncPath = i + 2 < value.size() && value[i] == L'\\' &&
            value[i + 1] == L'\\' && value[i + 2] != L'\\';
        if (!drivePath && !uncPath) {
            ++i;
            continue;
        }
        size_t end = i;
        while (end < value.size() && value[end] != L'\r' && value[end] != L'\n' &&
            value[end] != L'\"' && value[end] != L'\'') {
            ++end;
        }
        static const std::wstring kLocalPath = L"<local-path>";
        value.replace(i, end - i, kLocalPath);
        i += kLocalPath.size();
    }
}

} // namespace

const wchar_t* DocumentOcrTransportStateToString(DocumentOcrTransportState state) {
    switch (state) {
    case DocumentOcrTransportState::NotSubmitted: return L"not_submitted";
    case DocumentOcrTransportState::Submitting: return L"submitting";
    case DocumentOcrTransportState::Pending: return L"pending";
    case DocumentOcrTransportState::Running: return L"running";
    case DocumentOcrTransportState::Downloading: return L"downloading";
    case DocumentOcrTransportState::Normalizing: return L"normalizing";
    case DocumentOcrTransportState::Materializing: return L"materializing";
    case DocumentOcrTransportState::Completed: return L"completed";
    case DocumentOcrTransportState::Failed: return L"failed";
    case DocumentOcrTransportState::Expired: return L"expired";
    case DocumentOcrTransportState::Detached: return L"detached";
    case DocumentOcrTransportState::FallbackPending: return L"fallback_pending";
    default: return L"unknown";
    }
}

DocumentOcrTransportState DocumentOcrTransportStateFromString(const std::wstring& value) {
    const std::wstring lower = Lower(value);
    if (lower == L"not_submitted") return DocumentOcrTransportState::NotSubmitted;
    if (lower == L"submitting") return DocumentOcrTransportState::Submitting;
    if (lower == L"pending") return DocumentOcrTransportState::Pending;
    if (lower == L"running") return DocumentOcrTransportState::Running;
    if (lower == L"downloading") return DocumentOcrTransportState::Downloading;
    if (lower == L"normalizing") return DocumentOcrTransportState::Normalizing;
    if (lower == L"materializing") return DocumentOcrTransportState::Materializing;
    if (lower == L"completed") return DocumentOcrTransportState::Completed;
    if (lower == L"failed") return DocumentOcrTransportState::Failed;
    if (lower == L"expired") return DocumentOcrTransportState::Expired;
    if (lower == L"detached") return DocumentOcrTransportState::Detached;
    if (lower == L"fallback_pending") return DocumentOcrTransportState::FallbackPending;
    return DocumentOcrTransportState::Unknown;
}

const wchar_t* OcrAlignmentStateToString(OcrAlignmentState state) {
    switch (state) {
    case OcrAlignmentState::NotChecked: return L"not_checked";
    case OcrAlignmentState::Verified: return L"verified";
    case OcrAlignmentState::TextOnlyWarning: return L"text_only_warning";
    case OcrAlignmentState::Unresolved: return L"unresolved";
    case OcrAlignmentState::Ambiguous: return L"ambiguous";
    case OcrAlignmentState::Failed: return L"failed";
    default: return L"not_checked";
    }
}

OcrAlignmentState OcrAlignmentStateFromString(const std::wstring& value) {
    const std::wstring lower = Lower(value);
    if (lower == L"verified") return OcrAlignmentState::Verified;
    if (lower == L"text_only_warning") return OcrAlignmentState::TextOnlyWarning;
    if (lower == L"unresolved") return OcrAlignmentState::Unresolved;
    if (lower == L"ambiguous") return OcrAlignmentState::Ambiguous;
    if (lower == L"failed") return OcrAlignmentState::Failed;
    return OcrAlignmentState::NotChecked;
}

const wchar_t* OcrBlockSourceRelationToString(OcrBlockSourceRelation relation) {
    switch (relation) {
    case OcrBlockSourceRelation::Direct: return L"direct";
    case OcrBlockSourceRelation::Alias: return L"alias";
    case OcrBlockSourceRelation::LayoutOnly: return L"layout_only";
    case OcrBlockSourceRelation::Ambiguous: return L"ambiguous";
    default: return L"unresolved";
    }
}

OcrBlockSourceRelation OcrBlockSourceRelationFromString(const std::wstring& value) {
    const std::wstring lower = Lower(value);
    if (lower == L"direct") return OcrBlockSourceRelation::Direct;
    if (lower == L"alias") return OcrBlockSourceRelation::Alias;
    if (lower == L"layout_only") return OcrBlockSourceRelation::LayoutOnly;
    if (lower == L"ambiguous") return OcrBlockSourceRelation::Ambiguous;
    return OcrBlockSourceRelation::Unresolved;
}

std::wstring OcrBlockSourceMapToJson(
    const std::vector<OcrBlockSourceMapEntry>& entries,
    int indent)
{
    std::wstringstream ss;
    const std::wstring i0 = Indent(indent);
    const std::wstring i1 = Indent(indent + 2);
    const std::wstring i2 = Indent(indent + 4);
    ss << L"[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        ss << i1 << L"{\n";
        ss << i2 << L"\"blockId\": " << JsonString(entry.blockId) << L",\n";
        ss << i2 << L"\"relation\": " << JsonString(OcrBlockSourceRelationToString(entry.relation)) << L",\n";
        ss << i2 << L"\"contentOwnerId\": " << JsonString(entry.contentOwnerId) << L",\n";
        ss << i2 << L"\"sourceStart\": " << entry.sourceStart << L",\n";
        ss << i2 << L"\"sourceEnd\": " << entry.sourceEnd << L",\n";
        ss << i2 << L"\"sourceRevisionSha256\": " << JsonString(entry.sourceRevisionSha256) << L",\n";
        ss << i2 << L"\"reason\": " << JsonString(entry.reason) << L"\n";
        ss << i1 << L"}";
        if (i + 1 < entries.size()) ss << L",";
        ss << L"\n";
    }
    ss << i0 << L"]";
    return ss.str();
}

std::vector<OcrBlockSourceMapEntry> ParseOcrBlockSourceMap(const std::wstring& json) {
    std::wstring arrayText = OcrBlockJsonExtractValue(json, L"blockSourceMap");
    if (arrayText.empty() && json.find(L'[') != std::wstring::npos) arrayText = json;
    std::vector<OcrBlockSourceMapEntry> entries;
    for (const auto& item : OcrBlockJsonObjectArrayItems(arrayText)) {
        OcrBlockSourceMapEntry entry;
        entry.blockId = OcrBlockJsonText(item, L"blockId");
        entry.relation = OcrBlockSourceRelationFromString(OcrBlockJsonText(item, L"relation"));
        entry.contentOwnerId = OcrBlockJsonText(item, L"contentOwnerId");
        entry.sourceStart = JsonInt64(item, L"sourceStart", -1);
        entry.sourceEnd = JsonInt64(item, L"sourceEnd", -1);
        entry.sourceRevisionSha256 = OcrBlockJsonText(item, L"sourceRevisionSha256");
        entry.reason = OcrBlockJsonText(item, L"reason");
        if (!entry.blockId.empty()) entries.push_back(std::move(entry));
    }
    return entries;
}

std::wstring OcrCoordinateSpaceToJson(
    const OcrCoordinateSpaceMetadata& metadata,
    int indent)
{
    const std::wstring pad = Indent(indent);
    const std::wstring inner = Indent(indent + 2);
    std::wstringstream ss;
    ss << L"{\n";
    ss << inner << L"\"canonicalImageKind\": " << JsonString(metadata.canonicalImageKind) << L",\n";
    ss << inner << L"\"canonicalImagePath\": " << JsonString(metadata.canonicalImagePath) << L",\n";
    ss << inner << L"\"canonicalImageSha256\": " << JsonString(metadata.canonicalImageSha256) << L",\n";
    ss << inner << L"\"canonicalImageWidth\": " << metadata.canonicalImageWidth << L",\n";
    ss << inner << L"\"canonicalImageHeight\": " << metadata.canonicalImageHeight << L",\n";
    ss << inner << L"\"recognitionImageWidth\": " << metadata.recognitionImageWidth << L",\n";
    ss << inner << L"\"recognitionImageHeight\": " << metadata.recognitionImageHeight << L",\n";
    ss << inner << L"\"rotationDegrees\": " << metadata.rotationDegrees << L",\n";
    ss << inner << L"\"origin\": " << JsonString(metadata.origin) << L",\n";
    ss << inner << L"\"bboxConvention\": " << JsonString(metadata.bboxConvention) << L",\n";
    ss << inner << L"\"polygonConvention\": " << JsonString(metadata.polygonConvention) << L",\n";
    ss << inner << L"\"coordinateSpaceKind\": " << JsonString(metadata.coordinateSpaceKind) << L",\n";
    ss << inner << L"\"transformVerified\": " << (WideJsonBoolLiteral(metadata.transformVerified)) << L",\n";
    ss << inner << L"\"recognitionToCanonical\": [";
    for (size_t i = 0; i < metadata.recognitionToCanonical.size(); ++i) {
        if (i) ss << L", ";
        ss << metadata.recognitionToCanonical[i];
    }
    ss << L"],\n";
    ss << inner << L"\"warning\": " << JsonString(metadata.warning) << L"\n";
    ss << pad << L"}";
    return ss.str();
}

OcrCoordinateSpaceMetadata ParseOcrCoordinateSpace(const std::wstring& json) {
    std::wstring object = OcrBlockJsonExtractValue(json, L"coordinateSpace");
    if (object.empty() && json.find(L'{') != std::wstring::npos) object = json;
    OcrCoordinateSpaceMetadata metadata;
    metadata.canonicalImageKind = OcrBlockJsonText(object, L"canonicalImageKind");
    metadata.canonicalImagePath = OcrBlockJsonText(object, L"canonicalImagePath");
    metadata.canonicalImageSha256 = OcrBlockJsonText(object, L"canonicalImageSha256");
    metadata.canonicalImageWidth = static_cast<uint32_t>((std::max)(0, OcrBlockJsonInt(object, L"canonicalImageWidth")));
    metadata.canonicalImageHeight = static_cast<uint32_t>((std::max)(0, OcrBlockJsonInt(object, L"canonicalImageHeight")));
    metadata.recognitionImageWidth = static_cast<uint32_t>((std::max)(0, OcrBlockJsonInt(object, L"recognitionImageWidth")));
    metadata.recognitionImageHeight = static_cast<uint32_t>((std::max)(0, OcrBlockJsonInt(object, L"recognitionImageHeight")));
    metadata.rotationDegrees = OcrBlockJsonInt(object, L"rotationDegrees");
    metadata.origin = OcrBlockJsonText(object, L"origin");
    metadata.bboxConvention = OcrBlockJsonText(object, L"bboxConvention");
    metadata.polygonConvention = OcrBlockJsonText(object, L"polygonConvention");
    metadata.coordinateSpaceKind = OcrBlockJsonText(object, L"coordinateSpaceKind");
    metadata.transformVerified = OcrBlockJsonBool(object, L"transformVerified");
    metadata.warning = OcrBlockJsonText(object, L"warning");
    if (metadata.origin.empty()) metadata.origin = L"top_left";
    if (metadata.bboxConvention.empty()) metadata.bboxConvention = L"xyxy_half_open";
    if (metadata.polygonConvention.empty()) metadata.polygonConvention = L"ordered_pixel_points";

    std::wstring transform = OcrBlockJsonExtractValue(object, L"recognitionToCanonical");
    size_t cursor = transform.find(L'[');
    bool completeTransform = cursor != std::wstring::npos;
    for (size_t i = 0; i < metadata.recognitionToCanonical.size() && cursor != std::wstring::npos; ++i) {
        cursor++;
        while (cursor < transform.size() && iswspace(transform[cursor])) cursor++;
        wchar_t* end = nullptr;
        double value = wcstod(transform.c_str() + cursor, &end);
        if (!end || end == transform.c_str() + cursor || !std::isfinite(value)) {
            completeTransform = false;
            break;
        }
        metadata.recognitionToCanonical[i] = value;
        cursor = static_cast<size_t>(end - transform.c_str());
        while (cursor < transform.size() && iswspace(transform[cursor])) ++cursor;
        if (i + 1 < metadata.recognitionToCanonical.size()) {
            if (cursor >= transform.size() || transform[cursor] != L',') {
                completeTransform = false;
                break;
            }
        } else if (cursor >= transform.size() || transform[cursor] != L']') {
            completeTransform = false;
        }
    }
    if (!completeTransform) {
        metadata.recognitionToCanonical = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
        metadata.transformVerified = false;
        if (metadata.warning.empty()) {
            metadata.warning = L"Persisted recognition transform is incomplete or invalid.";
        }
    }
    return metadata;
}

std::wstring OcrPageAlignmentToJson(
    const OcrPageAlignmentStatus& status,
    int indent)
{
    const std::wstring pad = Indent(indent);
    const std::wstring inner = Indent(indent + 2);
    std::wstringstream ss;
    ss << L"{\n";
    ss << inner << L"\"pageIdentity\": " << JsonString(OcrAlignmentStateToString(status.pageIdentity)) << L",\n";
    ss << inner << L"\"geometry\": " << JsonString(OcrAlignmentStateToString(status.geometry)) << L",\n";
    ss << inner << L"\"semantic\": " << JsonString(OcrAlignmentStateToString(status.semantic)) << L",\n";
    ss << inner << L"\"overall\": " << JsonString(OcrAlignmentStateToString(status.overall)) << L",\n";
    ss << inner << L"\"reason\": " << JsonString(status.reason) << L"\n";
    ss << pad << L"}";
    return ss.str();
}

OcrPageAlignmentStatus ParseOcrPageAlignment(const std::wstring& json) {
    std::wstring object = OcrBlockJsonExtractValue(json, L"alignment");
    if (object.empty() && json.find(L'{') != std::wstring::npos) object = json;
    OcrPageAlignmentStatus status;
    status.pageIdentity = OcrAlignmentStateFromString(OcrBlockJsonText(object, L"pageIdentity"));
    status.geometry = OcrAlignmentStateFromString(OcrBlockJsonText(object, L"geometry"));
    status.semantic = OcrAlignmentStateFromString(OcrBlockJsonText(object, L"semantic"));
    status.overall = OcrAlignmentStateFromString(OcrBlockJsonText(object, L"overall"));
    status.reason = OcrBlockJsonText(object, L"reason");
    return status;
}

std::wstring RedactDocumentOcrSensitiveText(const std::wstring& text) {
    std::wstring redacted = text;
    RedactAuthorizationHeaders(redacted);
    RedactPrefixedToken(redacted, L"bearer ", L"<redacted>");
    RedactPrefixedToken(redacted, L"token ", L"<redacted>");
    for (const wchar_t* key : {
            L"password", L"token", L"access_token", L"authorization",
            L"fileUrl", L"jsonUrl", L"markdownUrl", L"inputImage"}) {
        RedactJsonStringValue(redacted, key);
    }
    RedactUrlQueries(redacted);
    RedactLocalPaths(redacted);
    return redacted;
}
