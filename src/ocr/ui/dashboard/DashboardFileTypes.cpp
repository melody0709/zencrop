#include "dashboard/DashboardFileTypes.h"
#include "core/WideStringUtils.h"
#include "core/WideMarkdownUtils.h"
#include <cstdio>

namespace {
std::wstring ExtensionLower(const std::wstring& filePath) {
    size_t dot = filePath.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    return WideToLower(filePath.substr(dot + 1));
}
}

bool DashboardIsSupportedImageFile(const std::wstring& filePath) {
    const std::wstring ext = ExtensionLower(filePath);
    return ext == L"jpg" || ext == L"jpeg" || ext == L"png" ||
           ext == L"bmp" || ext == L"gif" || ext == L"tiff" ||
           ext == L"tif" || ext == L"webp" || ext == L"avif";
}

bool DashboardIsSupportedPdfFile(const std::wstring& filePath) {
    return ExtensionLower(filePath) == L"pdf";
}

namespace {
constexpr int kFolderImportMaxDepthLimit = 64;
}

int DashboardNormalizeFolderImportDepth(int depth) {
    if (depth < 0) return 0;
    if (depth > kFolderImportMaxDepthLimit) return kFolderImportMaxDepthLimit;
    return depth;
}

std::vector<std::wstring> DashboardSplitFolderExcludePatterns(const std::wstring& patterns) {
    std::vector<std::wstring> result;
    size_t start = 0;
    while (start <= patterns.size()) {
        size_t end = patterns.find_first_of(L";,\r\n", start);
        if (end == std::wstring::npos) end = patterns.size();
        std::wstring piece = WideTrim(patterns.substr(start, end - start));
        if (!piece.empty()) result.push_back(piece);
        if (end == patterns.size()) break;
        start = end + 1;
    }
    return result;
}

// OWN-71/74: thin wrappers over pure WideStringUtils helpers.
std::wstring DashboardToLowerWide(std::wstring value) {
    return WideToLower(std::move(value));
}

std::wstring DashboardTrimWide(std::wstring value) {
    return WideTrim(std::move(value));
}

bool DashboardIsTiffImageFilePath(const std::wstring& filePath) {
    const std::wstring ext = ExtensionLower(filePath);
    return ext == L"tif" || ext == L"tiff";
}

bool DashboardIsAllPageRangeText(const std::wstring& pageRange) {
    std::wstring text = WideToLower(WideTrim(pageRange));
    return text.empty() || text == L"all" || text == L"*";
}

std::wstring DashboardFileNameFromPath(const std::wstring& path) {
    return WideFileNameFromPath(path);
}

std::wstring DashboardFormatElapsedShort(unsigned long elapsedMs) {
    if (elapsedMs == 0) return L"";
    // OWN-113: thin-wrap pure elapsed formatters.
    if (elapsedMs < 1000) {
        return WideFormatElapsedMs(elapsedMs);
    }
    // Keep one decimal place, matching historical FormatElapsedShort.
    return WideFormatSeconds1(elapsedMs / 1000.0);
}

std::wstring DashboardSanitizePathSegment(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.size());
    for (wchar_t ch : input) {
        if (ch < 32 || ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' ||
            ch == L'/' || ch == L'\\' || ch == L'|' || ch == L'?' || ch == L'*') {
            result += L'_';
        } else {
            result += ch;
        }
    }
    return result;
}

// OWN-72 pure path/text + OCR mode helpers.
std::wstring DashboardTrimPreviewStem(std::wstring value) {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'.')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == L' ' || value.back() == L'.')) {
        value.pop_back();
    }
    return value;
}

std::wstring DashboardSanitizePreviewPathSegment(const std::wstring& input) {
    std::wstring result = DashboardSanitizePathSegment(input);
    result = DashboardTrimPreviewStem(result);
    if (result.empty()) result = L"document";
    if (result.size() > 80) result.resize(80);
    return result;
}

std::wstring DashboardPdfPreviewStem(const std::wstring& path) {
    std::wstring name = DashboardFileNameFromPath(path);
    if (name.empty()) name = path;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name.resize(dot);
    return DashboardSanitizePreviewPathSegment(name);
}

std::wstring DashboardAppendPreviewDuplicateSuffix(const std::wstring& base, int suffix) {
    if (suffix <= 1) return base;
    // OWN-113: thin-wrap pure dup suffix.
    return base + WideFormatDupSuffix02(suffix);
}

