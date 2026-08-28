#include "ocr/ui/dashboard/DashboardHistoryCache.h"

#include <iostream>
#include <windows.h>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static std::wstring TempDir() {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    return dir;
}

int main() {
    const std::wstring root = TempDir() + L"zencrop_hist_cache_root\\";
    CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring underA = root + L"a.png";
    const std::wstring underB = root + L"b.png";
    const std::wstring underOwned = root + L"owned.png";
    const std::wstring outside = TempDir() + L"zencrop_hist_cache_outside.png";

    // Touch files so canonicalize has real paths (not required for pure logic).
    HANDLE h = CreateFileW(underA.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = CreateFileW(underB.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = CreateFileW(underOwned.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = CreateFileW(outside.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    std::wstring canonA;
    Expect(DashboardHistoryCacheCanonicalizePath(underA, canonA), "canon a");
    Expect(!canonA.empty(), "canon a non-empty");
    Expect(DashboardHistoryCacheIsPathUnderDirectory(underA, root), "a under root");
    Expect(!DashboardHistoryCacheIsPathUnderDirectory(outside, root), "outside not under");

    DashboardHistoryModel retained;
    OcrDashboardHistoryItem keep;
    keep.imagePath = underB;
    keep.ownedCacheFiles.push_back(underOwned);
    retained.items.push_back(keep);

    OcrDashboardHistoryItem removed;
    removed.imagePath = underA;
    removed.ownedCacheFiles.push_back(underOwned); // still referenced by retained
    removed.ownedCacheFiles.push_back(outside);    // not under OCR dir

    // ShouldDeleteUnreferenced: empty / outside / referenced / free.
    Expect(!DashboardHistoryCacheShouldDeleteUnreferenced(retained, L"", root), "empty no");
    Expect(!DashboardHistoryCacheShouldDeleteUnreferenced(retained, outside, root), "outside no");
    Expect(!DashboardHistoryCacheShouldDeleteUnreferenced(retained, underB, root), "keep ref");
    Expect(DashboardHistoryCacheShouldDeleteUnreferenced(retained, underA, root), "free a");
    // excludingIndex 0 skips sole retained item → underB becomes free.
    Expect(DashboardHistoryCacheShouldDeleteUnreferenced(retained, underB, root, 0), "exclude free");

    // CollectUnreferenced: a free; owned still held by retained; outside rejected.
    auto free = DashboardHistoryCacheCollectUnreferencedPaths(retained, {removed}, root);
    Expect(free.size() == 1, "one free");
    if (!free.empty()) {
        std::wstring freeCanon;
        Expect(DashboardHistoryCacheCanonicalizePath(underA, freeCanon), "free canon");
        Expect(free.count(freeCanon) == 1, "free is a");
    }

    // After remove keep, owned becomes free too.
    DashboardHistoryModel empty;
    OcrDashboardHistoryItem dropKeep = keep;
    auto free2 = DashboardHistoryCacheCollectUnreferencedPaths(empty, {dropKeep}, root);
    Expect(free2.size() == 2, "two free after empty retained");

    // CountRefs covers imagePath + ownedCacheFiles.
    Expect(DashboardHistoryCacheCountRefs(retained, underB) == 1, "count image");
    Expect(DashboardHistoryCacheCountRefs(retained, underOwned) == 1, "count owned");
    Expect(DashboardHistoryCacheCountRefs(retained, underA) == 0, "count missing");
    Expect(DashboardHistoryCacheCountRefs(retained, underB, 0) == 0, "count exclude");

    // D-C-S8: SamePath + OutputRootInUse.
    Expect(DashboardHistoryCacheSamePath(underA, underA), "same path self");
    Expect(!DashboardHistoryCacheSamePath(underA, underB), "same path diff");
    Expect(!DashboardHistoryCacheSamePath(L"", underA), "same path empty");
    std::vector<std::wstring> roots = {underA, underB};
    Expect(DashboardHistoryCacheOutputRootInUse(underA, roots), "root in use");
    Expect(!DashboardHistoryCacheOutputRootInUse(outside, roots), "root outside");
    Expect(!DashboardHistoryCacheOutputRootInUse(L"", roots), "root empty");

    // Normalize returns non-empty lowered/canonical path for absolute-ish input.
    const std::wstring normalized = DashboardHistoryCacheNormalizePath(underA);
    Expect(!normalized.empty(), "normalize non-empty");
    Expect(normalized == WideToLower(normalized), "normalize lower");

    DeleteFileW(underA.c_str());
    DeleteFileW(underB.c_str());
    DeleteFileW(underOwned.c_str());
    DeleteFileW(outside.c_str());
    RemoveDirectoryW(root.c_str());

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
