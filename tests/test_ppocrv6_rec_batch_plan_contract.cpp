#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

#include "ocr/engine/PPOcrV6RecBatchPlan.h"

namespace {

int g_failures = 0;

void Expect(bool cond, const wchar_t* name) {
    if (cond) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L"\n";
    ++g_failures;
}

void ExpectEqI(long long got, long long want, const wchar_t* name) {
    if (got == want) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L" got=" << got << L" want=" << want << L"\n";
    ++g_failures;
}

struct Line {
    size_t sourceBoxIndex = 0;
    int mark = 0;
};

void TestOrderByWidth() {
    std::wcout << L"\n[order by width]\n";
    // reading order widths: long, short, mid
    std::vector<int> widths = { 3200, 320, 640 };
    auto order = PPOcrV6RecBatch::OrderByWidthAscending(widths);
    Expect(order.size() == 3, L"order size 3");
    Expect(order[0] == 1 && order[1] == 2 && order[2] == 0, L"short→mid→long indices");
}

void TestBatchCapOnly() {
    std::wcout << L"\n[batch size cap]\n";
    std::vector<int> widths = { 100, 110, 120, 130, 140 };
    auto order = PPOcrV6RecBatch::OrderByWidthAscending(widths);
    auto batches = PPOcrV6RecBatch::BuildBatches(order, widths, 2, /*ratio disabled*/ 0.0f);
    ExpectEqI(static_cast<long long>(batches.size()), 3, L"5 items batch=2 → 3 batches");
    ExpectEqI(static_cast<long long>(batches[0].size()), 2, L"batch0 size 2");
    ExpectEqI(static_cast<long long>(batches[2].size()), 1, L"batch2 size 1");
}

void TestWidthRatioSplit() {
    std::wcout << L"\n[width ratio split]\n";
    // After sort: 100, 120, 3000 — ratio 3000/100=30 > 2 → long alone
    std::vector<int> widths = { 3000, 100, 120 };
    auto order = PPOcrV6RecBatch::OrderByWidthAscending(widths);
    auto batches = PPOcrV6RecBatch::BuildBatches(order, widths, 8, 2.0f);
    Expect(batches.size() >= 2, L"ratio forces >=2 batches");
    // First batch should be the two short ones
    ExpectEqI(static_cast<long long>(batches[0].size()), 2, L"short pair together");
    ExpectEqI(static_cast<long long>(batches.back().size()), 1, L"long alone");
    Expect(batches.back()[0] == 0, L"long is original index 0");

    const long long packed = PPOcrV6RecBatch::TotalPaddedWidthUnits(batches, widths);
    // Naive reading-order single batch of 3 would pad all to 3000 → 9000
    // Packed: max(100,120)*2 + 3000*1 = 240 + 3000 = 3240
    ExpectEqI(packed, 3240, L"padded units reduced vs all-to-max");
    Expect(packed < 9000, L"packed better than naive full pad");
}

void TestRestoreReadingOrder() {
    std::wcout << L"\n[restore sourceBoxIndex order]\n";
    std::vector<Line> lines = {
        { 2, 20 },
        { 0, 0 },
        { 1, 10 },
    };
    PPOcrV6RecBatch::SortBySourceBoxIndexMember(lines);
    Expect(lines[0].sourceBoxIndex == 0 && lines[0].mark == 0, L"first is box0");
    Expect(lines[1].sourceBoxIndex == 1 && lines[1].mark == 10, L"second is box1");
    Expect(lines[2].sourceBoxIndex == 2 && lines[2].mark == 20, L"third is box2");
}

void TestResolveBatchSize() {
    std::wcout << L"\n[resolve batch size]\n";
    ExpectEqI(PPOcrV6RecBatch::ResolveRecBatchSize(0), 6, L"0 → Auto 6");
    ExpectEqI(PPOcrV6RecBatch::ResolveRecBatchSize(-1), 6, L"neg → Auto 6");
    ExpectEqI(PPOcrV6RecBatch::ResolveRecBatchSize(1), 1, L"1 kept");
    ExpectEqI(PPOcrV6RecBatch::ResolveRecBatchSize(4), 4, L"4 kept");
    ExpectEqI(PPOcrV6RecBatch::ResolveRecBatchSize(8), 8, L"8 kept");
    ExpectEqI(PPOcrV6RecBatch::ResolveRecBatchSize(99), 8, L"over → 8");
}

void TestEmpty() {
    std::wcout << L"\n[empty inputs]\n";
    std::vector<int> widths;
    auto order = PPOcrV6RecBatch::OrderByWidthAscending(widths);
    auto batches = PPOcrV6RecBatch::BuildBatches(order, widths, 4, 2.0f);
    Expect(order.empty() && batches.empty(), L"empty stays empty");
}

} // namespace

int wmain() {
    std::wcout << L"PP-OCRv6 recognition batch plan contract tests\n";
    TestOrderByWidth();
    TestBatchCapOnly();
    TestWidthRatioSplit();
    TestRestoreReadingOrder();
    TestResolveBatchSize();
    TestEmpty();
    if (g_failures == 0) {
        std::wcout << L"\nALL PASSED\n";
        return 0;
    }
    std::wcerr << L"\nFAILURES: " << g_failures << L"\n";
    return 1;
}
