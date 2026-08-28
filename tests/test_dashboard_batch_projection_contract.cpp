#include "ocr/ui/dashboard/DashboardBatchProjection.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    Expect(DashboardBatchStatusIsActive(BatchOcrTaskStatus::Pending), "pending active");
    Expect(DashboardBatchStatusIsActive(BatchOcrTaskStatus::Recognizing), "rec active");
    Expect(DashboardBatchStatusIsActive(BatchOcrTaskStatus::Writing), "write active");
    Expect(!DashboardBatchStatusIsActive(BatchOcrTaskStatus::Completed), "done not active");
    Expect(DashboardBatchStatusIsTerminal(BatchOcrTaskStatus::Completed), "done terminal");
    Expect(DashboardBatchStatusIsTerminal(BatchOcrTaskStatus::Failed), "fail terminal");
    Expect(DashboardBatchStatusIsFailureLike(BatchOcrTaskStatus::Canceled), "cancel fail-like");
    Expect(!DashboardBatchStatusIsFailureLike(BatchOcrTaskStatus::Completed), "done not fail-like");
    Expect(DashboardBatchCompletionTokenMatches(3, 3), "token match");
    Expect(!DashboardBatchCompletionTokenMatches(3, 4), "token stale");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
