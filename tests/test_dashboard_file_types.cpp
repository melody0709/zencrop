#include "ocr/ui/dashboard/DashboardFileTypes.h"
#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    Expect(DashboardIsSupportedImageFile(L"a.PNG"), "png");
    Expect(DashboardIsSupportedImageFile(L"x.webp"), "webp");
    Expect(DashboardIsSupportedImageFile(L"x.avif"), "avif");
    Expect(!DashboardIsSupportedImageFile(L"x.txt"), "not image");
    Expect(DashboardIsSupportedPdfFile(L"doc.PDF"), "pdf");
    Expect(!DashboardIsSupportedPdfFile(L"doc.png"), "png not pdf");
    Expect(!DashboardIsSupportedImageFile(L"noext"), "no ext");
    Expect(DashboardNormalizeFolderImportDepth(-3) == 0, "depth clamp low");
    Expect(DashboardNormalizeFolderImportDepth(100) == 64, "depth clamp high");
    Expect(DashboardNormalizeFolderImportDepth(16) == 16, "depth keep");
    auto pats = DashboardSplitFolderExcludePatterns(L" node_modules ; .git,tmp\ncache ");
    Expect(pats.size() == 4, "split patterns count");

    // OWN-71 pure path/text helpers.
    Expect(DashboardToLowerWide(L"AbC") == L"abc", "tolower");
    Expect(DashboardTrimWide(L"  hi  ") == L"hi", "trim");
    Expect(DashboardIsTiffImageFilePath(L"scan.TIF"), "tiff tif");
    Expect(DashboardIsTiffImageFilePath(L"scan.tiff"), "tiff tiff");
    Expect(!DashboardIsTiffImageFilePath(L"scan.png"), "not tiff");
    Expect(DashboardIsAllPageRangeText(L""), "all empty");
    Expect(DashboardIsAllPageRangeText(L"ALL"), "all upper");
    Expect(DashboardIsAllPageRangeText(L" * "), "all star");
    Expect(!DashboardIsAllPageRangeText(L"1-3"), "not all range");
    Expect(DashboardFileNameFromPath(L"C:\\a\\b\\c.png") == L"c.png", "filename win");
    Expect(DashboardFileNameFromPath(L"/a/b/c.png") == L"c.png", "filename posix");
    Expect(DashboardFileNameFromPath(L"alone") == L"alone", "filename alone");
    Expect(DashboardFormatElapsedShort(0).empty(), "elapsed 0 empty");
    Expect(DashboardFormatElapsedShort(500) == L"500ms", "elapsed ms");
    Expect(DashboardFormatElapsedShort(1500) == L"1.5s", "elapsed sec");
    Expect(DashboardSanitizePathSegment(L"a:b/c*") == L"a_b_c_", "sanitize illegal");
    Expect(DashboardSanitizePathSegment(L"ok-name") == L"ok-name", "sanitize keep");

    // OWN-72 pure path/text + OCR mode helpers.
    Expect(DashboardTrimPreviewStem(L"  .hello.  ") == L"hello", "trim preview stem");
    Expect(DashboardTrimPreviewStem(L"...") == L"", "trim all dots");
    Expect(DashboardSanitizePreviewPathSegment(L"a:b/c*") == L"a_b_c_", "sanitize preview");
    Expect(DashboardSanitizePreviewPathSegment(L"  ...  ") == L"document", "sanitize empty → document");
    Expect(DashboardSanitizePreviewPathSegment(std::wstring(100, L'x')).size() == 80,
        "sanitize preview clamp 80");
    Expect(DashboardPdfPreviewStem(L"C:\\docs\\Report.PDF") == L"Report", "pdf stem win");
    Expect(DashboardPdfPreviewStem(L"/tmp/a.b.c.pdf") == L"a.b.c", "pdf stem multi-dot");
    Expect(DashboardPdfPreviewStem(L"noext") == L"noext", "pdf stem noext");
    Expect(DashboardAppendPreviewDuplicateSuffix(L"doc", 1) == L"doc", "suffix 1 keep");
    Expect(DashboardAppendPreviewDuplicateSuffix(L"doc", 0) == L"doc", "suffix 0 keep");
    Expect(DashboardAppendPreviewDuplicateSuffix(L"doc", 2) == L"doc_02", "suffix 2");
    Expect(DashboardAppendPreviewDuplicateSuffix(L"doc", 12) == L"doc_12", "suffix 12");
    Expect(DashboardJoinPathWide(L"", L"b") == L"b", "join empty left");
    Expect(DashboardJoinPathWide(L"a", L"") == L"a", "join empty right");
    Expect(DashboardJoinPathWide(L"a\\", L"b") == L"a\\b", "join trailing slash");
    Expect(DashboardJoinPathWide(L"a/", L"b") == L"a/b", "join trailing fwd");
    Expect(DashboardJoinPathWide(L"a", L"b") == L"a\\b", "join bare");

    Expect(DashboardNormalizeOcrMode(L"PADDLE_CLOUD") == L"paddle_cloud", "norm cloud");
    Expect(DashboardNormalizeOcrMode(L" paddle_local ") == L"paddle_local", "norm local paddle");
    Expect(DashboardNormalizeOcrMode(L"ppocrv6_onnx") == L"ppocrv6_onnx", "norm onnx");
    Expect(DashboardNormalizeOcrMode(L"local") == L"local", "norm local");
    Expect(DashboardNormalizeOcrMode(L"") == L"local", "norm empty → local");
    Expect(DashboardNormalizeOcrMode(L"weird") == L"local", "norm unknown → local");
    Expect(DashboardIsCloudOcrMode(L"paddle_cloud"), "is cloud");
    Expect(DashboardIsCloudOcrMode(L"PADDLE_CLOUD"), "is cloud case");
    Expect(!DashboardIsCloudOcrMode(L"paddle_local"), "paddle local not cloud");
    Expect(!DashboardIsCloudOcrMode(L"local"), "local not cloud");
    Expect(DashboardOcrModeToComboIndex(L"local") == 0, "combo local");
    Expect(DashboardOcrModeToComboIndex(L"paddle_cloud") == 1, "combo cloud");
    Expect(DashboardOcrModeToComboIndex(L"paddle_local") == 2, "combo paddle local");
    Expect(DashboardOcrModeToComboIndex(L"ppocrv6_onnx") == 3, "combo onnx");
    Expect(DashboardOcrModeFromComboIndex(0) == L"local", "from 0");
    Expect(DashboardOcrModeFromComboIndex(1) == L"paddle_cloud", "from 1");
    Expect(DashboardOcrModeFromComboIndex(2) == L"paddle_local", "from 2");
    Expect(DashboardOcrModeFromComboIndex(3) == L"ppocrv6_onnx", "from 3");
    Expect(DashboardOcrModeFromComboIndex(99) == L"local", "from bad");
    Expect(std::wstring(DashboardOcrModeLabel(L"local")) == L"Windows OCR", "label local");
    Expect(std::wstring(DashboardOcrModeLabel(L"paddle_cloud")) == L"PaddleOCR Cloud", "label cloud");
    Expect(std::wstring(DashboardOcrModeLabel(L"paddle_local")) == L"PaddleOCR-VL 1.6 Local", "label paddle");
    Expect(std::wstring(DashboardOcrModeLabel(L"ppocrv6_onnx")) == L"PP-OCRv6 Local", "label onnx");

    // OWN-72 folder-exclude pure name match + wide equals.
    Expect(DashboardWideEqualsNoCase(L"Node_Modules", L"node_modules"), "wide eq case");
    Expect(!DashboardWideEqualsNoCase(L"a", L"ab"), "wide eq len");
    Expect(DashboardFolderExcludeNameEquals(L".git", L".GIT"), "exclude name eq");
    Expect(!DashboardFolderExcludeNameEquals(L"node*", L"node_modules"), "exclude no glob");
    Expect(!DashboardFolderExcludeNameEquals(L"", L"x"), "exclude empty pattern");
    Expect(!DashboardFolderExcludeNameEquals(L"x", L""), "exclude empty name");

    // OWN-72 text/path pure helpers (writer hygiene).
    Expect(DashboardNormalizeNewlines(L"a\nb\r\nc\rd") == L"a\r\nb\r\nc\r\nd", "normalize newlines");
    Expect(DashboardNormalizeNewlines(L"") == L"", "normalize empty");
    Expect(DashboardNormalizeNewlines(L"plain") == L"plain", "normalize plain");
    Expect(DashboardPathWithSuffix(L"a\\b\\c.json", L".blocks.json") == L"a\\b\\c.blocks.json",
        "path suffix win");
    Expect(DashboardPathWithSuffix(L"/a/b/c.json", L".blocks.json") == L"/a/b/c.blocks.json",
        "path suffix posix");
    Expect(DashboardPathWithSuffix(L"noext", L".md") == L"noext.md", "path suffix noext");
    Expect(DashboardPathWithSuffix(L"a.b/c", L".x") == L"a.b/c.x", "path suffix no final ext");
    Expect(DashboardFormatImageIndexName(1) == L"image_001", "image index 1");
    Expect(DashboardFormatImageIndexName(42) == L"image_042", "image index 42");
    Expect(DashboardFormatImageIndexName(1234) == L"image_1234", "image index wide");
    Expect(DashboardFormatPageIndexName(1) == L"page_0001", "page index 1");
    Expect(DashboardFormatPageIndexName(99) == L"page_0099", "page index 99");
    Expect(DashboardFormatPageIndexName(12345) == L"page_12345", "page index wide");

    // OWN-73 pure path/text helpers.
    Expect(DashboardParentDirFromPath(L"C:\\a\\b\\c.txt") == L"C:\\a\\b", "parent win");
    Expect(DashboardParentDirFromPath(L"C:\\a\\b\\") == L"C:\\a", "parent trail sep");
    Expect(DashboardParentDirFromPath(L"C:\\a") == L"C:\\", "parent drive root");
    Expect(DashboardParentDirFromPath(L"/a/b/c") == L"/a/b", "parent posix");
    Expect(DashboardParentDirFromPath(L"/a") == L"/", "parent posix root");
    Expect(DashboardParentDirFromPath(L"alone") == L"", "parent alone");
    Expect(DashboardParentDirFromPath(L"") == L"", "parent empty");
    Expect(DashboardFileStemFromPath(L"C:\\a\\b\\c.txt") == L"c", "stem win");
    Expect(DashboardFileStemFromPath(L"/a/b/c.tar.gz") == L"c.tar", "stem multi-dot");
    Expect(DashboardFileStemFromPath(L"noext") == L"noext", "stem noext");
    Expect(DashboardFileStemFromPath(L"") == L"", "stem empty");
    Expect(DashboardIsRelativePathWide(L"assets\\x.png"), "rel assets");
    Expect(DashboardIsRelativePathWide(L"foo/bar"), "rel foo");
    Expect(DashboardIsRelativePathWide(L""), "rel empty");
    Expect(!DashboardIsRelativePathWide(L"C:\\x"), "abs drive");
    Expect(!DashboardIsRelativePathWide(L"c:/x"), "abs drive slash");
    Expect(!DashboardIsRelativePathWide(L"\\server\\x"), "abs root slash");
    Expect(!DashboardIsRelativePathWide(L"/tmp/x"), "abs posix");
    Expect(DashboardResolvePathUnderRoot(L"C:\\out", L"manifest.json") == L"C:\\out\\manifest.json",
        "resolve rel");
    Expect(DashboardResolvePathUnderRoot(L"C:\\out", L"D:\\abs\\m.json") == L"D:\\abs\\m.json",
        "resolve abs");
    Expect(DashboardResolvePathUnderRoot(L"C:\\out", L"") == L"", "resolve empty");
    Expect(DashboardContainsUnresolvedOcrAssetReference(L"see zencrop-asset://a"), "asset ref");
    Expect(DashboardContainsUnresolvedOcrAssetReference(L"http://127.0.0.1/x"), "loopback");
    Expect(DashboardContainsUnresolvedOcrAssetReference(L"HTTP://LOCALHOST/y"), "localhost case");
    Expect(!DashboardContainsUnresolvedOcrAssetReference(L"plain text"), "no asset ref");
    // comment + markdown image + img tag stripped; surrounding spaces retained
    // ("hi " + space after comment + space after markdown image = "hi   ").
    Expect(DashboardDeriveCommittedPlainText(L"hi <!--c--> ![a](b) <img x>") == L"hi   ",
        "derive plain strip");
    Expect(DashboardDeriveCommittedPlainText(L"a\nb") == L"a\r\nb", "derive plain nl");
    Expect(DashboardParseBatchTaskStatus(L"pending") == DashboardBatchTaskStatusKind::Pending,
        "status pending");
    Expect(DashboardParseBatchTaskStatus(L" RECOGNIZING ") ==
        DashboardBatchTaskStatusKind::Recognizing, "status recognizing");
    Expect(DashboardParseBatchTaskStatus(L"writing") == DashboardBatchTaskStatusKind::Writing,
        "status writing");
    Expect(DashboardParseBatchTaskStatus(L"completed") == DashboardBatchTaskStatusKind::Completed,
        "status completed");
    Expect(DashboardParseBatchTaskStatus(L"failed") == DashboardBatchTaskStatusKind::Failed,
        "status failed");
    Expect(DashboardParseBatchTaskStatus(L"canceled") == DashboardBatchTaskStatusKind::Canceled,
        "status canceled");
    Expect(DashboardParseBatchTaskStatus(L"cancelled") == DashboardBatchTaskStatusKind::Canceled,
        "status cancelled");
    Expect(DashboardParseBatchTaskStatus(L"weird") == DashboardBatchTaskStatusKind::Unknown,
        "status unknown");
    Expect(DashboardBatchTaskStatusIsTerminal(DashboardBatchTaskStatusKind::Completed),
        "terminal completed");
    Expect(DashboardBatchTaskStatusIsTerminal(DashboardBatchTaskStatusKind::Failed),
        "terminal failed");
    Expect(DashboardBatchTaskStatusIsTerminal(DashboardBatchTaskStatusKind::Canceled),
        "terminal canceled");
    Expect(!DashboardBatchTaskStatusIsTerminal(DashboardBatchTaskStatusKind::Pending),
        "pending not terminal");
    Expect(!DashboardBatchTaskStatusIsTerminal(DashboardBatchTaskStatusKind::Recognizing),
        "recognizing not terminal");
    Expect(DashboardFormatMegabytes(1.5, true) == L"1.5 MB", "mb decimal");
    Expect(DashboardFormatMegabytes(12.0, false) == L"12 MB", "mb whole");
    Expect(DashboardTrimTrailingSeparators(L"C:\\a\\b\\") == L"C:\\a\\b", "trim trail");
    Expect(DashboardTrimTrailingSeparators(L"C:\\") == L"C:\\", "trim keep drive");
    Expect(DashboardTrimTrailingSeparators(L"/") == L"/", "trim keep root");
    Expect(DashboardTrimTrailingSeparators(L"a") == L"a", "trim bare");

    // OWN-73 starts-with / URL / page-asset stem pure helpers.
    Expect(DashboardWideStartsWithNoCase(L"HTTP://x", L"http://"), "starts http");
    Expect(DashboardWideStartsWithNoCase(L"Bearer tok", L"bearer "), "starts bearer");
    Expect(!DashboardWideStartsWithNoCase(L"ab", L"abc"), "starts short");
    Expect(DashboardIsHttpUrlWide(L"https://example.com"), "https url");
    Expect(DashboardIsHttpUrlWide(L"HTTP://x"), "http case");
    Expect(!DashboardIsHttpUrlWide(L"ftp://x"), "ftp not http");
    Expect(!DashboardIsHttpUrlWide(L"plain"), "plain not url");
    Expect(DashboardFormatPageAssetStem(1, 2) == L"page_0001_img_002", "asset stem");
    Expect(DashboardFormatPageAssetStem(12, 99) == L"page_0012_img_099", "asset stem wide");

    // OWN-73 more pure path/text helpers.
    Expect(DashboardWideContainsNoCase(L"Hello Zencrop-Asset", L"zencrop-asset"), "contains");
    Expect(DashboardWideContainsNoCase(L"abc", L""), "contains empty needle");
    Expect(!DashboardWideContainsNoCase(L"abc", L"abcd"), "contains miss");
    Expect(DashboardIsDriveAbsolutePathWide(L"C:\\x"), "drive abs C");
    Expect(DashboardIsDriveAbsolutePathWide(L"d:/x"), "drive abs d");
    Expect(!DashboardIsDriveAbsolutePathWide(L"assets\\x"), "not drive abs");
    Expect(!DashboardIsDriveAbsolutePathWide(L""), "empty not drive");
    Expect(DashboardStripFinalExtension(L"a.b.c") == L"a.b", "strip multi");
    Expect(DashboardStripFinalExtension(L".hidden") == L".hidden", "strip keep hidden");
    Expect(DashboardStripFinalExtension(L"noext") == L"noext", "strip noext");
    Expect(DashboardStripFinalExtension(L"") == L"", "strip empty");
    Expect(DashboardJoinPathForwardSlash(L"pages", L"a.md") == L"pages/a.md", "join fwd");
    Expect(DashboardJoinPathForwardSlash(L"pages/", L"a.md") == L"pages/a.md", "join fwd trail");
    Expect(DashboardJoinPathForwardSlash(L"pages\\", L"a.md") == L"pages/a.md", "join fwd backslash");
    Expect(DashboardJoinPathForwardSlash(L"", L"a.md") == L"a.md", "join fwd empty left");
    Expect(DashboardJoinPathForwardSlash(L"pages", L"") == L"pages", "join fwd empty right");

    // OWN-75 FileTypes re-exports of WideStringUtils helpers.
    Expect(DashboardTrimTrailingLineBreaks(L"hi\r\n") == L"hi", "ft trim breaks");
    Expect(DashboardNormalizeEditText(L"a\nb") == L"a\r\nb", "ft norm edit");
    Expect(DashboardNormalizeAndTrimEditText(L"a\nb\r\n") == L"a\r\nb", "ft norm trim");
    Expect(DashboardStripMarkdownToPlainText(L"# T\n**b**") == L"T\r\nb", "ft strip md");
    Expect(DashboardExtensionFromPath(L"x.PNG") == L".PNG", "ft ext");
    Expect(DashboardIsAllowedImageExtension(L"x.webp"), "ft img");
    Expect(!DashboardIsAllowedImageExtension(L"x.txt"), "ft not img");
    Expect(DashboardNormalizeAssetExtension(L"a") == L".png", "ft asset empty");
    Expect(DashboardIsPathStrictlyUnderDirectory(L"c:\\a\\b", L"c:\\a"), "ft under");
    Expect(DashboardIsPathStrictlyUnderDirectoryNoCase(L"C:\\A\\B", L"c:\\a"), "ft under nc");
    Expect(DashboardPathCompareKey(L"C:/A") == L"c:\\a", "ft compare key");
    Expect(DashboardPdfPagePauseKey(L"j", 2) == L"j#page:2", "ft pause");
    Expect(DashboardIsRunningBatchStatusToken(L"recognizing"), "ft running");
    Expect(DashboardIsTerminalBatchStatusToken(L"completed"), "ft terminal");

    if (g_fail) { std::cerr << g_fail << " failures\n"; return 1; }
    std::cout << "ALL PASSED\n";
    return 0;
}
