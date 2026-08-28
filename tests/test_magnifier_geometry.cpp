#include "screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"
#include <windows.h>
#include <cstdio>
#include <vector>

static int g_failures = 0;

#define TEST(name) static void name()
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while(0)
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))
#define ASSERT_EQ_INT(expected, actual) do { \
    int e = (expected); \
    int a = (actual); \
    if (e != a) { \
        std::printf("  FAIL: %s:%d: expected %d, got %d\n", __FILE__, __LINE__, e, a); \
        g_failures++; \
    } \
} while(0)

static DWORD EncodedPixel(int x, int y) {
    return 0xFF000000u | ((DWORD)(x & 0xFF) << 16) | ((DWORD)(y & 0xFF) << 8) | 0x35u;
}

TEST(test_magnifier_connector_link_type_visibility) {
    RECT result = { 100, 100, 220, 180 };
    RECT source = { 20, 30, 80, 70 };

    ASSERT_TRUE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 0));
    ASSERT_TRUE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 1));
    ASSERT_TRUE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 2));
    ASSERT_FALSE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 3));
}

TEST(test_magnifier_connector_hidden_when_source_inside_result) {
    RECT result = { 100, 100, 260, 220 };
    RECT source = { 130, 130, 190, 170 };

    ASSERT_FALSE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 0));
    ASSERT_FALSE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 1));
    ASSERT_FALSE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 2));
}

TEST(test_magnifier_connector_hidden_for_shape_overlap) {
    RECT result = { 100, 100, 220, 180 };
    RECT source = { 190, 150, 280, 230 };

    ASSERT_TRUE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 0));
    ASSERT_TRUE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 1));
    ASSERT_FALSE(ScreenshotMagnifierConnectorVisibleLocal(result, source, false, 2));
}

TEST(test_magnifier_connector_ellipse_center_overlap) {
    RECT result = { 100, 100, 220, 220 };
    RECT source = { 170, 110, 230, 170 };

    ASSERT_FALSE(ScreenshotMagnifierConnectorVisibleLocal(result, source, true, 0));
    ASSERT_FALSE(ScreenshotMagnifierConnectorVisibleLocal(result, source, true, 1));
}

TEST(test_magnifier_creation_keeps_drag_rect_as_result) {
    ScreenshotAnnotation ann;
    ann.magnifierMagnification = 150;
    ScreenshotMagnifierSetResultRect(ann, { 100, 120, 400, 270 });
    ScreenshotMagnifierSetSourceRect(ann, ScreenshotMagnifierFallbackSourceRect(ann));

    RECT result = ScreenshotAnnotationNormalizeRect(ann.start, ann.end);
    RECT source = ScreenshotMagnifierSourceRect(ann);

    ASSERT_EQ_INT(100, result.left);
    ASSERT_EQ_INT(120, result.top);
    ASSERT_EQ_INT(400, result.right);
    ASSERT_EQ_INT(270, result.bottom);
    ASSERT_EQ_INT(150, source.left);
    ASSERT_EQ_INT(145, source.top);
    ASSERT_EQ_INT(350, source.right);
    ASSERT_EQ_INT(245, source.bottom);
}

TEST(test_magnifier_draw_samples_source_rect_pixels) {
    const int width = 160;
    const int height = 120;
    std::vector<DWORD> sourcePixels((size_t)width * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            sourcePixels[(size_t)y * width + x] = EncodedPixel(x, y);
        }
    }

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDc, bitmap);
    DWORD* out = static_cast<DWORD*>(bits);
    std::fill(out, out + (size_t)width * height, 0xFF101010u);

    RECT dest = { 80, 40, 120, 80 };
    RECT source = { 20, 30, 40, 50 };
    ScreenshotDrawMagnifierLocal(memDc, sourcePixels.data(), width, height, 0, 0,
        dest, source, false, 0, RGB(250, 3, 15), 2, 3, 200, false, false);
    GdiFlush();

    DWORD actual = out[(size_t)60 * width + 100] & 0x00FFFFFFu;
    DWORD expected = EncodedPixel(30, 40) & 0x00FFFFFFu;
    ASSERT_EQ_INT((int)expected, (int)actual);
    ASSERT_EQ_INT((int)0x00101010u, (int)(out[(size_t)30 * width + 20] & 0x00FFFFFFu));

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

int main() {
    test_magnifier_connector_link_type_visibility();
    test_magnifier_connector_hidden_when_source_inside_result();
    test_magnifier_connector_hidden_for_shape_overlap();
    test_magnifier_connector_ellipse_center_overlap();
    test_magnifier_creation_keeps_drag_rect_as_result();
    test_magnifier_draw_samples_source_rect_pixels();

    if (g_failures == 0) {
        std::printf("All magnifier geometry tests passed.\n");
        return 0;
    }
    std::printf("%d failures.\n", g_failures);
    return 1;
}
