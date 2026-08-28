#include "screenshot/ScreenshotTypes.h"
#include "screenshot/ScreenshotKeyboardShortcuts.h"
#include "ocr/ui/OcrCopyToastWindow.h"
#include "screenshot/annotation/AnnotationTypes.h"

#include <iostream>
#include <set>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
}

int main() {
    // Round-trip tool commands used by annotation model.
    const AnnotationType types[] = {
        AnnotationType::Geometry, AnnotationType::Arrow, AnnotationType::BrokenLine,
        AnnotationType::Pencil, AnnotationType::Marker, AnnotationType::Mosaic,
        AnnotationType::Text, AnnotationType::Eraser, AnnotationType::Serial,
    };
    for (AnnotationType t : types) {
        auto cmd = AnnotationTypeToToolCommand(t);
        auto back = ToolCommandToAnnotationType(cmd);
        Expect(back == t, "round-trip AnnotationType");
    }

    // Non-tool commands must not map to a real annotation type.
    Expect(ToolCommandToAnnotationType(ScreenshotToolbarCommand::Copy) == AnnotationType::None, "Copy -> None");
    Expect(ToolCommandToAnnotationType(ScreenshotToolbarCommand::Confirm) == AnnotationType::None, "Confirm -> None");
    Expect(ToolCommandToAnnotationType(ScreenshotToolbarCommand::Cancel) == AnnotationType::None, "Cancel -> None");

    // Independent roles keep distinct tool commands.
    Expect(AnnotationTypeToToolCommand(AnnotationType::None) == ScreenshotToolbarCommand::Confirm, "None -> Confirm");

    // Sanity: tool enums are distinct.
    std::set<int> tools = {
        (int)ScreenshotToolbarCommand::ToolGeometry,
        (int)ScreenshotToolbarCommand::ToolArrow,
        (int)ScreenshotToolbarCommand::ToolPencil,
        (int)ScreenshotToolbarCommand::ToolMarker,
        (int)ScreenshotToolbarCommand::ToolText,
        (int)ScreenshotToolbarCommand::ToolMosaic,
        (int)ScreenshotToolbarCommand::ToolEraser,
        (int)ScreenshotToolbarCommand::ToolSerial,
        (int)ScreenshotToolbarCommand::ToolBrokenLine,
        (int)ScreenshotToolbarCommand::ToolMagnifier,
        (int)ScreenshotToolbarCommand::ToolHighLight,
        (int)ScreenshotToolbarCommand::ToolWatermark,
        (int)ScreenshotToolbarCommand::ToolAutoMosaic,
    };
    Expect(tools.size() == 13, "13 distinct tool commands");

    // Shared "OCR and copy" shortcut: screenshot Adjust + OCR-mode Adjust (silent
    // copy). Ctrl+Shift+C remains the separate color-format shortcut in the
    // screenshot overlay keyboard dispatcher.
    Expect(ScreenshotIsCopyOcrShortcut('C', false, true, false), "Shift+C invokes Copy OCR");
    Expect(!ScreenshotIsCopyOcrShortcut('C', true, true, false), "Ctrl+Shift+C does not invoke Copy OCR");
    Expect(!ScreenshotIsCopyOcrShortcut('C', false, false, false), "bare C does not invoke Copy OCR");
    Expect(!ScreenshotIsCopyOcrShortcut('C', false, true, true), "Alt+Shift+C does not invoke Copy OCR");
    Expect(!ScreenshotIsCopyOcrShortcut('V', false, true, false), "Shift+V does not invoke Copy OCR");
    // Policy is mode-agnostic: OCR crop sessions gate enablement via
    // OverlayWindow::m_enableSilentOcrCopy, not via a different chord.
    Expect(ScreenshotIsLongShotCopyShortcut('C', true, false, false), "LongShot Ctrl+C copies and closes");
    Expect(!ScreenshotIsLongShotCopyShortcut('C', false, false, false), "LongShot bare C does not copy");
    Expect(!ScreenshotIsLongShotCopyShortcut('C', true, true, false), "LongShot Ctrl+Shift+C does not copy");
    Expect(!ScreenshotIsLongShotCopyShortcut('C', true, false, true), "LongShot Ctrl+Alt+C does not copy");
    Expect(!ScreenshotIsLongShotCopyShortcut('V', true, false, false), "LongShot Ctrl+V does not copy");
    Expect(ScreenshotIsLongShotCloseShortcut(0x1b, false, false, false), "LongShot Escape closes");
    Expect(!ScreenshotIsLongShotCloseShortcut(0x1b, true, false, false), "LongShot Ctrl+Escape does not close");
    Expect(!ScreenshotIsLongShotCloseShortcut('C', false, false, false), "LongShot C does not close");
    Expect(OcrCopyToastWindow::AutoCloseMs == 3000, "OCR copy toast auto closes after 3 seconds");
    Expect(OcrCopyToastWindow::FadeDurationMs == 300, "OCR copy toast fades over 300ms");

    if (g_fail) {
        std::cerr << g_fail << " failures\n";
        return 1;
    }
    std::cout << "ALL PASSED\n";
    return 0;
}
