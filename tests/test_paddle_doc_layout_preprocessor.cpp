#include "ocr/layout/PaddleDocLayoutPreprocessor.h"

#include <gdiplus.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const char* message) {
    if (!condition) Fail(message);
}

HBITMAP CreateSolidBitmap(BYTE red, BYTE green, BYTE blue) {
    Gdiplus::Bitmap bitmap(2, 3, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(255, red, green, blue));
    HBITMAP hBitmap = nullptr;
    if (bitmap.GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBitmap) != Gdiplus::Ok) {
        return nullptr;
    }
    return hBitmap;
}

bool NearlyEqual(float actual, float expected) {
    return std::fabs(actual - expected) < 0.0001f;
}

} // namespace

int wmain() {
    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    Expect(Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) == Gdiplus::Ok,
        "GDI+ startup failed");

    HBITMAP source = CreateSolidBitmap(64, 128, 192);
    Expect(source != nullptr, "failed to create source bitmap");

    PaddleDocLayoutInput input;
    std::string error;
    const bool success = BuildPaddleDocLayoutInput(source, input, &error);
    DeleteObject(source);
    Gdiplus::GdiplusShutdown(token);
    Expect(success, "layout preprocessing failed");
    Expect(input.originalWidth == 2 && input.originalHeight == 3,
        "original dimensions are not preserved");
    Expect(NearlyEqual(input.scaleWidth, 400.0f) &&
        NearlyEqual(input.scaleHeight, 800.0f / 3.0f),
        "scale factors use the original width and height");

    const size_t plane = static_cast<size_t>(kPaddleDocLayoutInputSize) *
        kPaddleDocLayoutInputSize;
    Expect(input.chw.size() == plane * 3, "tensor has the expected NCHW payload");
    const size_t sample = plane / 2;
    Expect(NearlyEqual(input.chw[sample], 64.0f / 255.0f),
        "channel 0 must contain red values");
    Expect(NearlyEqual(input.chw[plane + sample], 128.0f / 255.0f),
        "channel 1 must contain green values");
    Expect(NearlyEqual(input.chw[plane * 2 + sample], 192.0f / 255.0f),
        "channel 2 must contain blue values");

    std::cout << "Paddle Doc layout preprocessor contract passed\n";
    return 0;
}
