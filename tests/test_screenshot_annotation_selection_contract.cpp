#include "screenshot/editor/ScreenshotAnnotationSelection.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    Expect(ScreenshotAnnotationSelectionAfterErase(-1, 0) == -1, "erase none");
    Expect(ScreenshotAnnotationSelectionAfterErase(2, 2) == -1, "erase selected");
    Expect(ScreenshotAnnotationSelectionAfterErase(3, 1) == 2, "erase before");
    Expect(ScreenshotAnnotationSelectionAfterErase(1, 3) == 1, "erase after");

    Expect(ScreenshotAnnotationSelectionAfterInsert(-1, 4) == 4, "insert select");
    Expect(ScreenshotAnnotationSelectionAfterInsert(0, 2) == 2, "insert reselect");
    Expect(ScreenshotAnnotationSelectionAfterInsert(3, 0) == 0, "insert head");

    Expect(ScreenshotAnnotationSelectionClamp(5, 0) == -1, "clamp empty");
    Expect(ScreenshotAnnotationSelectionClamp(-2, 3) == -1, "clamp neg");
    Expect(ScreenshotAnnotationSelectionClamp(1, 3) == 1, "clamp mid");
    Expect(ScreenshotAnnotationSelectionClamp(9, 3) == 2, "clamp high");

    Expect(ScreenshotAnnotationSelectionIsValid(0, 1), "valid");
    Expect(!ScreenshotAnnotationSelectionIsValid(-1, 1), "invalid neg");
    Expect(!ScreenshotAnnotationSelectionIsValid(1, 1), "invalid high");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
