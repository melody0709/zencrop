#pragma once
#include <string>
#include <vector>

// Pure path-extension / import-option helpers used by Dashboard import (Stage 1 D-B).
// No HWND / Window dependency.

bool DashboardIsSupportedImageFile(const std::wstring& filePath);
bool DashboardIsSupportedPdfFile(const std::wstring& filePath);

// Folder import depth clamp (0..64). Matches historical Dashboard constants.
int DashboardNormalizeFolderImportDepth(int depth);
std::vector<std::wstring> DashboardSplitFolderExcludePatterns(const std::wstring& patterns);

// OWN-71 pure path/text helpers (no HWND; hermetic-friendly).
std::wstring DashboardToLowerWide(std::wstring value);
std::wstring DashboardTrimWide(std::wstring value);
bool DashboardIsTiffImageFilePath(const std::wstring& filePath);
bool DashboardIsAllPageRangeText(const std::wstring& pageRange);
// Pure file-name extraction (no shlwapi). Handles both separators.
std::wstring DashboardFileNameFromPath(const std::wstring& path);
// Format elapsed ms as "NNNms" or "N.Ns" (empty for 0). Matches historical FormatElapsedShort.
std::wstring DashboardFormatElapsedShort(unsigned long elapsedMs);
// Sanitize a path segment for preview/cache names (replace illegal chars with '_').
std::wstring DashboardSanitizePathSegment(const std::wstring& input);

// OWN-72 pure path/text + OCR mode helpers (no HWND; hermetic-friendly).
// Trim leading/trailing spaces and dots (preview stem hygiene).
std::wstring DashboardTrimPreviewStem(std::wstring value);
// Sanitize + trim stem; empty → "document"; clamp to 80 chars.
std::wstring DashboardSanitizePreviewPathSegment(const std::wstring& input);
// File name without extension, then SanitizePreviewPathSegment.
std::wstring DashboardPdfPreviewStem(const std::wstring& path);
// Append "_NN" when suffix > 1; otherwise return base unchanged.
std::wstring DashboardAppendPreviewDuplicateSuffix(const std::wstring& base, int suffix);
// Join path segments with '\\' (no filesystem). Empty side returns the other.
std::wstring DashboardJoinPathWide(const std::wstring& left, const std::wstring& right);

// Normalize dashboard OCR mode id to canonical lowercase token.
// Unknown / empty → "local". Known: local | paddle_cloud | paddle_local | ppocrv6_onnx.
std::wstring DashboardNormalizeOcrMode(const std::wstring& mode);
bool DashboardIsCloudOcrMode(const std::wstring& mode);
// Combo index: 0=local, 1=paddle_cloud, 2=paddle_local, 3=ppocrv6_onnx.
int DashboardOcrModeToComboIndex(const std::wstring& mode);
std::wstring DashboardOcrModeFromComboIndex(int index);
// Human label for status/UI (English, stable).
const wchar_t* DashboardOcrModeLabel(const std::wstring& mode);

// Case-insensitive wide equality (pure; no CompareStringOrdinal).
bool DashboardWideEqualsNoCase(const std::wstring& a, const std::wstring& b);
// Pure subset of folder-exclude matching: exact dir-name equality (case-insensitive).
// Glob/PathMatchSpec still live in product (Win32). Returns true on name hit only.
bool DashboardFolderExcludeNameEquals(const std::wstring& pattern, const std::wstring& dirName);

// Normalize newlines to CRLF (pure text hygiene for writer/export).
std::wstring DashboardNormalizeNewlines(std::wstring text);
// Insert suffix before final extension (or append). Pure; no filesystem.
// "a/b/c.json" + ".blocks.json" → "a/b/c.blocks.json"
std::wstring DashboardPathWithSuffix(const std::wstring& path, const std::wstring& suffix);
// Stable batch/image naming tokens (pure; no filesystem).
std::wstring DashboardFormatImageIndexName(int index);   // "image_NNN"
std::wstring DashboardFormatPageIndexName(int pageIndex); // "page_NNNN"

