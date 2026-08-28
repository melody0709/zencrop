#include "ocr/ui/DashboardBlockRuntimeIndex.h"
#include "ocr/ui/DashboardSourceMap.h"

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

struct FixtureBlock {
    std::wstring id;
    int pageIndex = 0;
    int order = 0;
    std::wstring content;
    std::wstring groupId;
    RECT bbox = {};
    double confidence = 0.9;
    bool edited = false;
};

static LONG Area(const RECT& r) {
    return (std::max)(0L, r.right - r.left) * (std::max)(0L, r.bottom - r.top);
}

static int LegacyIssues(const std::vector<FixtureBlock>& blocks, size_t index, LONG imageArea) {
    const auto& block = blocks[index];
    int count = block.edited ? 1 : 0;
    bool blank = std::all_of(block.content.begin(), block.content.end(),
        [](wchar_t ch) { return iswspace(ch) != 0; });
    if (blank) ++count;
    if (block.confidence >= 0.0 && block.confidence < 0.55) ++count;
    LONG area = Area(block.bbox);
    LONG width = (std::max)(0L, block.bbox.right - block.bbox.left);
    LONG height = (std::max)(0L, block.bbox.bottom - block.bbox.top);
    if (area > 0 && (width < 5 || height < 5 || area <= 64)) ++count;
    if (imageArea > 0 && area > imageArea * 8 / 10) ++count;
    else if (imageArea <= 0 && area > 2000000L) ++count;
    for (size_t j = 0; area > 0 && j < blocks.size(); ++j) {
        if (j == index || blocks[j].id == block.id || blocks[j].pageIndex != block.pageIndex) continue;
        LONG other = Area(blocks[j].bbox);
        RECT intersection = {
            (std::max)(block.bbox.left, blocks[j].bbox.left),
            (std::max)(block.bbox.top, blocks[j].bbox.top),
            (std::min)(block.bbox.right, blocks[j].bbox.right),
            (std::min)(block.bbox.bottom, blocks[j].bbox.bottom)};
        LONG overlap = Area(intersection);
        LONG smaller = (std::min)(area, other);
        if (smaller > 0 && overlap > smaller * 45 / 100) { ++count; break; }
    }
    return count;
}

static std::vector<FixtureBlock> MakeFixture(size_t count, int distribution) {
    std::mt19937 rng(0x5a17u + distribution);
    std::vector<FixtureBlock> blocks;
    blocks.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        FixtureBlock block;
        block.id = L"block:" + std::to_wstring(i);
        block.order = static_cast<int>((i * 37) % (count ? count : 1));
        block.content = i % 29 == 0 ? L"" : L"content";
        block.confidence = i % 31 == 0 ? 0.4 : 0.9;
        if (distribution == 0) {
            LONG x = static_cast<LONG>((i % 25) * 70);
            LONG y = static_cast<LONG>((i / 25) * 45);
            block.bbox = {x, y, x + 55, y + 30};
        } else if (distribution == 1) {
            LONG inset = static_cast<LONG>(i % 40);
            block.bbox = {100 + inset, 100 + inset, 900 - inset, 700 - inset};
        } else {
            LONG x = static_cast<LONG>(rng() % 1700);
            LONG y = static_cast<LONG>(rng() % 900);
            LONG w = 20 + static_cast<LONG>(rng() % 180);
            LONG h = 15 + static_cast<LONG>(rng() % 120);
            block.bbox = {x, y, x + w, y + h};
        }
        blocks.push_back(std::move(block));
    }
    return blocks;
}

static int Fail(const char* message) {
    std::cerr << "FAIL: " << message << "\n";
    return 1;
}

