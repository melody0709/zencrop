#include "ocr/ui/dashboard/DashboardHistoryStore.h"

#include <iostream>
#include <set>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int wmain() {
    Expect(DashboardHistoryIsEmptyJson(L"[]"), "empty array");
    Expect(DashboardHistoryIsEmptyJson(L"  [  ]  "), "empty padded");
    Expect(!DashboardHistoryIsEmptyJson(L"[{}]"), "not empty");
    Expect(DashboardHistoryIsStructurallyCompleteJson(L"[]"), "struct empty");
    Expect(!DashboardHistoryIsStructurallyCompleteJson(L"[{"), "truncated");
    Expect(!DashboardHistoryIsStructurallyCompleteJson(L"[{]"), "bad braces");

    std::wstring json =
        L"[{\n"
        L"  \"timestamp\": \"2026-01-01T00:00:00\",\n"
        L"  \"imagePath\": \"C:\\\\tmp\\\\a.png\",\n"
        L"  \"engineMode\": \"paddle_cloud\",\n"
        L"  \"text\": \"hello\",\n"
        L"  \"elapsedMs\": 12,\n"
        L"  \"bboxes\": [{\"left\":1,\"top\":2,\"right\":3,\"bottom\":4,\"class\":\"text\"}],\n"
        L"  \"blocks\": [],\n"
        L"  \"rawOcrJson\": \"{}\",\n"
        L"  \"debugOutputImagesJson\": \"\",\n"
        L"  \"ownedCacheFiles\": [\"cache1.png\"]\n"
        L"}]";

    Expect(DashboardHistoryIsStructurallyCompleteJson(json), "struct ok");
    auto items = DashboardHistoryParseJson(json);
    Expect(items.size() == 1, "parse one");
    if (!items.empty()) {
        Expect(items[0].imagePath.find(L"a.png") != std::wstring::npos, "image path");
        Expect(items[0].engineMode == L"paddle_cloud", "engine mode");
        Expect(items[0].text == L"hello", "text");
        Expect(items[0].elapsedMs == 12, "elapsed");
        Expect(items[0].bboxes.size() == 1, "bbox");
        Expect(items[0].ownedCacheFiles.size() == 1, "owned cache");
    }

    auto round = DashboardHistorySerializeJson(items);
    Expect(DashboardHistoryIsStructurallyCompleteJson(round), "serialize struct");
    auto items2 = DashboardHistoryParseJson(round);
    Expect(items2.size() == items.size(), "round-trip count");
    if (!items2.empty() && !items.empty()) {
        Expect(items2[0].text == items[0].text, "round-trip text");
        Expect(items2[0].engineMode == items[0].engineMode, "round-trip engine mode");
        Expect(items2[0].elapsedMs == items[0].elapsedMs, "round-trip elapsed");
    }

    std::set<std::wstring> keys = {L"manifest:c:\\a\\m.json", L"manifest:c:\\b\\m.json"};
    auto djson = DashboardHistorySerializeDismissedManifests(keys);
    std::set<std::wstring> parsed;
    Expect(DashboardHistoryParseDismissedManifestKeys(djson, parsed), "parse dismissed");
    Expect(parsed.size() == 2, "dismissed count");
    Expect(parsed.count(L"manifest:c:\\a\\m.json") == 1, "key a");

    // Reject non-manifest keys
    std::set<std::wstring> bad;
    Expect(!DashboardHistoryParseDismissedManifestKeys(L"[\"not-a-manifest\"]", bad), "reject bad key");

    Expect(DashboardHistoryNormalizePath(L"C:/Tmp/A.PNG") == L"c:\\tmp\\a.png", "normalize path");
    // Source instance ids are GUID-shaped: {8-4-4-4-12}
    const std::wstring guid = L"{12345678-1234-1234-1234-1234567890ab}";
    auto imgKey = DashboardHistoryBuildImageDismissalKey(
        L"C:/out/manifest.json", guid, L"", L"");
    Expect(imgKey.find(L"manifest:") == 0, "img key prefix");
    Expect(imgKey.find(L"|image:id:") != std::wstring::npos, "img key id");
    auto pdfKey = DashboardHistoryBuildPdfDismissalKey(
        L"C:/out/manifest.json", L"2026-01-01", L"C:/docs/a.pdf");
    Expect(pdfKey.find(L"|pdf:created:") != std::wstring::npos, "pdf key");
    auto histKey = DashboardHistoryBuildHistoryItemDismissalKey(
        L"C:/out/manifest.json", L"");
    Expect(histKey == DashboardHistoryNormalizeDismissalKey(
        DashboardHistoryDismissalBaseKey(L"C:/out/manifest.json")), "hist path-wide");

    // D-C-S4: pure IsDismissed (primary key + legacy path-wide fallback).
    std::vector<std::wstring> dismissed = {imgKey};
    Expect(DashboardHistoryIsDismissalKeyPresent(dismissed, imgKey), "present primary");
    Expect(!DashboardHistoryIsDismissalKeyPresent(dismissed, pdfKey), "absent pdf");
    Expect(DashboardHistoryIsImageJobDismissed(
        dismissed, L"C:/out/manifest.json", guid, L"", L""), "image dismissed by primary");
    Expect(!DashboardHistoryIsPdfJobDismissed(
        dismissed, L"C:/out/manifest.json", L"2026-01-01", L"C:/docs/a.pdf"),
        "pdf not dismissed by image key");
    // Legacy base key still matches image job when primary differs.
    const std::wstring legacyBase = DashboardHistoryNormalizeDismissalKey(
        DashboardHistoryDismissalBaseKey(L"C:/out/manifest.json"));
    std::vector<std::wstring> legacyOnly = {legacyBase};
    Expect(DashboardHistoryIsImageJobDismissed(
        legacyOnly, L"C:/out/manifest.json", guid, L"", L""), "image dismissed by legacy");
    Expect(DashboardHistoryIsPdfJobDismissed(
        legacyOnly, L"C:/out/manifest.json", L"2026-01-01", L"C:/docs/a.pdf"),
        "pdf dismissed by legacy");

    if (g_fail) {
        std::cerr << g_fail << " failures\n";
        return 1;
    }
    std::cout << "ALL PASSED\n";
    return 0;
}

// MSVC console entry
int main() { return wmain(); }
