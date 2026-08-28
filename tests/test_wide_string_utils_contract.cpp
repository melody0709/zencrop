#include "core/WideStringUtils.h"
#include "core/WideMarkdownUtils.h"
#include "core/WideFormatUtils.h"
#include "core/WideJsonUtils.h"
#include "core/NarrowStringUtils.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    // Trim / lower / equals.
    Expect(WideTrim(L"  hi  ") == L"hi", "trim");
    Expect(WideTrim(L"\t\na\r") == L"a", "trim ws");
    Expect(WideToLower(L"AbC") == L"abc", "tolower");
    Expect(WideEqualsNoCase(L"Ab", L"ab"), "eq");
    Expect(!WideEqualsNoCase(L"a", L"ab"), "eq len");

    // Starts / contains / http.
    Expect(WideStartsWithNoCase(L"HTTP://x", L"http://"), "starts http");
    Expect(WideStartsWithNoCase(L"Bearer t", L"bearer "), "starts bearer");
    Expect(!WideStartsWithNoCase(L"ab", L"abc"), "starts short");
    Expect(WideContainsNoCase(L"Hello Zencrop", L"zencrop"), "contains");
    Expect(WideContainsNoCase(L"x", L""), "contains empty");
    Expect(!WideContainsNoCase(L"abc", L"abcd"), "contains miss");
    Expect(WideIsHttpUrl(L"https://a"), "https");
    Expect(WideIsHttpUrl(L"HTTP://a"), "http case");
    Expect(!WideIsHttpUrl(L"ftp://a"), "ftp not http");

    // Escape / unescape JSON.
    Expect(WideEscapeJsonString(L"a\"b\\c\n") == L"a\\\"b\\\\c\\n", "escape");
    Expect(WideUnescapeJsonString(L"a\\\"b\\\\c\\n") == L"a\"b\\c\n", "unescape basic");
    Expect(WideUnescapeJsonString(L"\\/") == L"/", "unescape slash");
    Expect(WideUnescapeJsonString(L"\\u0041") == L"A", "unescape u0041");
    Expect(WideUnescapeJsonString(L"\\t\\r\\b\\f") == L"\t\r\b\f", "unescape controls");

    // Path helpers.
    Expect(WideFileNameFromPath(L"C:\\a\\b\\c.png") == L"c.png", "filename win");
    Expect(WideFileNameFromPath(L"/a/b/c.png") == L"c.png", "filename posix");
    Expect(WideParentDirFromPath(L"C:\\a\\b\\c.txt") == L"C:\\a\\b", "parent win");
    Expect(WideParentDirFromPath(L"C:\\a") == L"C:\\", "parent drive");
    Expect(WideParentDirFromPath(L"/a/b") == L"/a", "parent posix");
    Expect(WideParentDirFromPath(L"alone") == L"", "parent alone");
    Expect(WideJoinPath(L"a", L"b") == L"a\\b", "join");
    Expect(WideJoinPath(L"a\\", L"b") == L"a\\b", "join trail");
    Expect(WideJoinPathForwardSlash(L"pages", L"a.md") == L"pages/a.md", "join fwd");
    Expect(WideJoinPathForwardSlash(L"pages\\", L"a.md") == L"pages/a.md", "join fwd back");
    Expect(WideIsDriveAbsolutePath(L"C:\\x"), "drive abs");
    Expect(!WideIsDriveAbsolutePath(L"assets\\x"), "not drive");
    Expect(WideIsRelativePath(L"assets\\x"), "rel");
    Expect(!WideIsRelativePath(L"C:\\x"), "not rel drive");
    Expect(!WideIsRelativePath(L"/tmp"), "not rel root");
    Expect(WideStripFinalExtension(L"a.b.c") == L"a.b", "strip multi");
    Expect(WideStripFinalExtension(L".hidden") == L".hidden", "strip hidden");
    Expect(WideTrimTrailingSlashes(L"http://x///") == L"http://x", "trim slash");

    // Newlines / plain text / asset ref.
    Expect(WideNormalizeNewlines(L"a\nb\r\nc\rd") == L"a\r\nb\r\nc\r\nd", "nl");
    Expect(WideDeriveCommittedPlainText(L"hi <!--c--> ![a](b) <img x>") == L"hi   ",
        "derive plain");
    Expect(WideContainsUnresolvedOcrAssetReference(L"zencrop-asset://x"), "asset");
    Expect(WideContainsUnresolvedOcrAssetReference(L"HTTP://LOCALHOST/y"), "localhost");
    Expect(!WideContainsUnresolvedOcrAssetReference(L"plain"), "no asset");

    // Markdown image find.
    {
        size_t marker = 0, altClose = 0;
        Expect(WideFindNextMarkdownImage(L"pre ![alt](url) post", 0, marker, altClose),
            "md find");
        Expect(marker == 4, "md marker");
        Expect(altClose == 9, "md altClose");
        Expect(!WideFindNextMarkdownImage(L"no image here", 0, marker, altClose), "md miss");
    }

    // Skip JSON whitespace.
    Expect(WideSkipJsonWhitespace(L"  \t\r\nx", 0) == 5, "skip ws");
    Expect(WideSkipJsonWhitespace(L"x", 0) == 0, "skip none");

    // OWN-74 additional pure helpers.
    Expect(WideToForwardSlashes(L"a\\b\\c") == L"a/b/c", "fwd slash");
    Expect(WideToBackSlashes(L"a/b/c") == L"a\\b\\c", "back slash");
    Expect(WideClampInt(-1, 0, 10) == 0, "clamp lo");
    Expect(WideClampInt(99, 0, 10) == 10, "clamp hi");
    Expect(WideClampInt(5, 0, 10) == 5, "clamp mid");
    Expect(WideStripAuthSchemePrefix(L"Bearer abc") == L"abc", "strip bearer");
    Expect(WideStripAuthSchemePrefix(L"token xyz") == L"xyz", "strip token");
    Expect(WideStripAuthSchemePrefix(L"  raw  ") == L"raw", "strip none");
    Expect(WideBuildBearerAuthorizationHeader(L"abc") == L"Authorization: bearer abc",
        "auth bare");
    Expect(WideBuildBearerAuthorizationHeader(L"Bearer abc") == L"Authorization: Bearer abc",
        "auth keep bearer");
    Expect(WideBuildBearerAuthorizationHeader(L"Token xyz") == L"Authorization: bearer xyz",
        "auth token rewrite");
    Expect(WideStartsWithNoCaseAt(L"xxHTTPyy", 2, L"http"), "starts at");
    Expect(!WideStartsWithNoCaseAt(L"http", 1, L"http"), "starts at miss");
    Expect(WidePathWithSuffix(L"a\\b\\c.json", L".blocks.json") == L"a\\b\\c.blocks.json",
        "path suffix");
    Expect(WideResolvePathUnderRoot(L"C:\\out", L"m.json") == L"C:\\out\\m.json",
        "resolve rel");
    Expect(WideResolvePathUnderRoot(L"C:\\out", L"D:\\abs\\m.json") == L"D:\\abs\\m.json",
        "resolve abs");

    // OWN-75 pure text/path helpers (History/ImageLinks/OcrUtils).
    Expect(WideTrimTrailingLineBreaks(L"hi\r\n\r\n") == L"hi", "trim trail breaks");
    Expect(WideTrimTrailingLineBreaks(L"hi") == L"hi", "trim trail none");
    Expect(WideIsFormatMarker(L'*'), "fmt star");
    Expect(WideIsFormatMarker(L'`'), "fmt tick");
    Expect(!WideIsFormatMarker(L'a'), "fmt letter");
    Expect(WideIsUrlTerminator(L')'), "url term paren");
    Expect(WideIsUrlTerminator(L' '), "url term space");
    Expect(!WideIsUrlTerminator(L'x'), "url term letter");
    Expect(WideExtensionFromPath(L"C:\\a\\b.PNG") == L".PNG", "ext win");
    Expect(WideExtensionFromPath(L"/a/b/c.jpeg") == L".jpeg", "ext posix");
    Expect(WideExtensionFromPath(L"noext") == L"", "ext none");
    Expect(WideExtensionFromPath(L".hidden") == L".hidden", "ext hidden");
    Expect(WideIsAllowedImageExtension(L"x.PNG"), "img png");
    Expect(WideIsAllowedImageExtension(L"x.webp"), "img webp");
    Expect(!WideIsAllowedImageExtension(L"x.txt"), "img not");
    Expect(WideNormalizeAssetExtension(L"a.JPG") == L".jpg", "asset jpg");
    Expect(WideNormalizeAssetExtension(L"a") == L".png", "asset empty → png");
    Expect(WideNormalizeAssetExtension(L"a.txt") == L".png", "asset unknown → png");
    Expect(WideIsHtmlBlockTagName(L"br"), "html br");
    Expect(WideIsHtmlBlockTagName(L"div"), "html div");
    Expect(!WideIsHtmlBlockTagName(L"span"), "html span");
    {
        std::wstring out;
        WideAppendPlainLineBreak(out);
        Expect(out.empty(), "append break empty");
        out = L"a";
        WideAppendPlainLineBreak(out);
        Expect(out == L"a\r\n", "append break once");
        WideAppendPlainLineBreak(out);
        Expect(out == L"a\r\n", "append break idempotent");
    }
    Expect(WideStripMarkdownToPlainText(L"# Title\n- item\n**bold**") ==
        L"Title\r\n- item\r\nbold", "strip md");
    Expect(WideStripMarkdownToPlainText(L"![alt](u) [t](u)") == L"alt t", "strip md links");
    Expect(WideNormalizeEditText(L"a\nb\rc") == L"a\r\nb\r\nc", "norm edit");
    Expect(WideNormalizeAndTrimEditText(L"a\nb\r\n") == L"a\r\nb", "norm trim edit");

    // OWN-75 path-under / status token / pause key pure helpers.
    Expect(WideEnsureTrailingBackslash(L"C:\\a") == L"C:\\a\\", "ensure slash");
    Expect(WideEnsureTrailingBackslash(L"C:\\a\\") == L"C:\\a\\", "ensure slash keep");
    Expect(WideEnsureTrailingBackslash(L"") == L"", "ensure slash empty");
    Expect(WideIsPathStrictlyUnderDirectory(L"c:\\a\\b\\c.png", L"c:\\a"), "under dir");
    Expect(WideIsPathStrictlyUnderDirectory(L"c:\\a\\b\\c.png", L"c:\\a\\"), "under dir slash");
    Expect(!WideIsPathStrictlyUnderDirectory(L"c:\\a", L"c:\\a"), "not under self");
    Expect(!WideIsPathStrictlyUnderDirectory(L"c:\\z\\x", L"c:\\a"), "not under other");
    Expect(WideIsPathStrictlyUnderDirectoryNoCase(L"C:\\A\\B", L"c:\\a"), "under nocase");
    Expect(WidePathCompareKey(L"C:/A/B") == L"c:\\a\\b", "compare key");
    Expect(WidePathHasExtensionNoCase(L"x.PNG", {L".png", L".jpg"}), "has ext");
    Expect(!WidePathHasExtensionNoCase(L"x.txt", {L".png", L".jpg"}), "no ext");
    Expect(WidePdfPagePauseKey(L"job1", 3) == L"job1#page:3", "pause key");
    Expect(WidePdfPagePauseKey(L"", 3) == L"", "pause empty job");
    Expect(WidePdfPagePauseKey(L"job1", 0) == L"", "pause bad page");
    Expect(WideIsRunningBatchStatusToken(L"Recognizing"), "running recognizing");
    Expect(WideIsRunningBatchStatusToken(L" writing "), "running writing");
    Expect(!WideIsRunningBatchStatusToken(L"completed"), "completed not running");
    Expect(WideIsTerminalBatchStatusToken(L"completed"), "term completed");
    Expect(WideIsTerminalBatchStatusToken(L"FAILED"), "term failed");
    Expect(WideIsTerminalBatchStatusToken(L"cancelled"), "term cancelled");
    Expect(!WideIsTerminalBatchStatusToken(L"pending"), "pending not term");

    // OWN-76 pure color / JSON / cycle helpers.
    Expect(WideHexDigitValue(L'A') == 10, "hex A");
    Expect(WideHexDigitValue(L'f') == 15, "hex f");
    Expect(WideHexDigitValue(L'0') == 0, "hex 0");
    Expect(WideHexDigitValue(L'g') == -1, "hex bad");
    Expect(WidePackRgb(0x12, 0x34, 0x56) == 0x00563412u, "pack rgb");
    Expect(WideUnpackR(0x00563412u) == 0x12u, "unpack r");
    Expect(WideUnpackG(0x00563412u) == 0x34u, "unpack g");
    Expect(WideUnpackB(0x00563412u) == 0x56u, "unpack b");
    Expect(WideParseColorHex(L"#FF00AA") == WidePackRgb(255, 0, 170), "parse hex");
    Expect(WideParseColorHex(L"bad") == WidePackRgb(255, 0, 0), "parse hex fallback");
    Expect(WideColorToHex(WidePackRgb(255, 0, 170)) == L"#FF00AA", "color to hex");
    Expect(WideCycleIntInclusive(5, 1, 5, 1) == 1, "cycle wrap");
    Expect(WideCycleIntInclusive(1, 1, 5, 1) == 2, "cycle next");
    Expect(WideCycleIntInclusive(3, 1, 5, -1) == 2, "cycle prev");
    Expect(WideCycleLineStyle(5) == 1, "line style wrap");
    Expect(WideCycleLineStyle(1) == 2, "line style next");
    Expect(WideCycleBinaryMode(0) == 1, "binary 0→1");
    Expect(WideCycleBinaryMode(1) == 0, "binary 1→0");
    Expect(WideCycleSerialType(4) == 0, "serial type wrap");
    Expect(WideCycleSerialType(0) == 1, "serial type next");
    Expect(WideAdjustSerialCounter(1, -1) == 1, "serial floor");
    Expect(WideAdjustSerialCounter(5, 1) == 6, "serial ++");
    Expect(WideAdjustSerialCounter(5, -2) == 3, "serial --");
    Expect(WideHasJsonKey(L"{\"a\":1}", L"a"), "has key");
    Expect(!WideHasJsonKey(L"{\"a\":1}", L"b"), "no key");
    Expect(WideSkipJsonString(L"\"hi\"x", 0) == 4, "skip string");
    Expect(WideSkipJsonWhitespaceBom(L"\xFEFF  x", 0) == 3, "skip bom ws");
    Expect(WideExtractJsonField(L"{\"name\":\"bob\"}", L"name") == L"bob", "extract str");
    Expect(WideExtractJsonField(L"{\"n\":42}", L"n") == L"42", "extract num");
    Expect(WideExtractJsonField(L"{\"a\":[1,2]}", L"a") == L"[1,2]", "extract arr");
    Expect(WideExtractJsonField(L"{}", L"x") == L"", "extract miss");
    Expect(WideParseJsonBoolToken(L"true"), "bool true");
    Expect(WideParseJsonBoolToken(L"FALSE") == false, "bool false");
    Expect(WideParseJsonBoolToken(L"1"), "bool 1");
    Expect(WideParseJsonBoolToken(L"x", true), "bool fallback");
    Expect(WideParseJsonIntToken(L"42") == 42, "int 42");
    Expect(WideParseJsonIntToken(L"-7") == -7, "int -7");
    Expect(WideParseJsonIntToken(L"") == 0, "int empty");
    Expect(WideParseJsonIntToken(L"x", 9) == 9, "int fallback");
    Expect(WideIsPaddleOcrJobsUrlPath(L"https://x/api/v2/ocr/jobs"), "paddle jobs");
    Expect(!WideIsPaddleOcrJobsUrlPath(L"https://x/other"), "not paddle jobs");

    // OWN-77 pure JSON structural helpers (OcrBlockJson / Settings thin-wrap).
    Expect(WideJsonIndent(0) == L"", "indent 0");
    Expect(WideJsonIndent(2) == L"  ", "indent 2");
    Expect(WideJsonIndent(4) == L"    ", "indent 4");
    {
        const std::wstring nested = L"{\"a\":[1,{\"b\":2}],\"c\":3}";
        Expect(WideJsonFindMatching(nested, 0, L'{', L'}') == nested.size() - 1,
            "find match root");
        Expect(WideJsonFindMatching(nested, 5, L'[', L']') == 15, "find match arr");
        Expect(WideJsonFindMatching(L"{", 0, L'{', L'}') == std::wstring::npos,
            "find match open");
    }
    {
        const std::wstring obj = L"{\"id\":\"x\",\"n\":42,\"ok\":true,\"arr\":[1,2],\"o\":{\"k\":1}}";
        Expect(WideJsonFindField(obj, L"id") != std::wstring::npos, "find field id");
        Expect(WideJsonFindField(obj, L"missing") == std::wstring::npos, "find field miss");
        Expect(WideJsonExtractValue(obj, L"id") == L"x", "extract id");
        Expect(WideJsonExtractValue(obj, L"n") == L"42", "extract n");
        Expect(WideJsonExtractValue(obj, L"ok") == L"true", "extract ok");
        Expect(WideJsonExtractValue(obj, L"arr") == L"[1,2]", "extract arr");
        Expect(WideJsonExtractValue(obj, L"o") == L"{\"k\":1}", "extract obj");
        Expect(WideJsonExtractValue(obj, L"missing") == L"", "extract miss");
    }
    {
        const std::wstring json =
            L"{\"outer\":1,\"blocks\":[{\"x\":1},{\"x\":2}],\"tail\":true}";
        Expect(WideJsonFindTopLevelValue(json, L"outer") == L"1", "top outer");
        Expect(WideJsonFindTopLevelValue(json, L"blocks") == L"[{\"x\":1},{\"x\":2}]",
            "top blocks");
        Expect(WideJsonFindTopLevelValue(json, L"tail") == L"true", "top tail");
        Expect(WideJsonFindTopLevelValue(json, L"missing") == L"", "top miss");
    }
    {
        auto items = WideJsonObjectArrayItems(L"[{\"a\":1},{\"b\":2},{\"c\":3}]");
        Expect(items.size() == 3, "arr items size");
        Expect(items[0] == L"{\"a\":1}", "arr item0");
        Expect(items[1] == L"{\"b\":2}", "arr item1");
        Expect(items[2] == L"{\"c\":3}", "arr item2");
        Expect(WideJsonObjectArrayItems(L"[]").empty(), "arr empty");
        Expect(WideJsonObjectArrayItems(L"").empty(), "arr blank");
    }
    Expect(WideJsonParseIntOrNull(L"12", 0) == 12, "int or null 12");
    Expect(WideJsonParseIntOrNull(L"null", 7) == 7, "int or null null");
    Expect(WideJsonParseIntOrNull(L"", 3) == 3, "int or null empty");
    Expect(WideJsonParseBoolOrNull(L"true", false), "bool or null true");
    Expect(WideJsonParseBoolOrNull(L"null", true), "bool or null null");
    Expect(WideJsonParseBoolOrNull(L"false", true) == false, "bool or null false");
    Expect(WideIsJsonNullToken(L"null"), "is null");
    Expect(WideIsJsonNullToken(L" NULL "), "is null pad");
    Expect(!WideIsJsonNullToken(L"0"), "0 not null");
    Expect(WideParseClampedIntToken(L"5", 0, 0, 10) == 5, "clamp int mid");
    Expect(WideParseClampedIntToken(L"99", 0, 0, 10) == 10, "clamp int hi");
    Expect(WideParseClampedIntToken(L"-1", 0, 0, 10) == 0, "clamp int lo");
    Expect(WideParseClampedIntToken(L"null", 3, 0, 10) == 3, "clamp int null");
    Expect(WideParseClampedIntToken(L"", 4, 0, 10) == 4, "clamp int empty");

    // OWN-78 pure strict int / label / BOM-trim helpers.
    {
        int out = 0;
        Expect(WideTryParseJsonIntToken(L"42", out) && out == 42, "try int 42");
        Expect(WideTryParseJsonIntToken(L"-7", out) && out == -7, "try int -7");
        Expect(!WideTryParseJsonIntToken(L"12x", out), "try int trailing junk");
        Expect(!WideTryParseJsonIntToken(L"", out), "try int empty");
        Expect(!WideTryParseJsonIntToken(L"null", out), "try int null");
        Expect(!WideTryParseJsonIntToken(L"  ", out), "try int ws");
        long long out64 = 0;
        Expect(WideTryParseJsonInt64Token(L"9223372036854775807", out64) &&
            out64 == 9223372036854775807LL, "try int64 max");
        Expect(WideTryParseJsonInt64Token(L"-100", out64) && out64 == -100, "try int64 -100");
        Expect(!WideTryParseJsonInt64Token(L"1a", out64), "try int64 junk");
    }
    Expect(WideNormalizeLabelToken(L"Header-Image") == L"header_image", "norm label dash");
    Expect(WideNormalizeLabelToken(L"Page Number") == L"page_number", "norm label space");
    Expect(WideNormalizeLabelToken(L"TEXT") == L"text", "norm label lower");
    // BOM + whitespace trim.
    {
        std::wstring bom = L"\xFEFF  hi  ";
        Expect(WideTrim(std::move(bom)) == L"hi", "trim bom");
    }

    // OWN-80 pure color try-parse (optional '#', trim).
    {
        unsigned int packed = 0;
        Expect(WideTryParseColorHex(L"#FF00AA", packed) &&
            packed == WidePackRgb(255, 0, 170), "try color #");
        Expect(WideTryParseColorHex(L"FF00AA", packed) &&
            packed == WidePackRgb(255, 0, 170), "try color bare");
        Expect(WideTryParseColorHex(L"  #00ff00  ", packed) &&
            packed == WidePackRgb(0, 255, 0), "try color pad");
        Expect(!WideTryParseColorHex(L"bad", packed), "try color bad");
        Expect(!WideTryParseColorHex(L"", packed), "try color empty");
        Expect(WideParseColorHex(L"112233") == WidePackRgb(0x11, 0x22, 0x33),
            "parse bare hex");
    }

    // OWN-81 pure JSON bool literal for save paths.
    Expect(std::wstring(WideJsonBoolLiteral(true)) == L"true", "bool lit true");
    Expect(std::wstring(WideJsonBoolLiteral(false)) == L"false", "bool lit false");

    // OWN-97: CSV int5 + datetime parts pure parsers.
    {
        int x = 0, y = 0, w = 0, h = 0, mx = 0;
        Expect(WideTryParseCsvInt5(L"10,20,800,600,1", x, y, w, h, mx), "csv5 ok");
        Expect(x == 10 && y == 20 && w == 800 && h == 600 && mx == 1, "csv5 vals");
        Expect(!WideTryParseCsvInt5(L"10,20,800", x, y, w, h, mx), "csv5 short fail");
        Expect(!WideTryParseCsvInt5(L"10,20,800,600,1,9", x, y, w, h, mx), "csv5 long fail");
        Expect(!WideTryParseCsvInt5(L"", x, y, w, h, mx), "csv5 empty fail");
        int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, ms = 0;
        int n = WideTryParseDateTimeParts(
            L"2026-07-20 12:34:56.789", year, month, day, hour, minute, second, ms);
        Expect(n == 7, "dt full count");
        Expect(year == 2026 && month == 7 && day == 20 && hour == 12 && minute == 34 &&
                second == 56 && ms == 789,
            "dt full vals");
        n = WideTryParseDateTimeParts(
            L"2026-07-20 12:34", year, month, day, hour, minute, second, ms);
        Expect(n == 5, "dt min count");
        Expect(year == 2026 && hour == 12 && minute == 34, "dt min vals");
        Expect(WideTryParseDateTimeParts(L"bad", year, month, day, hour, minute, second, ms) == 0,
            "dt bad");
    }


    // OWN-102: case-sensitive WideEquals (wcscmp replacement).
    Expect(WideEquals(L"Ab", L"Ab"), "eq case");
    Expect(!WideEquals(L"Ab", L"ab"), "eq case sensitive");
    Expect(WideEquals(std::wstring(L"x"), L"x"), "eq wstr cstr");
    Expect(WideEquals(L"x", std::wstring(L"x")), "eq cstr wstr");
    Expect(!WideEquals(static_cast<const wchar_t*>(nullptr), L"x"), "eq null cstr");
    Expect(WideEquals(static_cast<const wchar_t*>(nullptr), static_cast<const wchar_t*>(nullptr)), "eq null null");


    // OWN-108: WideExeDirFromModulePath pure parent extract.
    Expect(WideExeDirFromModulePath(L"C:\\app\\ZenCrop.exe") == L"C:\\app", "exe dir from module");
    Expect(WideExeDirFromModulePath(std::wstring(L"D:\\build\\bin\\app.exe")) == L"D:\\build\\bin", "exe dir wstring");
    Expect(WideExeDirFromModulePath(L"file.exe") == L"", "exe dir bare name");


    // OWN-109: WideFormatDateTimeParts / WideFormatOcrCropFileName pure formatters.
    Expect(WideFormatDateTimeParts(2026, 7, 20, 15, 4, 5) == L"2026-07-20 15:04:05", "datetime format");
    Expect(WideFormatOcrCropFileName(15, 4, 5, 123) == L"ocr_crop_150405_123.png", "ocr crop name");


    // OWN-111: WideFormatDateParts / UI labels / compact stamp pure formatters.
    Expect(WideFormatDateParts(2026, 7, 21) == L"2026-07-21", "date parts");
    Expect(WideFormatPercentLabel(42) == L"42%", "percent label");
    Expect(WideFormatPxLabel(8) == L"8 px", "px label");
    Expect(WideFormatIntLabel(7) == L"7", "int label");
    Expect(WideFormatCompactStamp(2026, 7, 21, 15, 4, 5) == L".bad.20260721-150405", "compact stamp");


    // OWN-112: WideFormatPad2/Pad4 / point label / hex lower pure formatters.
    Expect(WideFormatPad2(7) == L"07", "pad2");
    Expect(WideFormatPad4(2026) == L"2026", "pad4");
    Expect(WideFormatPointLabel(10, 20) == L"(10, 20)", "point label");
    Expect(WideColorToHexLower(WidePackRgb(0xAB, 0xCD, 0xEF)) == L"#abcdef", "hex lower");

    // OWN-113: pad3 / mm:ss / page-image stems / hex / elapsed / temp names.
    Expect(WideFormatPad3(7) == L"007", "pad3");
    Expect(WideFormatMmSs(1, 5) == L"01:05", "mmss");
    Expect(WideFormatOcrElapsedLabel(2, 3) == L"OCR 02:03", "ocr elapsed");
    Expect(WideFormatElapsedMs(42) == L"42ms", "elapsed ms");
    Expect(WideFormatSeconds1(1.5) == L"1.5s", "seconds1");
    Expect(WideFormatPageIndexName(12) == L"page_0012", "page index");
    Expect(WideFormatImageIndexName(3) == L"image_003", "image index");
    Expect(WideFormatPageAssetStem(4, 5) == L"page_0004_img_005", "page asset stem");
    Expect(WideFormatPageAssetPrefix(4) == L"page_0004_img_", "page asset prefix");
    Expect(WideFormatDupSuffix02(3) == L"_03", "dup suffix");
    Expect(WideFormatHexByte02(0xAB) == L"0xAB", "hex byte");
    Expect(WideFormatHexU32(0x1234u) == L"0x00001234", "hex u32");
    Expect(WideFormatUrlPercentByte(0x20) == L"%20", "url percent");
    Expect(WideFormatUPlusCodepoint(0x4E00) == L"U+4E00", "u plus");
    Expect(WideFormatHash016(0xFFull) == L"00000000000000ff", "hash016");
    Expect(WideFormatMagnifierScale(1.5) == L"1.5x", "mag scale");
    Expect(WideFormatYmdCompact(2026, 7, 20) == L"20260720", "ymd compact");
    Expect(WideFormatHmsCompact(15, 4, 5) == L"150405", "hms compact");
    Expect(WideFormatMs3(7) == L"007", "ms3");
    Expect(WideFormatClipTempName(2026, 7, 20, 15, 4, 5, 123, 99ul, 1, L".png")
        == L"ZenCrop_clip_20260720_150405_123_99_01.png", "clip temp");
    Expect(WideFormatCodecTempName(2026, 7, 20, 15, 4, 5, 123, 99ul, 7u, 2, L".tmp")
        == L"ZenCrop_codec_20260720_150405_123_99_7_02.tmp", "codec temp");
    Expect(WideFormatOcrDropFileName(2026, 7, 20, 15, 4, 5, 123)
        == L"ocr_drop_20260720_150405_123.png", "ocr drop");
    Expect(WideFormatMegabytes1(1.5) == L"1.5 MB", "mb1");
    Expect(WideFormatMegabytes0(2.0) == L"2 MB", "mb0");

    // OWN-114: hex/float/CSV/zoom/temp/contains pure formatters.
    Expect(WideFormatHexLower02(0xAB) == L"ab", "hex lower02");
    Expect(WideFormatHexUpper02(0xAB) == L"AB", "hex upper02");
    Expect(WideFormatHexLower(255) == L"ff", "hex lower");
    Expect(WideFormatHexUpper(255) == L"FF", "hex upper");
    Expect(WideFormatFloat1(1.5) == L"1.5", "float1");
    Expect(WideFormatFloat2(1.5) == L"1.50", "float2");
    Expect(WideFormatFloat6(0.5) == L"0.500000", "float6");
    Expect(WideFormatZoomPercent0(150.0) == L"150%", "zoom pct");
    Expect(WideFormatElapsedParenSeconds1(1.5) == L"  (1.5s)", "elapsed paren");
    Expect(WideFormatCsvInt5(1, 2, 3, 4, 5) == L"1,2,3,4,5", "csv5 fmt");
    Expect(WideFormatUnsigned(42u) == L"42", "unsigned");
    Expect(WideFormatHotkeyJson(L"true", L"false", L"false", L"false", 65)
        == L"{\"win\": true, \"ctrl\": false, \"shift\": false, \"alt\": false, \"key\": 65}",
        "hotkey json");
    Expect(WideFormatDateTimeMinuteParts(2026, 7, 20, 15, 4) == L"2026-07-20 15:04",
        "dt minute");
    Expect(WideFormatTiffPagePrefix(3) == L"tiff_p0003", "tiff prefix");
    Expect(WideFormatPdfPreviewDirName(1ul, 2ull) == L"zencrop_pdf_preview_1_2", "pdf preview dir");
    Expect(WideFormatMpEstimate(1.5, 0.5) == L"1.5 MP total, 0.5 MP max/page", "mp estimate");
    Expect(WideFormatOcrVirtualStem(15, 4, 5, 123, 7) == L"ocr_virtual_150405_123_007",
        "ocr virtual stem");
    Expect(WideFormatTimedSeqPng(L"item", 15, 4, 5, 123, 7) == L"item_150405_123_007.png",
        "timed seq png");
    Expect(WideFormatPdfTempName(9ul, 8ull, 3) == L"pdf_9_8_003.pdf", "pdf temp");
    Expect(WideContains(L"ZenCrop.Main", L"ZenCrop."), "contains yes");
    Expect(!WideContains(L"Other", L"ZenCrop."), "contains no");
    Expect(WideContains(static_cast<const wchar_t*>(nullptr), L"") == true, "contains empty needle");
    Expect(!WideContains(static_cast<const wchar_t*>(nullptr), L"x"), "contains null hay");
    Expect(WideFormatIsoUtcTimestamp(2026, 7, 20, 15, 4, 5, 123)
        == L"2026-07-20T15:04:05.123Z", "iso utc");
    Expect(WideFormatHttpStatusRejected(401)
        == L"Endpoint is reachable, but the token was rejected. (HTTP 401)", "http rejected");
    Expect(WideFormatServerRunningOnPort(8080)
        == L"Server is already running on port 8080.", "server running");
    Expect(WideFormatServerStartedOnPort(8080)
        == L"Server started successfully on port 8080.\nIt is now ready for OCR.", "server started");
    Expect(WideFormatServerStartFailedOnPort(8080)
        == L"Failed to start llama-server on port 8080.\n\n", "server failed");
    Expect(WideFormatPathTableLoadedCount(12)
        == L"[ToolbarIconRenderer] Loaded PATH_TABLE.tsv: 12 entries\n", "path table count");
    Expect(WideFormatMissingCodepoint(0xE001)
        == L"[ToolbarIconRenderer] Missing codepoint 0xE001\n", "missing cp");
    Expect(WideFormatGeneralSettingsJson(L"zh", L"true")
        == L"  \"general\": {\n    \"language\": \"zh\",\n    \"showTitlebar\": true\n  }",
        "general json");
    Expect(WideFormatOverlaySettingsJson(L"#FF0000", 2, L"false")
        == L"  \"overlay\": {\n    \"color\": \"#FF0000\",\n    \"thickness\": 2,\n    \"cropOnTop\": false\n  }",
        "overlay json");
    Expect(WideFormatAotSettingsJson(L"true", L"false", L"#00FF00", 80, 3, L"true", 2)
        == L"  \"alwaysOnTop\": {\n    \"showBorder\": true,\n    \"customColor\": false,\n"
           L"    \"color\": \"#00FF00\",\n    \"opacity\": 80,\n    \"thickness\": 3,\n"
           L"    \"roundedCorners\": true,\n    \"inset\": 2\n  }",
        "aot json");
    Expect(WideFormatHttpJobsEndpointReachable(200)
        == L"Official async jobs endpoint is reachable. (HTTP 200)\n\n"
           L"This test does not submit an OCR job; actual OCR will upload an image and poll by jobId.",
        "jobs reachable");
    Expect(WideFormatCropLabel(L"%d,%d %dx%d", 10, 20, 100, 50) == L"10,20 100x50", "crop label");

    // OWN-115: JSON field builders + LayoutEngine debug pure formatters.
    Expect(WideJsonFieldString(L"language", L"zh") == L"    \"language\": \"zh\"", "json field str");
    Expect(WideJsonFieldInt(L"timeoutMs", 1500) == L"    \"timeoutMs\": 1500", "json field int");
    Expect(WideJsonFieldUnsigned(L"edge", 1024u) == L"    \"edge\": 1024", "json field u");
    Expect(WideJsonFieldBool(L"resultOnTop", true) == L"    \"resultOnTop\": true", "json field bool t");
    Expect(WideJsonFieldBool(L"resultOnTop", false) == L"    \"resultOnTop\": false", "json field bool f");
    Expect(WideJsonFieldBoolLit(L"flag", L"true") == L"    \"flag\": true", "json field bool lit");
    Expect(WideJsonFieldStringLiteral(L"ppocrv6Provider", L"cpu")
        == L"    \"ppocrv6Provider\": \"cpu\"", "json field str lit");
    {
        const std::wstring fields[] = {
            WideJsonFieldString(L"a", L"x"),
            WideJsonFieldInt(L"b", 1),
            WideJsonFieldBool(L"c", false),
        };
        const std::wstring section = WideJsonObjectSection(
            L"ocr", fields, sizeof(fields) / sizeof(fields[0]));
        Expect(section.find(L"\"ocr\": {") != std::wstring::npos, "json section name");
        Expect(section.find(L"\"a\": \"x\"") != std::wstring::npos, "json section a");
        Expect(section.find(L"\"b\": 1") != std::wstring::npos, "json section b");
        Expect(section.find(L"\"c\": false") != std::wstring::npos, "json section c");
        Expect(section.find(L"  }") != std::wstring::npos, "json section close");
    }
    Expect(WideFormatLayoutEngineDebug(L"PP-DocLayoutV3", L"default", 0.50, 0.40)
        == L"[LayoutEngine] family=PP-DocLayoutV3 thresholdProfile=default text=0.50 table=0.40\n",
        "layout engine debug");
    Expect(WideFormatLayoutEngineDebug(nullptr, nullptr, 0.0, 0.0)
        == L"[LayoutEngine] family= thresholdProfile= text=0.00 table=0.00\n",
        "layout engine debug null");

    // OWN-116: pure narrow debug formatters (NarrowStringUtils).
    Expect(NarrowFormatLayoutCreateSessionFailed("boom")
        == "[LayoutEngine] CreateSession failed: boom\n",
        "narrow layout create fail");
    Expect(NarrowFormatLayoutCreateSessionFailed(nullptr)
        == "[LayoutEngine] CreateSession failed: unknown\n",
        "narrow layout create fail null");
    Expect(NarrowFormatLayoutOnnxLoaded(L"PP-DocLayoutV3", 1, 3)
        == "[LayoutEngine] ONNX loaded: family=PP-DocLayoutV3 inputs=1 outputs=3\n",
        "narrow layout onnx");
    Expect(NarrowFormatLayoutInput(0, "image")
        == "[LayoutEngine]   Input 0: image\n",
        "narrow layout input");
    Expect(NarrowFormatLayoutOutput(1, "boxes", 1, 3)
        == "[LayoutEngine]   Output 1: boxes type=1 rank=3\n",
        "narrow layout output");
    Expect(NarrowFormatLayoutTileReconciliation(10, 8)
        == "[LayoutEngine] Tile reconciliation: 10 -> 8 regions\n",
        "narrow tile recon");
    Expect(NarrowFormatLayoutTileFusion(5, 7, 3, 8)
        == "[LayoutEngine] Tile fusion: full=5 tile=7 accepted=3 final=8\n",
        "narrow tile fusion");
    Expect(NarrowFormatLayoutAfterDedup(4)
        == "[LayoutEngine] After dedup: 4 regions\n",
        "narrow after dedup");
    Expect(NarrowFormatLayoutDetectDone(12)
        == "[LayoutEngine] PP-DocLayoutV3 detect done: 12 regions\n",
        "narrow detect done");
    Expect(NarrowFormatLayoutTiledRaw(20, 4)
        == "[LayoutEngine] Tiled raw regions: 20 from 4 tiles\n",
        "narrow tiled raw");
    Expect(NarrowFormatLayoutFullStats(100, 200, 0.500f, 1.0000f, 0.5000f, 9)
        == "[LayoutEngine] Full stats: size=100x200 aspect=0.500 scaleH=1.0000 scaleW=0.5000 regions=9\n",
        "narrow full stats");
    Expect(NarrowFormatLayoutTiledStats(5, 3)
        == "[LayoutEngine] Tiled stats: full=5 tile=3\n",
        "narrow tiled stats");
    Expect(std::string(NarrowLayoutFamilyChangedWarning()).find("WARNING")
        != std::string::npos, "narrow family warn");
    Expect(NarrowFormatLlamaCreateJobFailed(5)
        == "[LlamaServer] CreateJobObject failed: 5\n",
        "narrow llama create job");
    Expect(NarrowFormatLlamaSetJobInfoFailed(6)
        == "[LlamaServer] SetInformationJobObject failed: 6\n",
        "narrow llama set job");
    Expect(NarrowFormatLlamaCreateProcessFailed(7)
        == "[LlamaServer] CreateProcess failed: 7\n",
        "narrow llama create proc");
    Expect(NarrowFormatLlamaAssignJobFailed(8)
        == "[LlamaServer] AssignProcessToJobObject failed: 8\n",
        "narrow llama assign job");
    Expect(NarrowFormatLlamaServerReady(8080)
        == "[LlamaServer] Server ready on port 8080\n",
        "narrow llama ready");
    Expect(NarrowFormatMiniHttpStarted(9090)
        == "[MiniHttp] Started on port 9090\n",
        "narrow minihttp");
    Expect(NarrowFormatHotkeyRegisterFailed(3)
        == "[Hotkey] Failed to register hotkey id=3\n",
        "narrow hotkey");
    Expect(NarrowFormatOcrResultReceived(1, 12, 0)
        == "[OCR] Result received: success=1, textLen=12, errLen=0\n",
        "narrow ocr result");
    Expect(NarrowFormatHttpCrackUrlFailed(1)
        == "[HTTP] WinHttpCrackUrl failed: 1\n",
        "narrow http crack");
    Expect(NarrowFormatHttpHostPath(L"h", L"/p", 1, 443)
        == "[HTTP] Host: h, Path: /p, HTTPS: 1, Port: 443\n",
        "narrow http host");
    Expect(NarrowFormatHttpOpenFailed(2)
        == "[HTTP] WinHttpOpen failed: 2\n",
        "narrow http open");
    Expect(NarrowFormatHttpConnectFailed(3)
        == "[HTTP] WinHttpConnect failed: 3\n",
        "narrow http connect");
    Expect(NarrowFormatHttpOpenRequestFailed(4)
        == "[HTTP] WinHttpOpenRequest failed: 4\n",
        "narrow http openreq");
    Expect(NarrowFormatHttpHeaderCount(2)
        == "[HTTP] Header count: 2\n",
        "narrow http headers");
    Expect(NarrowFormatHttpBodySize(100)
        == "[HTTP] Body size: 100 bytes\n",
        "narrow http body");
    Expect(NarrowFormatHttpSendFailed(5)
        == "[HTTP] WinHttpSendRequest failed: 5\n",
        "narrow http send");
    Expect(NarrowFormatHttpReceiveFailed(6)
        == "[HTTP] WinHttpReceiveResponse failed: 6\n",
        "narrow http recv");
    Expect(NarrowFormatHttpStatusCode(200)
        == "[HTTP] Status code: 200\n",
        "narrow http status");
    Expect(NarrowFormatHttpResponseBodySize(42)
        == "[HTTP] Response body size: 42 bytes\n",
        "narrow http resp body");
    Expect(NarrowFormatPercentHexByte(0xAB) == "%AB", "narrow percent hex");
    Expect(NarrowFormatLayoutWarn("x")
        == "[LayoutEngine] WARNING: x\n",
        "narrow layout warn");
    Expect(NarrowFormatLayoutRegionsLabeled("After NMS", 7)
        == "[LayoutEngine] After NMS: 7 regions\n",
        "narrow layout regions");
    Expect(NarrowFormatLayoutDetectImage(800, 600, 1.333f, 1.0f, 1.0f, L"V3")
        .find("Detect image: 800x600") != std::string::npos,
        "narrow detect image");
    Expect(NarrowFormatLayoutQueryRow(0, 1, 0.9f, 1.0f, 2.0f, 3.0f, 4.0f, 5)
        .find("query[0]") != std::string::npos,
        "narrow query row");
    Expect(NarrowFormatLayoutPostprocessStats(10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
        .find("postprocess raw=10") != std::string::npos,
        "narrow postprocess");
    Expect(NarrowFormatDebugInt("[Tag] n=", 7) == "[Tag] n=7\n", "narrow debug int");
    Expect(NarrowFormatDebugULong("[Tag] e=", 9ul) == "[Tag] e=9\n", "narrow debug ulong");
    Expect(NarrowFormatDebugSize("[Tag] s=", 11) == "[Tag] s=11\n", "narrow debug size");
    Expect(NarrowFormatDebugCStr("[Tag] m=", "ok") == "[Tag] m=ok\n", "narrow debug cstr");

    // OWN-117: residual product OCR / engine narrow debug formatters.
    Expect(NarrowFormatOcrRejectedProviderUrl(L"bad")
        == "[OCR] Rejected provider image URL: bad\n",
        "narrow ocr reject url");
    Expect(NarrowFormatOcrProviderDownloadFailed(1, 3, 500, 0, L"timeout")
        == "[OCR] Provider image download failed attempt=1/3 status=500 body=0 err=timeout\n",
        "narrow ocr download fail");
    Expect(NarrowFormatOcrProviderContentTypeRejected(L"text/html")
        == "[OCR] Provider image content-type rejected: text/html\n",
        "narrow ocr content-type");
    Expect(NarrowFormatOcrFoundImageKey(2, L"img1")
        == "[OCR] Found image key (page 2): img1\n",
        "narrow ocr found key");
    Expect(std::string(NarrowOcrImageIsUrl()).find("URL") != std::string::npos,
        "narrow ocr image url");
    Expect(std::string(NarrowOcrImageIsBase64()).find("base64") != std::string::npos,
        "narrow ocr image b64");
    Expect(NarrowFormatOcrSavedImagesScoped(5)
        == "[OCR] Saved 5 images (scoped layoutParsingResults)\n",
        "narrow ocr saved scoped");
    Expect(NarrowFormatOcrSavedImages(3)
        == "[OCR] Saved 3 images\n",
        "narrow ocr saved");
    Expect(NarrowFormatOcrAsyncApiModel("PaddleOCR-VL-1.6", 1024)
        == "[OCR] Async API model=PaddleOCR-VL-1.6 body=1024 bytes\n",
        "narrow ocr async model");
    Expect(std::string(NarrowOcrAsyncJobSubmitted()).find("submitted") != std::string::npos,
        "narrow ocr async submitted");
    Expect(NarrowFormatOcrException("boom")
        == "[OCR] Exception: boom\n",
        "narrow ocr exception");
    Expect(NarrowFormatPaddleDocLayoutDetected(9)
        == "[PaddleDoc] Layout detected 9 original regions\n",
        "narrow paddledoc layout");
    Expect(NarrowFormatPaddleDocException("x")
        == "[PaddleDoc] Exception: x\n",
        "narrow paddledoc exception");
    Expect(NarrowFormatPaddleLocalException("y")
        == "[PaddleLocal] Exception: y\n",
        "narrow paddlelocal exception");
    Expect(NarrowFormatPpocrv6LoadedModel("det", "x", "y")
        == "[PPOCRv6] Loaded det model. input=x output=y\n",
        "narrow ppocrv6 loaded");
    Expect(NarrowFormatPpocrv6DbPostFailed("e")
        == "[PPOCRv6] OpenCV DBPostProcess failed: e\n",
        "narrow ppocrv6 dbpost");
    Expect(NarrowFormatPpocrv6CropFailed("c")
        == "[PPOCRv6] OpenCV crop failed: c\n",
        "narrow ppocrv6 crop");
    Expect(NarrowFormatPpocrv6DetBoxes(10, 4)
        == "[PPOCRv6] det boxes=10 output_shape_dims=4\n",
        "narrow ppocrv6 det");
    Expect(NarrowFormatPpocrv6RecBatchMismatch(3, 2)
        == "[PPOCRv6] recognition batch count mismatch inputs=3 results=2\n",
        "narrow ppocrv6 rec mismatch");
    Expect(NarrowFormatPpocrv6DroppedInvalidBoxes(2)
        == "[PPOCRv6] dropped 2 accepted line(s) with invalid box geometry\n",
        "narrow ppocrv6 dropped");
    Expect(NarrowFormatPpocrv6Exception("z")
        == "[PPOCRv6] Exception: z\n",
        "narrow ppocrv6 exception");
    Expect(NarrowFormatDebugWStr("[Tag] w=", L"hi") == "[Tag] w=hi\n", "narrow debug wstr");
    Expect(NarrowFormatDebugIntSize("[Tag] ", 1, 2) == "[Tag] 1 2\n", "narrow debug intsize");

    // OWN-118: complex multi-arg residual engine narrow debug formatters.
    Expect(NarrowFormatOcrCloudUploadImage("image/jpeg", 42, 0)
        == "[OCR] Cloud upload image: image/jpeg, 42 bytes\n",
        "narrow cloud upload jpeg");
    Expect(NarrowFormatOcrCloudUploadImage("image/png", 99, 1)
        == "[OCR] Cloud upload image: image/png, 99 bytes (JPEG encode fallback)\n",
        "narrow cloud upload png fallback");
    {
        const std::string probe = NarrowFormatPaddleServerProbe(
            "PaddleLocal", 1, 200, 1, 200, 1, 0, 4, 8192, L"none");
        Expect(probe.find("[PaddleLocal] server models=1/200 props=1/200 modelListed=1 multimodal=0")
            != std::string::npos, "narrow paddle server probe core");
        Expect(probe.find("slots=4 context=8192 warning=none") != std::string::npos,
            "narrow paddle server probe slots");
    }
    {
        const std::string local = NarrowFormatPaddleLocalMetrics(
            200, 1500u, 30000, 1024, 2048, 4096, 10, 20, 30,
            L"stop", L"none", L"");
        Expect(local.find("[PaddleLocal] status=200 elapsed=1500 timeout=30000")
            != std::string::npos, "narrow paddle local metrics head");
        Expect(local.find("tokens=10/20/30 finish=stop") != std::string::npos,
            "narrow paddle local metrics tokens");
    }
    {
        const std::string group = NarrowFormatPaddleDocGroupMetrics(
            L"g1", 3, 0, 1, 0, 100, 50, 1, 4, 200, 800u, 30000,
            512, L"image/png", 400, 1000u, 2000, 3000, 5, 6, 11,
            L"stop", L"none", L"");
        Expect(group.find("[PaddleDoc] group=g1 members=3 owner=0") != std::string::npos,
            "narrow paddledoc group head");
        Expect(group.find("crop=100x50") != std::string::npos,
            "narrow paddledoc group crop");
    }
    {
        const std::string pipe = NarrowFormatPaddleDocPipelineSummary(
            10, 4, 3, 1, 0, 0, 5, 1.5f, 1, 0, 1000, 800, L"group", 0);
        Expect(pipe.find("[PaddleDoc] blocks=10 groups=4 recognized=3") != std::string::npos,
            "narrow paddledoc pipeline head");
        Expect(pipe.find("mode=group fallback=0") != std::string::npos,
            "narrow paddledoc pipeline mode");
    }
    Expect(NarrowFormatPpocrv6Variant(
            L"mobile", L"cpu", 4, 8, L"max", 960, 960, 0.3f, 0.6f, 1.5f, "opencv")
        .find("[PPOCRv6] variant=mobile provider=cpu threads=4 recBatch=8")
            != std::string::npos,
        "narrow ppocrv6 variant");
    Expect(NarrowFormatPpocrv6RecPlan(12, 2, 6, 100)
        == "[PPOCRv6] rec plan: inputs=12 batches=2 batchSize=6 paddedWidthUnits=100\n",
        "narrow ppocrv6 rec plan");
    Expect(NarrowFormatPpocrv6FinalStats(10, 8, 7, 1, 0, 0, 0, 0)
        == "[PPOCRv6] det boxes=10 rec inputs=8 accepted blocks=7 crop skipped=1 batch fallback=0 single failed=0 geometry dropped=0\n",
        "narrow ppocrv6 final stats ok");
    Expect(NarrowFormatPpocrv6FinalStats(10, 8, 0, 1, 0, 0, 0, 1)
        == "[PPOCRv6] det boxes=10 rec inputs=8 accepted blocks=0 crop skipped=1 batch fallback=0 single failed=0 geometry dropped=0 (assemble failed)\n",
        "narrow ppocrv6 final stats assemble fail");

    // OWN-120: Win32 error / page-asset / size / status / ann / glob pure formatters.
    Expect(WideFormatWin32ErrorSuffix(L"Failed to disable HTTP redirects", 5ul)
        == L"Failed to disable HTTP redirects: 5", "win32 err suffix");
    Expect(WideFormatWin32Failed(L"WinHttpConnect", 12029ul)
        == L"WinHttpConnect failed: 12029", "win32 failed");
    Expect(WideFormatPageAssetId(3, 7) == L"3:7", "page asset id");
    Expect(WideFormatSizeWxH(1920, 1080) == L"1920x1080", "size wxh");
    Expect(WideFormatStatusCount(L"pages", 12) == L"pages: 12", "status count");
    Expect(WideFormatAnnId(42) == L"#42", "ann id");
    Expect(WideJoinGlob(L"C:\\tmp", L"*.png") == L"C:\\tmp\\*.png", "join glob");
    Expect(WideJoinGlob(L"C:\\tmp\\", L"*") == L"C:\\tmp\\*", "join glob trail");

    // OWN-121: localhost / ms-spaced / slash-count / count-label / dpi / page pure formatters.
    Expect(WideFormatLocalhostBase(8080) == L"http://127.0.0.1:8080", "localhost base");
    Expect(WideFormatLocalhostChatCompletions(8080)
        == L"http://127.0.0.1:8080/v1/chat/completions", "localhost chat");
    Expect(WideFormatMsSpaced(1500ul) == L"1500 ms", "ms spaced");
    Expect(WideFormatSlashCount(3, 10) == L"3/10", "slash count");
    Expect(WideFormatCountLabel(2, L"failed") == L"2 failed", "count label");
    Expect(WideFormatDpiLabel(150) == L"DPI: 150", "dpi label");
    Expect(WideFormatPageLabel(4) == L"Page 4", "page label");

    // OWN-122: count-prefix / slash-u / times / runtime-key / duplicate / page-meta pure formatters.
    Expect(WideFormatCountPrefix(L"OCR x", 5) == L"OCR x5", "count prefix");
    Expect(WideFormatCountPrefix(L"Q", 12) == L"Q12", "count prefix Q");
    Expect(WideFormatSlashCountU(3u, 10u) == L"3/10", "slash count u");
    Expect(WideFormatTimesInt(2) == L"2x", "times int");
    Expect(WideFormatImageRuntimeKeyPrefix(7) == L"image:runtime:7:", "image runtime key");
    Expect(WideFormatPdfRuntimeKey(3) == L"pdf:runtime:3", "pdf runtime key");
    Expect(WideFormatDuplicateSuffix(2) == L":duplicate:2", "dup suffix");
    Expect(WideFormatPageMetaSuffix(5, L"01:23") == L"P5 · 01:23", "page meta suffix");

    // OWN-123: paren-slash / thumbnail-gen / path-hash-page / ull / page-slash pure formatters.
    Expect(WideFormatParenSlashCount(3, 10) == L"(3/10)", "paren slash count");
    Expect(WideFormatThumbnailGenPrefix(42ull) == L"thumbnail.g42.", "thumbnail gen prefix");
    Expect(WideFormatPathHashPage(L"C:\\docs\\a.pdf", 7) == L"C:\\docs\\a.pdf#p7", "path hash page");
    Expect(WideFormatUll(123456789ull) == L"123456789", "ull");
    Expect(WideFormatPageSlashLabel(4) == L" / Page 4", "page slash label");

    // OWN-124: page-block/bbox/layout-asset/bbox-ltrb/tmp-pid-tick/hash-index +
    // 2-space/compact JSON field pure formatters.
    Expect(WideFormatPageBlockId(3, 7) == L"page_3:block_7", "page block id");
    Expect(WideFormatPageBboxId(12) == L"page_1:bbox_12", "page bbox id");
    Expect(WideFormatPageLayoutAssetId(4) == L"page_1:layout_4:asset", "page layout asset id");
    Expect(WideFormatBboxLtrb(10, 20, 30, 40) == L"10,20 - 30,40", "bbox ltrb");
    Expect(WideFormatTmpPidTick(1234ul, 5678ull) == L".tmp.1234.5678", "tmp pid tick");
    Expect(WideFormatHashIndex(9) == L"#9", "hash index");
    Expect(WideJsonFieldInt2(L"elapsedMs", 42) == L"  \"elapsedMs\": 42,\r\n", "json int2");
    Expect(WideJsonFieldUll2(L"bytes", 99ull) == L"  \"bytes\": 99,\r\n", "json ull2");
    Expect(WideJsonFieldIntCompact(L"count", 5) == L"\"count\":5", "json int compact");
    Expect(WideJsonFieldUllCompact(L"size", 8ull) == L"\"size\":8", "json ull compact");

    // OWN-125: source-rail / page-key / size-key / middot / titles / fingerprint pure formatters.
    Expect(WideFormatColonPageKey(3) == L":page:3", "colon page key");
    Expect(WideFormatThumbSizeSuffix(64, 48) == L"\n64x48", "thumb size suffix");
    Expect(WideFormatMiddotSlashCount(2, 5) == L" \x00b7 2/5", "middot slash count");
    Expect(WideFormatPageMetaLive(4, L"01:23") == L"P4 \x00b7 01:23", "page meta live");
    Expect(WideFormatImageTitle(7) == L"Image 7", "image title");
    Expect(WideFormatPdfTitle(2) == L"PDF 2", "pdf title");
    Expect(WideFormatCaptureTitle(9) == L"Capture 9", "capture title");
    Expect(WideFormatSlashCountBar(3, 10) == L"3/10 | ", "slash count bar");
    Expect(WideFormatSlashTotal(12) == L"/12", "slash total");
    Expect(WideFormatPageDotLabel(5) == L"p.5", "page dot label");
    Expect(WideFormatPdfPageDotLabel(8) == L"PDF p.8", "pdf page dot label");
    Expect(WideFormatPrefixSlashCount(L"D", 1, 4) == L"D1/4", "prefix slash count");
    Expect(WideFormatExProgressPrefix(42) == L"EX:42:", "ex progress prefix");
    Expect(WideFormatOcrFpSuffix(L"src-key", 3) == L"OCR:src-key:3|", "ocr fp suffix");
    Expect(WideFormatParenInt(7) == L" (7)", "paren int");
    Expect(WideFormatDotKindPidTick(L"tmp", 11ul, 22ull) == L".tmp.11.22", "dot kind pid tick");
    Expect(WideFormatCandidatePidTick(33ul, 44ull) == L".candidate.33.44", "candidate pid tick");
    Expect(WideFormatBackupPidTick(55ul, 66ull) == L".backup.55.66", "backup pid tick");

    // OWN-126: provider-asset / page-warn / offset-error / page-kind-order pure formatters.
    Expect(WideFormatPageProviderAssetId(3, 7) == L"page_3:provider_asset_7", "page provider asset id");
    Expect(WideFormatProviderAssetUri(3, 7)
        == L"zencrop-asset://provider/page_3/asset_7", "provider asset uri");
    Expect(WideFormatPageWarnPrefix(4) == L"Page 4: ", "page warn prefix");
    Expect(WideFormatUtf16OffsetSuffix(42ull) == L" at UTF-16 offset 42.", "utf16 offset suffix");
    Expect(WideFormatIntDotSuffix(L"page number: ", 9) == L"page number: 9.", "int dot suffix");
    Expect(WideFormatIntColonDetail(L"page ", 2, L"bad map") == L"page 2: bad map", "int colon detail");
    Expect(WideFormatPageId(5) == L"page_5", "page id");
    Expect(WideFormatAssetId(8) == L"asset_8", "asset id");
    Expect(WideFormatProviderAssetLocal(3) == L"provider_asset_3", "provider asset local");
    Expect(WideFormatPidTickCounterSuffix(11ul, 22ull, 3u, L".tmp")
        == L".11.22.3.tmp", "pid tick counter suffix");
    Expect(WideFormatPageKindOrderId(1, L"ppocrv6_line", 4)
        == L"page_1:ppocrv6_line_4", "page kind order id");

    // OWN-127: block/page-prefix/ann/legacy/fkey/http/json-index/point/job-dir/
    // endpoint/group/groups-failed/history-pin pure formatters.
    Expect(WideFormatBlockId(7) == L"block_7", "block id");
    Expect(WideFormatPagePrefix(3) == L"page_3:", "page prefix");
    Expect(WideFormatAnnIdPlain(42ull) == L"ann_42", "ann id plain");
    Expect(WideFormatLegacyId(5) == L"legacy_5", "legacy id");
    Expect(WideFormatFunctionKey(1) == L"F1", "function key");
    Expect(WideFormatHttpStatus(404) == L"HTTP 404", "http status");
    Expect(WideFormatJsonIndexOpen(9) == L"{\"index\":9", "json index open");
    Expect(WideFormatPointXy(10, 20) == L"10,20", "point xy");
    Expect(WideFormatJobDirError(5ul) == L"Failed to create job directory (5).", "job dir error");
    Expect(WideFormatEndpointHttp(503) == L"Endpoint returned HTTP 503", "endpoint http");
    Expect(WideFormatGroupId(4) == L"group_4", "group id");
    Expect(WideFormatGroupsFailedSuffix(3) == L"3 group(s) failed.", "groups failed suffix");
    Expect(WideFormatHistoryPinHeader(1) == L"\U0001F4CC #1  |  ", "history pin header");

    if (g_fail) { std::cerr << g_fail << " failures\n"; return 1; }
    std::cout << "ALL PASSED\n";
    return 0;
}
