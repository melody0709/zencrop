#include "ocr/ui/DashboardModels.h"

#include <iostream>
#include <string>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    Expect(DashboardItemStatusFromBatch(BatchOcrTaskStatus::Pending) == DashboardItemStatus::Pending, "pending");
    Expect(DashboardItemStatusFromBatch(BatchOcrTaskStatus::Recognizing) == DashboardItemStatus::Recognizing, "rec");
    Expect(DashboardItemStatusFromBatch(BatchOcrTaskStatus::Writing) == DashboardItemStatus::Writing, "write");
    Expect(DashboardItemStatusFromBatch(BatchOcrTaskStatus::Completed) == DashboardItemStatus::Completed, "done");
    Expect(DashboardItemStatusFromBatch(BatchOcrTaskStatus::Failed) == DashboardItemStatus::Failed, "fail");
    Expect(DashboardItemStatusFromBatch(BatchOcrTaskStatus::Canceled) == DashboardItemStatus::Canceled, "cancel");

    Expect(DashboardProjectionFileName(L"C:\\a\\b\\c.png") == L"c.png", "file name");
    Expect(DashboardProjectionFileName(L"only.txt") == L"only.txt", "bare name");
    Expect(DashboardProjectionNormalizePath(L"C:/Tmp/A/") == L"c:\\tmp\\a", "normalize");
    Expect(DashboardProjectionTextEquals(L"AbC", L"abc"), "text eq");
    Expect(!DashboardProjectionTextEquals(L"", L"x"), "empty not eq");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
