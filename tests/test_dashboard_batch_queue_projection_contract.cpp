#include "ocr/ui/dashboard/DashboardBatchQueueProjection.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    DashboardBatchQueueCounts empty{};
    Expect(DashboardBatchQueueTotal(empty) == 0, "empty total");
    Expect(!DashboardBatchQueueIsBusy(empty), "empty not busy");
    Expect(!DashboardBatchQueueCanClearFinished(empty), "empty no clear");

    DashboardBatchQueueCounts busy{2, 1, 0, 3};
    Expect(DashboardBatchQueueTotal(busy) == 6, "busy total");
    Expect(DashboardBatchQueueIsBusy(busy), "busy");
    Expect(!DashboardBatchQueueCanClearFinished(busy), "busy no clear");

    DashboardBatchQueueCounts done{0, 0, 1, 4};
    Expect(DashboardBatchQueueCanClearFinished(done), "can clear");
    Expect(!DashboardBatchQueueIsBusy(done), "done not busy");

    Expect(DashboardBatchQueueClampIndex(0, 0) == -1, "clamp empty");
    Expect(DashboardBatchQueueClampIndex(-3, 5) == -1, "clamp neg");
    Expect(DashboardBatchQueueClampIndex(2, 5) == 2, "clamp mid");
    Expect(DashboardBatchQueueClampIndex(99, 5) == 4, "clamp high");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