// OWN-73 pure path/text helpers (no HWND; hermetic-friendly).
// Parent directory of path (pure; both separators). Empty / root → empty.
// "C:\\a\\b\\c.txt" → "C:\\a\\b"; "C:\\a" → "C:"; "/a/b" → "/a"; "alone" → "".
std::wstring DashboardParentDirFromPath(const std::wstring& path);
// File stem (name without final extension). Empty leaf → empty (caller may default).
std::wstring DashboardFileStemFromPath(const std::wstring& path);
// True when path has no drive root and no leading slash (pure; no PathIsRelativeW).
// "assets\\x.png" true; "C:\\x" false; "\\server\\x" false; "/tmp/x" false.
bool DashboardIsRelativePathWide(const std::wstring& path);
// Resolve relative path under root; absolute path returned unchanged (pure join).
std::wstring DashboardResolvePathUnderRoot(
    const std::wstring& root,
    const std::wstring& relativeOrAbsolute);
// True when text contains unresolved local OCR asset references (zencrop-asset / loopback).
bool DashboardContainsUnresolvedOcrAssetReference(const std::wstring& text);
// Strip HTML comments, <img>, and markdown images; normalize newlines (writer plain-text).
std::wstring DashboardDeriveCommittedPlainText(const std::wstring& markdown);
// Batch OCR task status parse (case-insensitive). Returns false on unknown.
// Known: pending | recognizing | writing | completed | failed | canceled.
enum class DashboardBatchTaskStatusKind {
    Unknown = 0,
    Pending,
    Recognizing,
    Writing,
    Completed,
    Failed,
    Canceled,
};
DashboardBatchTaskStatusKind DashboardParseBatchTaskStatus(const std::wstring& text);
bool DashboardBatchTaskStatusIsTerminal(DashboardBatchTaskStatusKind kind);
// Format byte size as "N.N MB" / "N MB" (pure; matches product FormatByteSize-ish).
std::wstring DashboardFormatMegabytes(double megabytes, bool oneDecimal);
// Strip trailing path separators except root ("C:\\" stays; "C:\\a\\" → "C:\\a").
std::wstring DashboardTrimTrailingSeparators(std::wstring path);
// Case-insensitive prefix match (pure; no CompareStringOrdinal).
bool DashboardWideStartsWithNoCase(const std::wstring& value, const std::wstring& prefix);
// True when value looks like http:// or https:// URL (case-insensitive).
bool DashboardIsHttpUrlWide(const std::wstring& value);
// Stable page asset stem: "page_NNNN_img_MMM".
std::wstring DashboardFormatPageAssetStem(int pageIndex, int assetIndex);
// Case-insensitive substring search (pure). Empty needle → true.
bool DashboardWideContainsNoCase(const std::wstring& value, const std::wstring& needle);
// True when path looks like a drive-absolute Windows path ("C:\\..." / "c:/...").
bool DashboardIsDriveAbsolutePathWide(const std::wstring& path);
// Strip file extension from a leaf name only (not full path). "a.b.c" → "a.b"; ".hidden" kept.
std::wstring DashboardStripFinalExtension(std::wstring name);
// Join folder + leaf with '/' (POSIX-style relative, pure). Used by PDF page root-relative paths.
std::wstring DashboardJoinPathForwardSlash(const std::wstring& left, const std::wstring& right);

// OWN-75: re-exports of core WideStringUtils helpers for Dashboard product call sites.
std::wstring DashboardTrimTrailingLineBreaks(std::wstring text);
std::wstring DashboardStripMarkdownToPlainText(const std::wstring& input);
std::wstring DashboardNormalizeEditText(const std::wstring& text);
std::wstring DashboardNormalizeAndTrimEditText(const std::wstring& text);
std::wstring DashboardExtensionFromPath(const std::wstring& path);
bool DashboardIsAllowedImageExtension(const std::wstring& path);
std::wstring DashboardNormalizeAssetExtension(const std::wstring& sourcePath);
bool DashboardIsPathStrictlyUnderDirectory(
    const std::wstring& fullPath,
    const std::wstring& fullDir);
bool DashboardIsPathStrictlyUnderDirectoryNoCase(
    const std::wstring& path,
    const std::wstring& dir);
std::wstring DashboardPathCompareKey(std::wstring path);
std::wstring DashboardPdfPagePauseKey(const std::wstring& jobKey, int pageIndex);
bool DashboardIsRunningBatchStatusToken(const std::wstring& status);
bool DashboardIsTerminalBatchStatusToken(const std::wstring& status);