// OWN-74: thin wrappers over pure WideStringUtils.
std::wstring DashboardJoinPathWide(const std::wstring& left, const std::wstring& right) {
    return WideJoinPath(left, right);
}

std::wstring DashboardNormalizeOcrMode(const std::wstring& mode) {
    const std::wstring normalized = WideToLower(WideTrim(mode));
    if (normalized == L"paddle_cloud") return L"paddle_cloud";
    if (normalized == L"paddle_local") return L"paddle_local";
    if (normalized == L"ppocrv6_onnx") return L"ppocrv6_onnx";
    if (normalized == L"local") return L"local";
    // Historical aliases / empty → Windows OCR local.
    return L"local";
}

bool DashboardIsCloudOcrMode(const std::wstring& mode) {
    return DashboardNormalizeOcrMode(mode) == L"paddle_cloud";
}

int DashboardOcrModeToComboIndex(const std::wstring& mode) {
    const std::wstring normalized = DashboardNormalizeOcrMode(mode);
    if (normalized == L"paddle_cloud") return 1;
    if (normalized == L"paddle_local") return 2;
    if (normalized == L"ppocrv6_onnx") return 3;
    return 0;
}

std::wstring DashboardOcrModeFromComboIndex(int index) {
    switch (index) {
    case 1: return L"paddle_cloud";
    case 2: return L"paddle_local";
    case 3: return L"ppocrv6_onnx";
    default: return L"local";
    }
}

const wchar_t* DashboardOcrModeLabel(const std::wstring& mode) {
    const std::wstring normalized = DashboardNormalizeOcrMode(mode);
    if (normalized == L"paddle_cloud") return L"PaddleOCR Cloud";
    if (normalized == L"paddle_local") return L"PaddleOCR-VL 1.6 Local";
    if (normalized == L"ppocrv6_onnx") return L"PP-OCRv6 Local";
    return L"Windows OCR";
}

bool DashboardWideEqualsNoCase(const std::wstring& a, const std::wstring& b) {
    return WideEqualsNoCase(a, b);
}

bool DashboardFolderExcludeNameEquals(const std::wstring& pattern, const std::wstring& dirName) {
    if (pattern.empty() || dirName.empty()) return false;
    return WideEqualsNoCase(pattern, dirName);
}

std::wstring DashboardNormalizeNewlines(std::wstring text) {
    return WideNormalizeNewlines(std::move(text));
}

// OWN-74: thin wrapper over pure WidePathWithSuffix.
std::wstring DashboardPathWithSuffix(const std::wstring& path, const std::wstring& suffix) {
    return WidePathWithSuffix(path, suffix);
}

std::wstring DashboardFormatImageIndexName(int index) {
    // OWN-113: thin-wrap pure image index name.
    return WideFormatImageIndexName(index);
}

std::wstring DashboardFormatPageIndexName(int pageIndex) {
    // OWN-113: thin-wrap pure page index name.
    return WideFormatPageIndexName(pageIndex);
}

// OWN-73/74: thin wrappers over pure WideStringUtils path/text helpers.
std::wstring DashboardParentDirFromPath(const std::wstring& path) {
    return WideParentDirFromPath(path);
}

std::wstring DashboardFileStemFromPath(const std::wstring& path) {
    return WideStripFinalExtension(WideFileNameFromPath(path));
}

bool DashboardIsRelativePathWide(const std::wstring& path) {
    return WideIsRelativePath(path);
}

std::wstring DashboardResolvePathUnderRoot(
    const std::wstring& root,
    const std::wstring& relativeOrAbsolute)
{
    if (relativeOrAbsolute.empty()) return L"";
    if (!WideIsRelativePath(relativeOrAbsolute)) return relativeOrAbsolute;
    return WideJoinPath(root, relativeOrAbsolute);
}

bool DashboardContainsUnresolvedOcrAssetReference(const std::wstring& text) {
    return WideContainsUnresolvedOcrAssetReference(text);
}

std::wstring DashboardDeriveCommittedPlainText(const std::wstring& markdown) {
    return WideDeriveCommittedPlainText(markdown);
}

