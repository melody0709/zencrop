#include "screenshot/annotation/AnnotationTypes.h"
#include "screenshot/ScreenshotTypes.h"
#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
}

int main() {
    // Independent behavior roles are not AnnotationType enum values.
    Expect((int)AnnotationRole::HighLight > 0, "highlight role");
    Expect((int)AnnotationRole::Magnifier > 0, "magnifier role");
    Expect((int)AnnotationRole::Watermark > 0, "watermark role");
    Expect((int)AnnotationRole::AutoMosaicRect > 0, "automosaic role");

    // Tool commands for role-backed tools exist.
    Expect((int)ScreenshotToolbarCommand::ToolHighLight != (int)ScreenshotToolbarCommand::ToolGeometry, "hl tool");
    Expect((int)ScreenshotToolbarCommand::ToolMagnifier != (int)ScreenshotToolbarCommand::ToolText, "mag tool");
    Expect((int)ScreenshotToolbarCommand::ToolWatermark != (int)ScreenshotToolbarCommand::ToolSerial, "wm tool");
    Expect((int)ScreenshotToolbarCommand::ToolAutoMosaic != (int)ScreenshotToolbarCommand::ToolMosaic, "am tool");

    // Valid type range
    Expect(IsValidAnnotationType(AnnotationType::None), "none valid");
    Expect(IsValidAnnotationType(AnnotationType::Serial), "serial valid");
    Expect(!IsValidAnnotationType(AnnotationType::_Count), "count invalid");
    Expect(!IsValidAnnotationType(static_cast<AnnotationType>(99)), "99 invalid");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