int main() {
    const LONG imageArea = 1920L * 1080L;
    volatile int sink = 0;
    for (size_t count : {size_t{100}, size_t{300}, size_t{1000}}) {
        for (int distribution = 0; distribution < 3; ++distribution) {
            auto blocks = MakeFixture(count, distribution);
            DashboardBlockRuntimeIndex index;
            auto buildStart = std::chrono::steady_clock::now();
            if (!index.Rebuild(blocks, imageArea)) return Fail("unique fixture IDs were rejected");
            auto buildEnd = std::chrono::steady_clock::now();
            for (size_t i = 0; i < blocks.size(); ++i) {
                if (index.IssueCount(i) != LegacyIssues(blocks, i, imageArea))
                    return Fail("cached issue result differs from legacy semantics");
                if (index.FindById(blocks[i].id) != i) return Fail("id lookup mismatch");
            }
            auto legacyStart = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 20; ++repeat)
                for (size_t i = 0; i < blocks.size(); ++i) sink += LegacyIssues(blocks, i, imageArea);
            auto legacyEnd = std::chrono::steady_clock::now();
            auto cachedStart = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 20; ++repeat)
                for (size_t i = 0; i < blocks.size(); ++i) sink += index.IssueCount(i);
            auto cachedEnd = std::chrono::steady_clock::now();
            auto micros = [](auto a, auto b) {
                return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
            };
            std::cout << "blocks=" << count << " distribution=" << distribution
                      << " build_us=" << micros(buildStart, buildEnd)
                      << " legacy_hot_us=" << micros(legacyStart, legacyEnd)
                      << " cached_hot_us=" << micros(cachedStart, cachedEnd) << "\n";
        }
    }

    std::vector<FixtureBlock> grouped(5);
    grouped[0] = {L"secondary", 0, 2, L"", L"g1", RECT{0, 0, 100, 30}, 0.9, false};
    grouped[1] = {L"owner", 0, 1, L"recognized", L"g1", RECT{0, 0, 100, 30}, 0.9, false};
    grouped[2] = {L"other-page", 1, 1, L"page two", L"g1", RECT{0, 0, 100, 30}, 0.9, false};
    grouped[3] = {L"empty-owner", 0, 3, L"", L"g2", RECT{0, 40, 100, 70}, 0.9, false};
    grouped[4] = {L"empty-secondary", 0, 4, L"", L"g2", RECT{0, 40, 100, 70}, 0.9, false};
    DashboardBlockRuntimeIndex groupedIndex;
    if (!groupedIndex.Rebuild(grouped, imageArea)) return Fail("grouped fixture rejected");
    if (groupedIndex.ContentOwnerIndex(0, grouped) != 1 ||
        groupedIndex.ContentOwnerIndex(1, grouped) != 1 ||
        groupedIndex.ContentOwnerIndex(2, grouped) != 2)
        return Fail("page-scoped content owner resolution is incorrect");
    if (groupedIndex.IssueCount(0) != 0 || groupedIndex.IssueCount(1) != 0)
        return Fail("legal secondary blank or same-group overlap was reported");
    if (groupedIndex.IssueCount(3) != 1 || groupedIndex.IssueCount(4) != 0)
        return Fail("all-empty group must report one deterministic issue");

    auto duplicates = MakeFixture(2, 0);
    duplicates[1].id = duplicates[0].id;
    DashboardBlockRuntimeIndex duplicateIndex;
    if (duplicateIndex.Rebuild(duplicates, imageArea) || !duplicateIndex.HasDuplicateIds() ||
        duplicateIndex.FindById(duplicates[0].id) != 0)
        return Fail("duplicate ID policy must reject and deterministically retain the first block");

    std::wstring source = L"# Repeat\r\n\r\n# Repeat\r\n\r\nEmoji \U0001F600\r\n";
    std::wstring canonical = DashboardSourceMap::NormalizeLf(source);
    size_t second = canonical.find(L"# Repeat", canonical.find(L"# Repeat") + 1);
    DashboardSourceEditRequest edit;
    edit.canonicalSource = L"markdown-body-lf";
    edit.offsetUnit = L"utf16-code-unit";
    edit.sourceStart = second;
    edit.sourceEnd = second + 8;
    edit.revisionSha256 = DashboardSourceMap::RevisionSha256(canonical);
    edit.expectedSource = L"# Repeat";
    std::wstring updated;
    if (!DashboardSourceMap::ApplyStrict(source, edit, L"## Changed", updated) ||
        updated.find(L"# Repeat\n\n## Changed") == std::wstring::npos)
        return Fail("strict range edit did not target the second repeated heading");
    std::wstring rejected;
    if (DashboardSourceMap::ApplyStrict(updated, edit, L"stale", rejected))
        return Fail("stale revision was accepted");
    edit.revisionSha256 = DashboardSourceMap::RevisionSha256(updated);
    edit.sourceStart = updated.find(L"Emoji");
    edit.sourceEnd = updated.size() - 1;
    edit.expectedSource = updated.substr(edit.sourceStart, edit.sourceEnd - edit.sourceStart);
    if (!DashboardSourceMap::ApplyStrict(updated, edit, L"Emoji \U0001F680", rejected))
        return Fail("UTF-16 emoji range edit failed");
    edit.sourceEnd = rejected.size() + 1;
    edit.revisionSha256 = DashboardSourceMap::RevisionSha256(rejected);
    if (DashboardSourceMap::ApplyStrict(rejected, edit, L"bad", updated))
        return Fail("out-of-bounds range was accepted");

    std::cout << "Dashboard optimization contracts passed (sink=" << sink << ").\n";
    return 0;
}