DashboardBatchTaskStatusKind DashboardParseBatchTaskStatus(const std::wstring& text) {
    const std::wstring lower = WideToLower(WideTrim(text));
    if (lower == L"pending") return DashboardBatchTaskStatusKind::Pending;
    if (lower == L"recognizing") return DashboardBatchTaskStatusKind::Recognizing;
    if (lower == L"writing") return DashboardBatchTaskStatusKind::Writing;
    if (lower == L"completed") return DashboardBatchTaskStatusKind::Completed;
    if (lower == L"failed") return DashboardBatchTaskStatusKind::Failed;
    if (lower == L"canceled" || lower == L"cancelled") {
        return DashboardBatchTaskStatusKind::Canceled;
    }
    return DashboardBatchTaskStatusKind::Unknown;
}

bool DashboardBatchTaskStatusIsTerminal(DashboardBatchTaskStatusKind kind) {
    return kind == DashboardBatchTaskStatusKind::Completed
        || kind == DashboardBatchTaskStatusKind::Failed
        || kind == DashboardBatchTaskStatusKind::Canceled;
}

std::wstring DashboardFormatMegabytes(double megabytes, bool oneDecimal) {
    // OWN-113: thin-wrap pure megabyte labels.
    if (oneDecimal) return WideFormatMegabytes1(megabytes);
    return WideFormatMegabytes0(megabytes);
}

std::wstring DashboardTrimTrailingSeparators(std::wstring path) {
    while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/')) {
        // Keep "C:\" drive root.
        if (path.size() == 3 && path[1] == L':') break;
        // Keep single "/" root.
        if (path.size() == 1) break;
        path.pop_back();
    }
    return path;
}

bool DashboardWideStartsWithNoCase(const std::wstring& value, const std::wstring& prefix) {
    return WideStartsWithNoCase(value, prefix);
}

bool DashboardIsHttpUrlWide(const std::wstring& value) {
    return WideIsHttpUrl(value);
}

std::wstring DashboardFormatPageAssetStem(int pageIndex, int assetIndex) {
    // OWN-113: thin-wrap pure page asset stem.
    return WideFormatPageAssetStem(pageIndex, assetIndex);
}

bool DashboardWideContainsNoCase(const std::wstring& value, const std::wstring& needle) {
    return WideContainsNoCase(value, needle);
}

bool DashboardIsDriveAbsolutePathWide(const std::wstring& path) {
    return WideIsDriveAbsolutePath(path);
}

std::wstring DashboardStripFinalExtension(std::wstring name) {
    return WideStripFinalExtension(std::move(name));
}

std::wstring DashboardJoinPathForwardSlash(const std::wstring& left, const std::wstring& right) {
    return WideJoinPathForwardSlash(left, right);
}

// OWN-75: re-exports of core WideStringUtils helpers.
std::wstring DashboardTrimTrailingLineBreaks(std::wstring text) {
    return WideTrimTrailingLineBreaks(std::move(text));
}

std::wstring DashboardStripMarkdownToPlainText(const std::wstring& input) {
    return WideStripMarkdownToPlainText(input);
}

std::wstring DashboardNormalizeEditText(const std::wstring& text) {
    return WideNormalizeEditText(text);
}

std::wstring DashboardNormalizeAndTrimEditText(const std::wstring& text) {
    return WideNormalizeAndTrimEditText(text);
}

std::wstring DashboardExtensionFromPath(const std::wstring& path) {
    return WideExtensionFromPath(path);
}

bool DashboardIsAllowedImageExtension(const std::wstring& path) {
    return WideIsAllowedImageExtension(path);
}

std::wstring DashboardNormalizeAssetExtension(const std::wstring& sourcePath) {
    return WideNormalizeAssetExtension(sourcePath);
}

bool DashboardIsPathStrictlyUnderDirectory(
    const std::wstring& fullPath,
    const std::wstring& fullDir)
{
    return WideIsPathStrictlyUnderDirectory(fullPath, fullDir);
}

bool DashboardIsPathStrictlyUnderDirectoryNoCase(
    const std::wstring& path,
    const std::wstring& dir)
{
    return WideIsPathStrictlyUnderDirectoryNoCase(path, dir);
}

std::wstring DashboardPathCompareKey(std::wstring path) {
    return WidePathCompareKey(std::move(path));
}

std::wstring DashboardPdfPagePauseKey(const std::wstring& jobKey, int pageIndex) {
    return WidePdfPagePauseKey(jobKey, pageIndex);
}

bool DashboardIsRunningBatchStatusToken(const std::wstring& status) {
    return WideIsRunningBatchStatusToken(status);
}

bool DashboardIsTerminalBatchStatusToken(const std::wstring& status) {
    return WideIsTerminalBatchStatusToken(status);
}
